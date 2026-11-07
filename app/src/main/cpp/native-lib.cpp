#include <jni.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

static bool inspectCore(int fd, uint64_t offset, uint64_t fileBytes, std::string& report) {
    // Retail CORE.GT4 has a 6-byte wrapper followed by a raw DEFLATE stream.
    if (fileBytes < 7 || fileBytes > 16u * 1024u * 1024u) {
        report += "CORE.GT4: ukuran file di luar batas pemeriksaan aman.\\n";
        return false;
    }
    std::vector<uint8_t> packed(static_cast<size_t>(fileBytes));
    if (pread(fd, packed.data(), packed.size(), static_cast<off_t>(offset)) !=
        static_cast<ssize_t>(packed.size())) {
        report += "CORE.GT4: gagal membaca data executable.\\n";
        return false;
    }
    const uint16_t flags = uint16_t(packed[0]) | (uint16_t(packed[1]) << 8);
    const uint32_t expectedSize = readLe32(packed.data() + 2);
    if (expectedSize == 0 || expectedSize > 32u * 1024u * 1024u) {
        report += "CORE.GT4: ukuran hasil dekompresi tidak valid.\\n";
        return false;
    }

    std::vector<uint8_t> unpacked(static_cast<size_t>(expectedSize));
    z_stream stream {};
    stream.next_in = packed.data() + 6;
    stream.avail_in = static_cast<uInt>(packed.size() - 6);
    stream.next_out = unpacked.data();
    stream.avail_out = static_cast<uInt>(unpacked.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        report += "CORE.GT4: zlib gagal diinisialisasi.\\n";
        return false;
    }
    const int result = inflate(&stream, Z_FINISH);
    const uint64_t produced = stream.total_out;
    inflateEnd(&stream);

    char detail[192];
    std::snprintf(detail, sizeof(detail),
        "CORE.GT4: flags=0x%04X, target=%u byte, hasil=%llu byte.\\n",
        flags, expectedSize, static_cast<unsigned long long>(produced));
    report += detail;
    if (result != Z_STREAM_END || produced != expectedSize) {
        report += "CORE.GT4: dekompresi belum valid/utuh; executable tidak akan dijalankan.\\n";
        return false;
    }
    report += "CORE.GT4: raw DEFLATE berhasil diperiksa. Ini baru validasi executable, belum menjalankan mesin PS2.\\n";
    return true;
}

static uint32_t readLe32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

