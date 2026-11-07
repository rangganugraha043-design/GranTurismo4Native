#include <jni.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static uint32_t readLe32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

extern "C" JNIEXPORT jstring JNICALL
Java_id_granturismo4_nativeapp_MainActivity_nativeVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("GT4 Native Core 0.3 — ISO-9660 directory reader");
}

extern "C" JNIEXPORT jstring JNICALL
Java_id_granturismo4_nativeapp_MainActivity_inspectIsoHeader(JNIEnv* env, jobject, jint fd) {
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0)
        return env->NewStringUTF("Tidak dapat membaca ukuran file.");

    constexpr uint64_t sectorSize = 2048;
    const uint64_t fileSize = static_cast<uint64_t>(st.st_size);
    uint8_t sector[2048] {};
    uint8_t pvd[2048] {};
    bool iso = false, udf = false, pvdFound = false;

    for (int n = 16; n <= 31; ++n) {
        const uint64_t offset = uint64_t(n) * sectorSize;
        if (offset + sectorSize > fileSize) break;
        if (pread(fd, sector, sizeof(sector), static_cast<off_t>(offset)) != 2048) continue;
        if (std::memcmp(sector + 1, "CD001", 5) == 0) {
            iso = true;
            if (sector[0] == 1) {
                std::memcpy(pvd, sector, sizeof(pvd));
                pvdFound = true;
            }
        }
        if (std::memcmp(sector + 1, "BEA01", 5) == 0 ||
            std::memcmp(sector + 1, "NSR02", 5) == 0 ||
            std::memcmp(sector + 1, "NSR03", 5) == 0 ||
            std::memcmp(sector + 1, "TEA01", 5) == 0) udf = true;
    }

    char report[2048] {};
    const double mib = double(fileSize) / (1024.0 * 1024.0);
    std::snprintf(report, sizeof(report),
        "Ukuran: %.1f MiB\nFormat: %s\n",
        mib, (iso && udf) ? "ISO-9660 + UDF" : iso ? "ISO-9660" : udf ? "UDF" : "Belum dikenali");

    if (!pvdFound) {
        std::strncat(report, "Primary Volume Descriptor ISO-9660 tidak ditemukan. Direktori belum dapat dibaca.", sizeof(report)-std::strlen(report)-1);
        return env->NewStringUTF(report);
    }

    const uint8_t* root = pvd + 156;
    if (root[0] < 34) {
        std::strncat(report, "Record direktori root tidak valid.", sizeof(report)-std::strlen(report)-1);
        return env->NewStringUTF(report);
    }

    const uint64_t rootLba = readLe32(root + 2);
    const uint64_t rootLength = readLe32(root + 10);
    const uint64_t rootOffset = rootLba * sectorSize;
    if (rootLength == 0 || rootLength > 4u * 1024u * 1024u ||
        rootOffset > fileSize || rootLength > fileSize - rootOffset) {
        std::strncat(report, "Rentang direktori root tidak valid.", sizeof(report)-std::strlen(report)-1);
        return env->NewStringUTF(report);
    }

    std::vector<uint8_t> data(static_cast<size_t>(rootLength));
    if (pread(fd, data.data(), data.size(), static_cast<off_t>(rootOffset)) != static_cast<ssize_t>(data.size())) {
        std::strncat(report, "Gagal membaca direktori root.", sizeof(report)-std::strlen(report)-1);
        return env->NewStringUTF(report);
    }

    std::strncat(report, "Isi direktori utama (maks. 16 entri):\n", sizeof(report)-std::strlen(report)-1);
    size_t pos = 0;
    int count = 0;
    while (pos < data.size() && count < 16) {
        const uint8_t len = data[pos];
        if (len == 0) {
            pos = ((pos / sectorSize) + 1) * sectorSize;
            continue;
        }
        if (len < 34 || pos + len > data.size()) break;
        const uint8_t* rec = data.data() + pos;
        const uint8_t nameLen = rec[32];
        if (nameLen > 0 && size_t(33 + nameLen) <= len) {
            const uint8_t* name = rec + 33;
            const bool dotEntry = nameLen == 1 && (name[0] == 0 || name[0] == 1);
            if (!dotEntry) {
                std::string filename(reinterpret_cast<const char*>(name), nameLen);
                const size_t version = filename.find(';');
                if (version != std::string::npos) filename.resize(version);
                while (!filename.empty() && filename.back() == '.') filename.pop_back();
                if (!filename.empty()) {
                    const char* kind = (rec[25] & 0x02) ? "[DIR] " : "[FILE] ";
                    std::string line = std::string(kind) + filename + "\n";
                    std::strncat(report, line.c_str(), sizeof(report)-std::strlen(report)-1);
                    ++count;
                }
            }
        }
        pos += len;
    }
    if (count == 0)
        std::strncat(report, "(tidak ada entri yang terbaca)\n", sizeof(report)-std::strlen(report)-1);
    std::strncat(report, "Pemeriksaan read-only. Game belum dijalankan.", sizeof(report)-std::strlen(report)-1);
    return env->NewStringUTF(report);
}
