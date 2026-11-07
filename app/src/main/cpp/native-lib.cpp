#include <jni.h>
#include <unistd.h>
#include <cstdint>

extern "C" JNIEXPORT jstring JNICALL
Java_id_granturismo4_nativeapp_MainActivity_nativeVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("GT4 Native Core 0.1 — ARM64");
}

extern "C" JNIEXPORT jstring JNICALL
Java_id_granturismo4_nativeapp_MainActivity_inspectIsoHeader(JNIEnv* env, jobject, jint fd) {
    // Read-only ISO-9660 identification probe; not a PS2 game runtime.
    uint8_t sector[2048] = {};
    const off_t offsets[] = {16 * 2048, 16 * 2048 + 1};
    for (off_t off : offsets) {
        if (pread(fd, sector, sizeof(sector), off) == sizeof(sector)) {
            if (sector[1]=='C' && sector[2]=='D' && sector[3]=='0' &&
                sector[4]=='0' && sector[5]=='1') {
                return env->NewStringUTF("ISO-9660 signature detected. Disc contents can be analyzed next.");
            }
        }
    }
    return env->NewStringUTF("No ISO-9660 PVD signature at expected sector. Image may use another layout or be unsupported.");
}
