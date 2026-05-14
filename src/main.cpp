#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include <portaudio.h>
#include "vst/vst_host.hpp"
#include "ui/piano_roll.hpp"
#include <cstring>
#include <print>

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
struct AppAudio {
    VstHost *host{nullptr};
    float left[2048]{};
    float right[2048]{};
};

static int paCallback(const void * /*in*/, void *out, unsigned long frames,
                      const PaStreamCallbackTimeInfo * /*ti*/,
                      PaStreamCallbackFlags /*fl*/, void *userData) {
    auto *app  = static_cast<AppAudio *>(userData);
    float *buf = static_cast<float *>(out);
    app->host->process(app->left, app->right, static_cast<int>(frames));
    for (unsigned long i = 0; i < frames; ++i) {
        *buf++ = app->left[i];
        *buf++ = app->right[i];
    }
    return paContinue;
}

// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println("SDL init failed");
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window *window = SDL_CreateWindow(
        "Girth", 1100, 520, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::println("Window creation failed");
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    VstHost vstHost;
    PianoRoll pianoRoll;

    AppAudio audio;
    audio.host = &vstHost;

    Pa_Initialize();
    PaStream *stream = nullptr;
    Pa_OpenDefaultStream(&stream, 0, 2, paFloat32, 44100, 256, paCallback,
                         &audio);
    Pa_StartStream(stream);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Tweak style for a dark DAW feel
    ImGuiStyle &s               = ImGui::GetStyle();
    s.WindowRounding            = 0.0f;
    s.FrameRounding             = 3.0f;
    s.ItemSpacing               = {6, 4};
    s.Colors[ImGuiCol_WindowBg] = {0.10f, 0.10f, 0.11f, 1.0f};

    static char pluginPath[512] =
        "C:\\Program Files\\Common Files\\VST3\\Serum2.vst3";
    static std::string loadError;

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
        }

        double nowMs = static_cast<double>(SDL_GetTicks());

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##main", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoScrollbar);

        // ── Top bar: plugin + transport ───────────────────────────────────
        // Plugin path + load/unload
        ImGui::SetNextItemWidth(io.DisplaySize.x - 430.0f);
        ImGui::InputText("##path", pluginPath, sizeof(pluginPath));
        ImGui::SameLine();

        if (ImGui::Button("Load")) {
            loadError.clear();
            vstHost.closeEditor();
            if (!vstHost.load(pluginPath, 44100.0, 256))
                loadError = "Load failed.";
            else
                vstHost.openEditor();
        }
        ImGui::SameLine();
        if (ImGui::Button("Unload")) {
            vstHost.closeEditor();
            vstHost.unload();
            loadError.clear();
        }

        ImGui::SameLine(0, 12);

        // Plugin status
        if (!loadError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, {1, 0.3f, 0.3f, 1});
            ImGui::TextUnformatted(loadError.c_str());
            ImGui::PopStyleColor();
        } else if (vstHost.isLoaded()) {
            ImGui::TextColored({0.4f, 1, 0.4f, 1}, "%s",
                               vstHost.getPluginName().c_str());
            ImGui::SameLine();
            if (vstHost.isEditorOpen()) {
                if (ImGui::SmallButton("Hide UI"))
                    vstHost.closeEditor();
            } else {
                if (ImGui::SmallButton("Show UI"))
                    vstHost.openEditor();
            }
        } else {
            ImGui::TextDisabled("No plugin");
        }

        ImGui::SameLine(0, 20);
        ImGui::Separator();
        ImGui::SameLine(0, 10);

        // Transport
        bool playing = pianoRoll.isPlaying();
        if (playing) {
            if (ImGui::Button("  ||  "))
                pianoRoll.stop(&vstHost);
        } else {
            if (ImGui::Button("  >   "))
                pianoRoll.play();
        }
        ImGui::SameLine();
        if (ImGui::Button("<<")) {
            pianoRoll.stop(&vstHost);
            pianoRoll.setPlayheadBeat(0.0);
        }
        ImGui::SameLine();

        // BPM
        ImGui::SetNextItemWidth(70);
        float bpmF = static_cast<float>(pianoRoll.bpm);
        if (ImGui::DragFloat("BPM", &bpmF, 0.5f, 20.0f, 300.0f, "%.1f"))
            pianoRoll.bpm = static_cast<double>(bpmF);
        ImGui::SameLine();

        // Loop end
        ImGui::SetNextItemWidth(60);
        ImGui::DragFloat("Loop", &pianoRoll.loopEnd, 0.25f, 1.0f, 128.0f,
                         "%.0f");
        ImGui::SameLine();
        ImGui::Checkbox("##loop", &pianoRoll.looping);

        ImGui::SameLine(0, 12);
        ImGui::TextDisabled(
            "LMB=place  RMB=delete  MMB/drag=pan  Ctrl+scroll=hzoom  "
            "Shift+scroll=hscroll  Alt+scroll=vzoom");

        ImGui::Separator();

        // ── Piano roll fills remaining space ─────────────────────────────
        ImVec2 avail = ImGui::GetContentRegionAvail();
        pianoRoll.draw(avail, vstHost, nowMs);

        ImGui::End();

        ImGui::Render();
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    pianoRoll.stop(&vstHost);
    vstHost.closeEditor();
    vstHost.unload();
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
