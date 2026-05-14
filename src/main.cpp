#include <SDL3/SDL.h>
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
}
