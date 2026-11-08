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
#include <vector>

static int g_discFd = -1;
static std::string g_discName = "No ISO selected";

extern "C" int gt4_inspect_rofs_fd(int fd, uint64_t volumeOffset,
                                    uint64_t volumeBytes, char* output,
                                    size_t outputCapacity);


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

static uint32_t readLe32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

struct DiscFile {
    std::string name;
    uint64_t offset;
    uint64_t size;
    bool directory;
};

static void scanDiscDirectory(int fd, uint64_t imageBytes, uint64_t lba,
                              uint64_t bytes, unsigned depth,
                              std::vector<uint64_t>& visited,
                              std::vector<DiscFile>& files) {
    constexpr uint64_t sectorBytes = 2048;
    constexpr uint64_t maxDirBytes = 2u * 1024u * 1024u;
    if (depth > 6 || bytes == 0 || bytes > maxDirBytes ||
        files.size() >= 2048 || lba > UINT64_MAX / sectorBytes) return;
    for (uint64_t previous : visited) if (previous == lba) return;
    const uint64_t offset = lba * sectorBytes;
    if (offset > imageBytes || bytes > imageBytes - offset) return;
    visited.push_back(lba);
    std::vector<uint8_t> data(static_cast<size_t>(bytes));
    if (pread(fd, data.data(), data.size(), static_cast<off_t>(offset)) !=
        static_cast<ssize_t>(data.size())) return;
    std::vector<DiscFile> children;
    size_t pos = 0;
    while (pos < data.size() && files.size() + children.size() < 2048) {
        const uint8_t recordBytes = data[pos];
        if (!recordBytes) {
            pos = ((pos / sectorBytes) + 1) * sectorBytes;
            continue;
        }
        if (recordBytes < 34 || pos + recordBytes > data.size()) break;
        const uint8_t* rec = data.data() + pos;
        const uint8_t nameBytes = rec[32];
        if (nameBytes && size_t(33 + nameBytes) <= recordBytes) {
            const bool dot = nameBytes == 1 && (rec[33] == 0 || rec[33] == 1);
            if (!dot) {
                std::string name(reinterpret_cast<const char*>(rec + 33), nameBytes);
                const size_t version = name.find(';');
                if (version != std::string::npos) name.resize(version);
                while (!name.empty() && name.back() == '.') name.pop_back();
                if (!name.empty()) {
                    const uint64_t extent = readLe32(rec + 2);
                    if (extent <= UINT64_MAX / sectorBytes) {
                        children.push_back({name, extent * sectorBytes,
                                            readLe32(rec + 10), (rec[25] & 2) != 0});
                    }
                }
            }
        }
        pos += recordBytes;
    }
    for (const auto& item : children) files.push_back(item);
    for (const auto& item : children) {
        if (item.directory && depth < 6)
            scanDiscDirectory(fd, imageBytes, item.offset / sectorBytes, item.size,
                              depth + 1, visited, files);
    }
}

static bool discNameIs(const std::string& value, const char* expected) {
    if (value.size() != std::strlen(expected)) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        char a = value[i], b = expected[i];
        if (a >= 'a' && a <= 'z') a -= ('a' - 'A');
        if (b >= 'a' && b <= 'z') b -= ('a' - 'A');
        if (a != b) return false;
    }
    return true;
}

static std::string inspectDiscDescriptor(int fd) {
    if (fd < 0) return "NO ISO: choose a disc image in the launcher";
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) return "ISO ERROR: descriptor is not readable";
    uint8_t pvd[2048] {};
    const ssize_t count = pread(fd, pvd, sizeof(pvd), 16 * 2048);
    char details[180];
    std::snprintf(details, sizeof(details), "%s | %.2f GiB",
                  g_discName.c_str(), double(st.st_size) / (1024.0 * 1024.0 * 1024.0));
    if (count != 2048 || pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0)
        return std::string(details) + " | ISO-9660 PVD not found (UDF-only not supported)";
    const uint8_t* root = pvd + 156;
    if (root[0] < 34) return std::string(details) + " | invalid root directory record";
    std::vector<uint64_t> visited;
    std::vector<DiscFile> files;
    scanDiscDirectory(fd, static_cast<uint64_t>(st.st_size), readLe32(root + 2),
                      readLe32(root + 10), 0, visited, files);
    const DiscFile* core = nullptr;
    const DiscFile* volume = nullptr;
    for (const auto& file : files) {
        if (file.directory) continue;
        if (discNameIs(file.name, "CORE.GT4")) core = &file;
        if (discNameIs(file.name, "GT4.VOL")) volume = &file;
    }
    char summary[256];
    std::snprintf(summary, sizeof(summary),
        " | %zu ISO entries, %zu dirs | CORE.GT4: %s | GT4.VOL: %s",
        files.size(), visited.size(), core ? "found" : "not found",
        volume ? "found" : "not found");
    std::string result = std::string(details) + summary;
    if (volume) {
        char rofsReport[4096]{};
        const int valid = gt4_inspect_rofs_fd(fd, volume->offset, volume->size,
                                               rofsReport, sizeof(rofsReport));
        SDL_Log("GT4.VOL RoFS inspection (%s):\n%s",
                valid ? "valid" : "not validated", rofsReport);
        const char* firstLineEnd = std::strchr(rofsReport, '\n');
        if (firstLineEnd) {
            result += valid ? " | RoFS TOC parsed" : " | RoFS TOC not validated";
            result += " | ";
            result.append(rofsReport, static_cast<size_t>(firstLineEnd - rofsReport));
        } else if (rofsReport[0]) {
            result += " | ";
            result += rofsReport;
        }
    }
    return result;
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
