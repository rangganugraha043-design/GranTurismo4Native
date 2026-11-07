#include <jni.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_id_granturismo4_nativeapp_MainActivity_nativeVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("GT4 Native Core 0.2 — ARM64 ISO inspector");
}

extern "C" JNIEXPORT jstring JNICALL
Java_id_granturismo4_nativeapp_MainActivity_inspectIsoHeader(JNIEnv* env, jobject, jint fd) {
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        return env->NewStringUTF("Tidak dapat membaca ukuran file.");
    }

    constexpr off_t kSector = 2048;
    const off_t size = st.st_size;
    const double mib = static_cast<double>(size) / (1024.0 * 1024.0);
    bool iso9660 = false;
    bool udf = false;
    bool hasPvd = false;

    // Scan volume descriptor sectors 16–31 (2048-byte logical sectors).
    uint8_t sector[kSector] {};
    for (int n = 16; n <= 31; ++n) {
        const off_t offset = static_cast<off_t>(n) * kSector;
        if (offset + kSector > size) break;
        const ssize_t got = pread(fd, sector, sizeof(sector), offset);
        if (got != static_cast<ssize_t>(sizeof(sector))) continue;

        // ISO-9660 Volume Descriptor: type byte followed by "CD001".
        if (std::memcmp(sector + 1, "CD001", 5) == 0) {
            iso9660 = true;
            if (sector[0] == 1) hasPvd = true;
        }

        // UDF recognition sequence identifiers in Volume Recognition Sequence.
        if (std::memcmp(sector + 1, "BEA01", 5) == 0 ||
            std::memcmp(sector + 1, "NSR02", 5) == 0 ||
            std::memcmp(sector + 1, "NSR03", 5) == 0 ||
            std::memcmp(sector + 1, "TEA01", 5) == 0) {
            udf = true;
        }
    }

    char report[512];
    std::snprintf(report, sizeof(report),
        "Ukuran: %.1f MiB\nFormat: %s\n%s\nPemeriksaan hanya-baca selesai. Belum menjalankan game.",
        mib,
        (iso9660 && udf) ? "ISO-9660 + UDF (hybrid)" :
        iso9660 ? "ISO-9660 terdeteksi" :
        udf ? "UDF terdeteksi" : "Format ISO/UDF belum dikenali",
        hasPvd ? "Primary Volume Descriptor ditemukan." :
        "Primary Volume Descriptor ISO-9660 tidak ditemukan.");
    return env->NewStringUTF(report);
}
