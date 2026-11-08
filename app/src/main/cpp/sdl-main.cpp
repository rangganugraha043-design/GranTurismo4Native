#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

struct RuntimeState {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool touchPressed = false;
    float touchX = 0.5f;
    float touchY = 0.5f;
};

SDL_AppResult SDL_AppInit(void** appstate, int, char**) {
    auto* state = new RuntimeState();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("GT4 Native: SDL_Init failed: %s", SDL_GetError());
        delete state;
        return SDL_APP_FAILURE;
    }
    state->window = SDL_CreateWindow("Gran Turismo 4 Native - SDL3 Runtime",
                                     960, 540, SDL_WINDOW_RESIZABLE);
    if (!state->window) {
        SDL_Log("GT4 Native: window creation failed: %s", SDL_GetError());
        SDL_Quit();
        delete state;
        return SDL_APP_FAILURE;
    }
    state->renderer = SDL_CreateRenderer(state->window, nullptr);
    if (!state->renderer) {
        SDL_Log("GT4 Native: renderer creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(state->window);
        SDL_Quit();
        delete state;
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(state->renderer, 1);
    SDL_Log("GT4 Native: SDL3 runtime initialized; test screen only, no GT4 gameplay yet.");
    *appstate = state;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* state = static_cast<RuntimeState*>(appstate);
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_FINGER_DOWN || event->type == SDL_EVENT_FINGER_MOTION) {
        state->touchPressed = true;
        state->touchX = event->tfinger.x;
        state->touchY = event->tfinger.y;
    } else if (event->type == SDL_EVENT_FINGER_UP) {
        state->touchPressed = false;
        state->touchX = event->tfinger.x;
        state->touchY = event->tfinger.y;
    } else if (event->type == SDL_EVENT_GAMEPAD_ADDED) {
        SDL_Log("GT4 Native: controller connected.");
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* state = static_cast<RuntimeState*>(appstate);
    int width = 0, height = 0;
    SDL_GetRenderOutputSize(state->renderer, &width, &height);
    SDL_SetRenderDrawColor(state->renderer, 10, 15, 24, 255);
    SDL_RenderClear(state->renderer);

    SDL_FRect panel{width * 0.08f, height * 0.12f, width * 0.84f, height * 0.76f};
    SDL_SetRenderDrawColor(state->renderer, 25, 39, 56, 255);
    SDL_RenderFillRect(state->renderer, &panel);

    SDL_FRect accent{width * 0.12f, height * 0.18f, width * 0.76f, height * 0.035f};
    SDL_SetRenderDrawColor(state->renderer, 205, 35, 48, 255);
    SDL_RenderFillRect(state->renderer, &accent);

    SDL_FRect left{width * 0.15f, height * 0.62f, width * 0.18f, height * 0.18f};
    SDL_FRect right{width * 0.67f, height * 0.62f, width * 0.18f, height * 0.18f};
    SDL_SetRenderDrawColor(state->renderer, 70, 92, 116, 255);
    SDL_RenderFillRect(state->renderer, &left);
    SDL_RenderFillRect(state->renderer, &right);

    if (state->touchPressed) {
        SDL_FRect marker{state->touchX * width - 18.0f, state->touchY * height - 18.0f, 36.0f, 36.0f};
        SDL_SetRenderDrawColor(state->renderer, 235, 190, 65, 255);
        SDL_RenderFillRect(state->renderer, &marker);
    }
    SDL_RenderPresent(state->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    auto* state = static_cast<RuntimeState*>(appstate);
    if (!state) return;
    if (state->renderer) SDL_DestroyRenderer(state->renderer);
    if (state->window) SDL_DestroyWindow(state->window);
    SDL_Quit();
    delete state;
}
