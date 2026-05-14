#include "vst_host.hpp"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <functional>
#include <mutex>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// IPlugFrame implementation - lets the plugin ask the host to resize its
// window
// ---------------------------------------------------------------------------
class PlugFrame : public Steinberg::U::Implements<
                      Steinberg::U::Directly<Steinberg::IPlugFrame>> {
  public:
    HWND hwnd{nullptr};

    Steinberg::tresult PLUGIN_API resizeView(
        Steinberg::IPlugView *view, Steinberg::ViewRect *newSize) override {
        if (!newSize || !hwnd)
            return Steinberg::kInvalidArgument;

        int w = newSize->getWidth();
        int h = newSize->getHeight();

        // Grow/shrink the container window to fit the new client area
        RECT client{}, window{};
        GetClientRect(hwnd, &client);
        GetWindowRect(hwnd, &window);
        int ncW = (window.right - window.left) - (client.right - client.left);
        int ncH = (window.bottom - window.top) - (client.bottom - client.top);
        SetWindowPos(hwnd, nullptr, 0, 0, w + ncW, h + ncH,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        view->onSize(newSize);
        return Steinberg::kResultOk;
    }
};

// Callback type stored in GWLP_USERDATA - avoids exposing VstHost::Impl to the
// proc
using PluginWindowCloseCallback = std::function<void()>;

static LRESULT CALLBACK PluginWindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam);

struct VstHost::Impl {
    // VST3 objects
    Steinberg::IPtr<Steinberg::Vst::HostApplication> hostApp;
    std::shared_ptr<VST3::Hosting::Module> module;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor;

    // Processing
    Steinberg::Vst::HostProcessData processData;
    Steinberg::Vst::EventList eventList;
    Steinberg::Vst::ParameterChanges inputParamChanges;
    Steinberg::Vst::ParameterChangeTransfer paramTransfer{256};
    Steinberg::Vst::ProcessContext processContext{};

    // State
    std::string pluginName;
    std::vector<VstParam> params;
    std::atomic<bool> loaded{false};
    std::atomic<bool> processing{false};
    bool separateController{
        false}; // controller created separately from component
    double sampleRate{44100.0};
    int blockSize{256};
    int64_t samplePos{0};

    // Thread-safe event queue (main → audio thread)
    std::mutex eventMutex;
    std::vector<Steinberg::Vst::Event> pendingEvents;
    std::vector<Steinberg::Vst::Event> swapEvents;

    // Editor (native plugin GUI)
    Steinberg::IPtr<Steinberg::IPlugView> plugView;
    Steinberg::IPtr<PlugFrame> plugFrame;
    HWND pluginHwnd{nullptr};
    bool editorOpen{false};

    bool doLoad(const std::string &path, double sr, int bs);
    void doUnload();
    void collectParams();
    void activateBuses();
    bool doOpenEditor();
    void doCloseEditor();
};

// ---------------------------------------------------------------------------

