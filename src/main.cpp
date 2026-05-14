#include <SDL3/SDL.h>
<<<<<<< HEAD
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include <portaudio.h>
#include <cmath>
#include <vector>
#include <print>

// --- Audio Logic ---
struct AudioData {
    float phase = 0.0f;
    float frequency = 440.0f;
    bool playing = false;
};

// PortAudio Callback: Generates a simple sine wave
static int paCallback(const void* inputBuffer, void* outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void* userData) {
    (void)inputBuffer;
    (void)timeInfo;
    (void)statusFlags;

    AudioData* data = (AudioData*)userData;
    float* out = (float*)outputBuffer;

    for (unsigned int i = 0; i < framesPerBuffer; i++) {
        if (data->playing) {
            *out++ = 0.2f * sinf(data->phase); // Left channel
            *out++ = 0.2f * sinf(data->phase); // Right channel
            data->phase += (2.0f * 3.14159f * data->frequency) / 44100.0f;
            if (data->phase > 2.0f * 3.14159f) data->phase -= 2.0f * 3.14159f;
        } else {
            *out++ = 0.0f;
            *out++ = 0.0f;
        }
    }
    return paContinue;
=======
#include <iostream>

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cout << "Failed to initialize SDL\n";
        std::exit(1);
    }

    SDL_Window* sdlWindow;
    SDL_Renderer* sdlRenderer;
    bool shouldQuit = false;

    SDL_CreateWindowAndRenderer("Girth", 800, 600, SDL_WINDOW_RESIZABLE, &sdlWindow, &sdlRenderer);

    while (!shouldQuit) {
        SDL_Event e;

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT: {
                    shouldQuit = true;
                } break;
            }
        }

        SDL_SetRenderDrawColor(sdlRenderer, 10, 10, 10, 255);
        SDL_RenderClear(sdlRenderer);

        SDL_RenderPresent(sdlRenderer);

        // This avoids the CPU overusage.
        // This will be discarded once we implement drawing by event and not by loop.
        SDL_Delay(1000 / 60);
    }

    SDL_Quit();
>>>>>>> 7695fcad269805f0f57a6428f082c6bbfd5f6608
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::println("Initializing Girth...");
    SDL_Log("hi\n");
    printf("hii\n");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println("Failed to init SDL3.");
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow("Girth", 1280, 720, SDL_WINDOW_OPENGL);
    if (!window) {
        std::println("Failed to create SDL3 window.");
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);

    // 3. Initialize PortAudio
    AudioData audioData;
    Pa_Initialize();
    PaStream* stream;
    Pa_OpenDefaultStream(&stream, 0, 2, paFloat32, 44100, 256, paCallback, &audioData);
    Pa_StartStream(stream);

    // 4. Setup Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Main Loop
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) done = true;
        }

        // Start Frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // UI Window
        ImGui::Begin("Audio Control");
        ImGui::Checkbox("Play Sine Wave", &audioData.playing);
        ImGui::SliderFloat("Frequency", &audioData.frequency, 100.0f, 1000.0f);
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::End();

        // Rendering
        ImGui::Render();
        glViewport(0, 0, 1280, 720);
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}