extern "C" JNIEXPORT jstring JNICALL
Java_id_granturismo4_nativeapp_MainActivity_nativeVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("GT4 Native Core 0.4 — disc content scanner");
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

    // Read SYSTEM.CNF to discover the disc's BOOT2 executable path.
    std::string bootPath;
    size_t scan = 0;
    while (scan < data.size()) {
        const uint8_t recLen = data[scan];
        if (recLen == 0) { scan = ((scan / sectorSize) + 1) * sectorSize; continue; }
        if (recLen < 34 || scan + recLen > data.size()) break;
        const uint8_t* rec = data.data() + scan;
        const uint8_t nl = rec[32];
        if (nl > 0 && size_t(33 + nl) <= recLen) {
            std::string nm(reinterpret_cast<const char*>(rec + 33), nl);
            const size_t ver = nm.find(';'); if (ver != std::string::npos) nm.resize(ver);
            if (nm == "SYSTEM.CNF" && !(rec[25] & 0x02)) {
                const uint64_t extent = uint64_t(readLe32(rec + 2)) * sectorSize;
                const uint64_t bytes = readLe32(rec + 10);
                if (bytes > 0 && bytes <= 4096 && extent <= fileSize && bytes <= fileSize - extent) {
                    std::vector<char> cnf(static_cast<size_t>(bytes) + 1, 0);
                    if (pread(fd, cnf.data(), static_cast<size_t>(bytes), static_cast<off_t>(extent)) == static_cast<ssize_t>(bytes)) {
                        std::string cfg(cnf.data());
                        size_t p = cfg.find("BOOT2");
                        if (p != std::string::npos) {
                            p = cfg.find('=', p);
                            if (p != std::string::npos) {
                                ++p; while (p < cfg.size() && (cfg[p] == ' ' || cfg[p] == '\t' || cfg[p] == '"' )) ++p;
                                size_t e = cfg.find_first_of("\r\n\";", p);
                                if (e == std::string::npos) e = cfg.size();
                                bootPath = cfg.substr(p, e-p);
                            }
                        }
                    }
                }
                break;
            }
        }
        scan += recLen;
    }

    if (!bootPath.empty()) {
        const size_t slash = bootPath.find_last_of("\\/");
        std::string bootName = slash == std::string::npos ? bootPath : bootPath.substr(slash + 1);
        std::strncat(report, "BOOT2: ", sizeof(report)-std::strlen(report)-1);
        std::strncat(report, bootName.c_str(), sizeof(report)-std::strlen(report)-1);
        std::strncat(report, "\n", sizeof(report)-std::strlen(report)-1);
        size_t p = 0;
        while (p < data.size()) {
            const uint8_t recLen = data[p];
            if (recLen == 0) { p = ((p / sectorSize) + 1) * sectorSize; continue; }
            if (recLen < 34 || p + recLen > data.size()) break;
            const uint8_t* rec = data.data() + p;
            const uint8_t nl = rec[32];
            if (nl > 0 && size_t(33 + nl) <= recLen) {
                std::string nm(reinterpret_cast<const char*>(rec + 33), nl);
                const size_t ver = nm.find(';'); if (ver != std::string::npos) nm.resize(ver);
                if (nm == bootName && !(rec[25] & 0x02)) {
                    const uint64_t ex = uint64_t(readLe32(rec + 2)) * sectorSize;
                    const uint64_t sz = readLe32(rec + 10);
                    uint8_t elf[52] {};
                    if (sz >= sizeof(elf) && ex <= fileSize && sizeof(elf) <= fileSize - ex &&
                        pread(fd, elf, sizeof(elf), static_cast<off_t>(ex)) == sizeof(elf)) {
                        if (std::memcmp(elf, "\x7f" "ELF", 4) == 0 && elf[4] == 1 && elf[5] == 1) {
                            const uint16_t machine = uint16_t(elf[18]) | (uint16_t(elf[19]) << 8);
                            const uint32_t entry = readLe32(elf + 24);
                            char detail[128];
                            std::snprintf(detail, sizeof(detail), "Executable: ELF32 little-endian, machine=%u, entry=0x%08X\n", machine, entry);
                            std::strncat(report, detail, sizeof(report)-std::strlen(report)-1);
                            if (machine == 8) std::strncat(report, "Arsitektur MIPS terdeteksi (sesuai keluarga PS2).\n", sizeof(report)-std::strlen(report)-1);
                        } else std::strncat(report, "File BOOT2 ditemukan, tetapi header ELF standar tidak cocok.\n", sizeof(report)-std::strlen(report)-1);
                    }
                    break;
                }
            }
            p += recLen;
        }
    } else {
        std::strncat(report, "SYSTEM.CNF/BOOT2 tidak ditemukan di direktori root.\\n", sizeof(report)-std::strlen(report)-1);
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
                    std::string line = std::string(kind) + filename;
                    if (!(rec[25] & 0x02) && (filename == "CORE.GT4" || filename == "GT4.VOL")) {
                        const uint64_t bytes = readLe32(rec + 10);
                        const uint64_t extent = uint64_t(readLe32(rec + 2)) * sectorSize;
                        char sizeText[96];
                        std::snprintf(sizeText, sizeof(sizeText), " — %.2f MiB", double(bytes) / (1024.0 * 1024.0));
                        line += sizeText;
                        line += (filename == "CORE.GT4") ? " [executable GT4: ditemukan]" : " [volume data GT4: ditemukan]";
                        if (filename == "CORE.GT4" && extent <= fileSize && bytes <= fileSize - extent)
                            inspectCore(fd, extent, bytes, report);
                        if (filename == "GT4.VOL")
                            std::strncat(report, "GT4.VOL: volume aset game ditemukan; isi volume belum diekstrak.\\n", sizeof(report)-std::strlen(report)-1);
                    }
                    line += "\n";
                    std::strncat(report, line.c_str(), sizeof(report)-std::strlen(report)-1);
                    ++count;
                }
            }
        }
        pos += len;
    }
    if (count == 0)
        std::strncat(report, "(tidak ada entri yang terbaca)\n", sizeof(report)-std::strlen(report)-1);
    std::strncat(report, "Catatan: CORE.GT4 adalah executable; aset utama game berada di GT4.VOL.\n", sizeof(report)-std::strlen(report)-1);
    std::strncat(report, "Pemeriksaan read-only. Game belum dijalankan.", sizeof(report)-std::strlen(report)-1);
    return env->NewStringUTF(report);
}