bool VstHost::Impl::doLoad(const std::string &path, double sr, int bs) {
    sampleRate = sr;
    blockSize  = bs;

    // Host application context
    hostApp = Steinberg::owned(new Steinberg::Vst::HostApplication());
    Steinberg::Vst::PluginContextFactory::instance().setPluginContext(hostApp);

    // Load the .vst3 bundle
    std::string error;
    module = VST3::Hosting::Module::create(path, error);
    if (!module)
        return false;

    // Find the audio component class
    auto &factory   = module->getFactory();
    auto classInfos = factory.classInfos();

    VST3::Hosting::ClassInfo componentClass;
    bool found = false;
    for (auto &ci : classInfos) {
        if (ci.category() == kVstAudioEffectClass) {
            componentClass = ci;
            found          = true;
            break;
        }
    }
    if (!found)
        return false;

    pluginName = componentClass.name();

    // Create IComponent
    component = factory.createInstance<Steinberg::Vst::IComponent>(
        componentClass.ID());
    if (!component)
        return false;
    if (component->initialize(hostApp) != Steinberg::kResultOk)
        return false;

    // QI for IAudioProcessor
    processor =
        Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(component);
    if (!processor)
        return false;

    // Obtain IEditController (try QI on component first, then create
    // separately)
    controller =
        Steinberg::FUnknownPtr<Steinberg::Vst::IEditController>(component);
    if (!controller) {
        Steinberg::TUID controllerCID;
        if (component->getControllerClassId(controllerCID) ==
            Steinberg::kResultOk) {
            auto ctrlUID = VST3::UID::fromTUID(controllerCID);
            controller =
                factory.createInstance<Steinberg::Vst::IEditController>(
                    ctrlUID);
            if (controller) {
                controller->initialize(hostApp);
                separateController = true;
            }
        }
    }

    // Connect component <-> controller so parameter changes flow
    if (controller) {
        auto compCP = Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint>(
            component);
        auto ctrlCP = Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint>(
            controller);
        if (compCP && ctrlCP) {
            compCP->connect(ctrlCP);
            ctrlCP->connect(compCP);
        }
    }

    // Activate audio output and event input buses
    activateBuses();

    // Setup processing
    Steinberg::Vst::ProcessSetup setup{
        Steinberg::Vst::kRealtime, Steinberg::Vst::kSample32,
        static_cast<Steinberg::int32>(blockSize), sampleRate};
    if (processor->setupProcessing(setup) != Steinberg::kResultOk)
        return false;

    // Prepare process data (allocates internal buffers)
    processData.prepare(*component, blockSize, Steinberg::Vst::kSample32);
    processData.inputEvents           = &eventList;
    processData.inputParameterChanges = &inputParamChanges;
    processData.processContext        = &processContext;

    processContext            = {};
    processContext.state      = Steinberg::Vst::ProcessContext::kPlaying |
                                Steinberg::Vst::ProcessContext::kTempoValid |
                                Steinberg::Vst::ProcessContext::kTimeSigValid;
    processContext.sampleRate = sampleRate;
    processContext.tempo      = 120.0;
    processContext.timeSigNumerator   = 4;
    processContext.timeSigDenominator = 4;

    inputParamChanges.setMaxParameters(512);

    if (component->setActive(true) != Steinberg::kResultOk)
        return false;
    processor->setProcessing(true);

    collectParams();

    processing = true;
    loaded     = true;
    return true;
}

void VstHost::Impl::activateBuses() {
    // Activate all event input buses
    auto eventInCount =
        component->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput);
    for (Steinberg::int32 i = 0; i < eventInCount; ++i)
        component->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kInput,
                               i, true);

    // Activate main audio output bus only
    auto audioOutCount = component->getBusCount(Steinberg::Vst::kAudio,
                                                Steinberg::Vst::kOutput);
    for (Steinberg::int32 i = 0; i < audioOutCount; ++i)
        component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput,
                               i, true);
}

void VstHost::Impl::collectParams() {
    params.clear();
    if (!controller)
        return;

    auto count = controller->getParameterCount();
    for (Steinberg::int32 i = 0; i < count; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        if (controller->getParameterInfo(i, info) != Steinberg::kResultOk)
            continue;
        if (info.flags & Steinberg::Vst::ParameterInfo::kIsHidden)
            continue;
        if (info.flags & Steinberg::Vst::ParameterInfo::kIsBypass)
            continue;

        VstParam p;
        p.id    = info.id;
        p.name  = Steinberg::Vst::StringConvert::convert(info.title);
        p.value = static_cast<float>(controller->getParamNormalized(info.id));
        params.push_back(std::move(p));
    }
}

