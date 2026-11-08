#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>

static int g_discFd = -1;
static std::string g_discName = "No ISO selected";

extern "C" JNIEXPORT void JNICALL
Java_id_granturismo4_nativeapp_NativeGameActivity_nativeSetDisc(
    JNIEnv* env, jclass, jint ownedFd, jstring displayName) {
    if (g_discFd >= 0) close(g_discFd);
    g_discFd = ownedFd; // Ownership was transferred by ParcelFileDescriptor.detachFd().
    const char* chars = displayName ? env->GetStringUTFChars(displayName, nullptr) : nullptr;
    g_discName = chars ? chars : "Selected disc image";
    if (chars) env->ReleaseStringUTFChars(displayName, chars);
}

struct RuntimeState {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool touchPressed = false;
    float touchX = 0.5f;
    float touchY = 0.5f;
    std::string discStatus = "No disc image passed from Android";
};

static std::string inspectDiscDescriptor(int fd) {
    if (fd < 0) return "NO ISO: choose a disc image in the launcher";
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) return "ISO ERROR: descriptor is not readable";
    uint8_t pvd[2048] {};
    const ssize_t count = pread(fd, pvd, sizeof(pvd), 16 * 2048);
    char details[160];
    std::snprintf(details, sizeof(details), "%s | %.2f GiB",
                  g_discName.c_str(), double(st.st_size) / (1024.0 * 1024.0 * 1024.0));
    if (count != 2048) return std::string(details) + " | cannot read sector 16";
    if (pvd[0] == 1 && std::memcmp(pvd + 1, "CD001", 5) == 0)
        return std::string(details) + " | ISO-9660 PVD detected";
    return std::string(details) + " | ISO-9660 PVD not found (UDF/other format?)";
}

SDL_AppResult SDL_AppInit(void** appstate, int, char**) {
    auto* state = new RuntimeState();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("GT4 Native: SDL_Init failed: %s", SDL_GetError());
        delete state;
        return SDL_APP_FAILURE;
    }
    state->window = SDL_CreateWindow("Gran Turismo 4 Native", 960, 540, SDL_WINDOW_RESIZABLE);
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
    state->discStatus = inspectDiscDescriptor(g_discFd);
    SDL_Log("GT4 Native runtime ready: %s", state->discStatus.c_str());
    SDL_Log("This is an ISO descriptor handoff test, not GT4 gameplay.");
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

    SDL_FRect panel{width * 0.06f, height * 0.08f, width * 0.88f, height * 0.84f};
    SDL_SetRenderDrawColor(state->renderer, 25, 39, 56, 255);
    SDL_RenderFillRect(state->renderer, &panel);
    SDL_FRect accent{width * 0.10f, height * 0.15f, width * 0.80f, height * 0.025f};
    SDL_SetRenderDrawColor(state->renderer, 205, 35, 48, 255);
    SDL_RenderFillRect(state->renderer, &accent);

    SDL_SetRenderDrawColor(state->renderer, 235, 240, 248, 255);
    SDL_RenderDebugText(state->renderer, width * 0.11f, height * 0.25f, "GRAN TURISMO 4 - NATIVE PORT");
    SDL_SetRenderDrawColor(state->renderer, 125, 205, 160, 255);
    SDL_RenderDebugText(state->renderer, width * 0.11f, height * 0.34f, "SDL3 RUNTIME: READY");
    SDL_SetRenderDrawColor(state->renderer, 220, 220, 220, 255);
    SDL_RenderDebugText(state->renderer, width * 0.11f, height * 0.43f, state->discStatus.c_str());
    SDL_RenderDebugText(state->renderer, width * 0.11f, height * 0.52f, "ISO FD HANDOFF: NO FULL IMAGE COPY");
    SDL_RenderDebugText(state->renderer, width * 0.11f, height * 0.59f, "GAME ENGINE: NOT IMPLEMENTED YET");

    SDL_FRect left{width * 0.15f, height * 0.70f, width * 0.18f, height * 0.13f};
    SDL_FRect right{width * 0.67f, height * 0.70f, width * 0.18f, height * 0.13f};
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
    if (state) {
        if (state->renderer) SDL_DestroyRenderer(state->renderer);
        if (state->window) SDL_DestroyWindow(state->window);
        delete state;
    }
    if (g_discFd >= 0) {
        close(g_discFd);
        g_discFd = -1;
    }
    SDL_Quit();
}