void VstHost::Impl::doUnload() {
    if (!loaded)
        return;

    doCloseEditor();

    processing = false;
    loaded     = false;

    if (processor)
        processor->setProcessing(false);
    if (component)
        component->setActive(false);

    // Disconnect component <-> controller
    if (controller) {
        auto compCP = Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint>(
            component);
        auto ctrlCP = Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint>(
            controller);
        if (compCP && ctrlCP) {
            compCP->disconnect(ctrlCP);
            ctrlCP->disconnect(compCP);
        }
        if (separateController)
            controller->terminate();
        controller = nullptr;
    }

    if (component) {
        component->terminate();
        component = nullptr;
    }

    processor          = nullptr;
    module             = nullptr;
    separateController = false;
    params.clear();
    pluginName.clear();
    samplePos = 0;
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

static void registerPluginWindowClass() {
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = PluginWindowProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"GirthPluginWindow";
    RegisterClassExW(&wc);
}

bool VstHost::Impl::doOpenEditor() {
    if (!loaded || !controller)
        return false;
    if (editorOpen)
        return true;

    // Ask the controller for its editor view
    Steinberg::IPlugView *rawView =
        controller->createView(Steinberg::Vst::ViewType::kEditor);
    if (!rawView)
        return false;
    plugView = Steinberg::owned(rawView);

    if (plugView->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) !=
        Steinberg::kResultOk) {
        plugView = nullptr;
        return false;
    }

    registerPluginWindowClass();

    // Query initial size (may be zero before attach; that's fine)
    Steinberg::ViewRect rect{};
    plugView->getSize(&rect);
    int w = rect.getWidth() > 0 ? rect.getWidth() : 800;
    int h = rect.getHeight() > 0 ? rect.getHeight() : 600;

    // Compute window size including title bar and borders
    DWORD style = WS_OVERLAPPEDWINDOW &
                  ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    RECT winRect{0, 0, w, h};
    AdjustWindowRect(&winRect, style, FALSE);

    // UTF-8 -> UTF-16 for the title
    int titleLen =
        MultiByteToWideChar(CP_UTF8, 0, pluginName.c_str(), -1, nullptr, 0);
    std::wstring title(static_cast<size_t>(titleLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, pluginName.c_str(), -1, title.data(),
                        titleLen);

    pluginHwnd = CreateWindowExW(0, L"GirthPluginWindow", title.c_str(), style,
                                 CW_USEDEFAULT, CW_USEDEFAULT,
                                 winRect.right - winRect.left,
                                 winRect.bottom - winRect.top, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!pluginHwnd) {
        plugView = nullptr;
        return false;
    }

    // Store a heap-allocated callback so the window proc can close the editor
    // without needing to access the private VstHost::Impl type.
    auto *cb = new PluginWindowCloseCallback([this]() { doCloseEditor(); });
    SetWindowLongPtrW(pluginHwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cb));

    // Give the plugin a frame so it can request resizes
    plugFrame       = Steinberg::owned(new PlugFrame());
    plugFrame->hwnd = pluginHwnd;
    plugView->setFrame(plugFrame);

    // Attach plugin UI - the plugin may call resizeView during this call
    if (plugView->attached(reinterpret_cast<void *>(pluginHwnd),
                           Steinberg::kPlatformTypeHWND) !=
        Steinberg::kResultOk) {
        plugView->setFrame(nullptr);
        plugFrame = nullptr;
        plugView  = nullptr;
        DestroyWindow(pluginHwnd);
        pluginHwnd = nullptr;
        return false;
    }

    ShowWindow(pluginHwnd, SW_SHOW);
    UpdateWindow(pluginHwnd);
    editorOpen = true;
    return true;
}

void VstHost::Impl::doCloseEditor() {
    if (!editorOpen)
        return;
    editorOpen = false;

    if (plugView) {
        plugView->setFrame(nullptr);
        plugView->removed();
        plugView = nullptr;
    }
    plugFrame = nullptr;

    if (pluginHwnd) {
        // Clear USERDATA before DestroyWindow so WM_DESTROY can't double-free
        auto *cb = reinterpret_cast<PluginWindowCloseCallback *>(
            GetWindowLongPtrW(pluginHwnd, GWLP_USERDATA));
        SetWindowLongPtrW(pluginHwnd, GWLP_USERDATA, 0);
        delete cb;

        DestroyWindow(pluginHwnd);
        pluginHwnd = nullptr;
    }
}

// Win32 window procedure for the plugin container window
static LRESULT CALLBACK PluginWindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam) {
    if (msg == WM_CLOSE) {
        auto *cbPtr = reinterpret_cast<PluginWindowCloseCallback *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (cbPtr) {
            // Copy locally before calling: the callback deletes cbPtr and
            // destroys the window, so dereferencing cbPtr after the call is
            // UB.
            PluginWindowCloseCallback cb = *cbPtr;
            cb();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// VstHost public API
// ---------------------------------------------------------------------------

VstHost::VstHost() : impl_(std::make_unique<Impl>()) {
}
VstHost::~VstHost() {
    unload();
}

bool VstHost::load(const std::string &path, double sampleRate, int blockSize) {
    unload();
    return impl_->doLoad(path, sampleRate, blockSize);
}

void VstHost::unload() {
    impl_->doUnload();
}

void VstHost::process(float *outLeft, float *outRight, int frames) {
    if (!impl_->loaded || !impl_->processing) {
        std::fill(outLeft, outLeft + frames, 0.0f);
        std::fill(outRight, outRight + frames, 0.0f);
        return;
    }

    // Collect pending MIDI events
    {
        std::unique_lock<std::mutex> lk(impl_->eventMutex, std::try_to_lock);
        if (lk.owns_lock()) {
            impl_->swapEvents.clear();
            std::swap(impl_->pendingEvents, impl_->swapEvents);
        }
    }
    for (auto &e : impl_->swapEvents)
        impl_->eventList.addEvent(e);
    impl_->swapEvents.clear();

    // Transfer parameter changes from main thread
    impl_->paramTransfer.transferChangesTo(impl_->inputParamChanges);

    // Update process context
    impl_->processContext.projectTimeSamples   = impl_->samplePos;
    impl_->processContext.continousTimeSamples = impl_->samplePos;
    impl_->processData.numSamples = static_cast<Steinberg::int32>(frames);

    impl_->processor->process(impl_->processData);

    // Copy output to caller's buffers
    if (impl_->processData.numOutputs > 0) {
        auto &bus = impl_->processData.outputs[0];
        if (bus.numChannels >= 1 && bus.channelBuffers32[0])
            std::memcpy(outLeft, bus.channelBuffers32[0],
                        sizeof(float) * static_cast<size_t>(frames));
        if (bus.numChannels >= 2 && bus.channelBuffers32[1])
            std::memcpy(outRight, bus.channelBuffers32[1],
                        sizeof(float) * static_cast<size_t>(frames));
        if (bus.numChannels < 2)
            std::memcpy(outRight, outLeft,
                        sizeof(float) * static_cast<size_t>(frames));
    } else {
        std::fill(outLeft, outLeft + frames, 0.0f);
        std::fill(outRight, outRight + frames, 0.0f);
    }

    impl_->eventList.clear();
    impl_->inputParamChanges.clearQueue();
    impl_->samplePos += frames;
}

void VstHost::noteOn(int pitch, float velocity, int channel) {
    Steinberg::Vst::Event e{};
    e.type            = Steinberg::Vst::Event::kNoteOnEvent;
    e.sampleOffset    = 0;
    e.ppqPosition     = 0.0;
    e.flags           = Steinberg::Vst::Event::kIsLive;
    e.noteOn.channel  = static_cast<Steinberg::int16>(channel);
    e.noteOn.pitch    = static_cast<Steinberg::int16>(pitch);
    e.noteOn.velocity = velocity;
    e.noteOn.length   = 0;
    e.noteOn.tuning   = 0.0f;
    e.noteOn.noteId   = -1;

    std::lock_guard<std::mutex> lk(impl_->eventMutex);
    impl_->pendingEvents.push_back(e);
}

void VstHost::noteOff(int pitch, int channel) {
    Steinberg::Vst::Event e{};
    e.type             = Steinberg::Vst::Event::kNoteOffEvent;
    e.sampleOffset     = 0;
    e.ppqPosition      = 0.0;
    e.flags            = Steinberg::Vst::Event::kIsLive;
    e.noteOff.channel  = static_cast<Steinberg::int16>(channel);
    e.noteOff.pitch    = static_cast<Steinberg::int16>(pitch);
    e.noteOff.velocity = 0.0f;
    e.noteOff.tuning   = 0.0f;
    e.noteOff.noteId   = -1;

    std::lock_guard<std::mutex> lk(impl_->eventMutex);
    impl_->pendingEvents.push_back(e);
}

bool VstHost::openEditor() {
    return impl_->doOpenEditor();
}
void VstHost::closeEditor() {
    impl_->doCloseEditor();
}
bool VstHost::isEditorOpen() const {
    return impl_->editorOpen;
}

bool VstHost::isLoaded() const {
    return impl_->loaded;
}

const std::string &VstHost::getPluginName() const {
    return impl_->pluginName;
}

std::vector<VstParam> VstHost::getParams() const {
    return impl_->params;
}

void VstHost::setParam(uint32_t paramId, float normalizedValue) {
    impl_->paramTransfer.addChange(
        static_cast<Steinberg::Vst::ParamID>(paramId),
        static_cast<Steinberg::Vst::ParamValue>(normalizedValue), 0);

    // Also notify the controller so the UI stays in sync
    if (impl_->controller)
        impl_->controller->setParamNormalized(
            static_cast<Steinberg::Vst::ParamID>(paramId),
            static_cast<Steinberg::Vst::ParamValue>(normalizedValue));

    // Update cached value
    for (auto &p : impl_->params) {
        if (p.id == paramId) {
            p.value = normalizedValue;
            break;
        }
    }
}
