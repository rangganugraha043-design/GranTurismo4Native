#include <jni.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

static uint16_t readLe16(const uint8_t* p);
static uint32_t readLe32(const uint8_t* p);

static bool inspectCore(int fd, uint64_t offset, uint64_t fileBytes, std::string& report) {
    // Retail CORE.GT4 has a 6-byte wrapper followed by a raw DEFLATE stream.
    if (fileBytes < 7 || fileBytes > 16u * 1024u * 1024u) {
        report += "CORE.GT4: ukuran file di luar batas pemeriksaan aman.\n";
        return false;
    }
    std::vector<uint8_t> packed(static_cast<size_t>(fileBytes));
    if (pread(fd, packed.data(), packed.size(), static_cast<off_t>(offset)) !=
        static_cast<ssize_t>(packed.size())) {
        report += "CORE.GT4: gagal membaca data executable.\n";
        return false;
    }
    const uint16_t flags = uint16_t(packed[0]) | (uint16_t(packed[1]) << 8);
    const uint32_t expectedSize = readLe32(packed.data() + 2);
    if (expectedSize == 0 || expectedSize > 32u * 1024u * 1024u) {
        report += "CORE.GT4: ukuran hasil dekompresi tidak valid.\n";
        return false;
    }

    std::vector<uint8_t> unpacked(static_cast<size_t>(expectedSize));
    z_stream stream {};
    stream.next_in = packed.data() + 6;
    stream.avail_in = static_cast<uInt>(packed.size() - 6);
    stream.next_out = unpacked.data();
    stream.avail_out = static_cast<uInt>(unpacked.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        report += "CORE.GT4: zlib gagal diinisialisasi.\n";
        return false;
    }
    const int result = inflate(&stream, Z_FINISH);
    const uint64_t produced = stream.total_out;
    inflateEnd(&stream);

    char detail[192];
    std::snprintf(detail, sizeof(detail),
        "CORE.GT4: flags=0x%04X, target=%u byte, hasil=%llu byte.\n",
        flags, expectedSize, static_cast<unsigned long long>(produced));
    report += detail;
    if (result != Z_STREAM_END || produced != expectedSize) {
        report += "CORE.GT4: dekompresi belum valid/utuh; executable tidak akan dijalankan.\n";
        return false;
    }
    // Parse the decompressed CORE header: two sized hashes, section count,
    // EE entry point, then (load address, byte count, data) records.
    size_t cursor = 0;
    if (unpacked.size() < 4) {
        report += "CORE.GT4: header hasil dekompresi terlalu pendek.\n";
        return false;
    }
    const uint16_t hash1Size = readLe16(unpacked.data() + cursor);
    cursor += 2;
    if (hash1Size > 128 || cursor + hash1Size + 2 > unpacked.size()) {
        report += "CORE.GT4: ukuran hash pertama tidak valid.\n";
        return false;
    }
    cursor += hash1Size;
    const uint16_t hash2Size = readLe16(unpacked.data() + cursor);
    cursor += 2;
    if (hash2Size > 128 || cursor + hash2Size + 8 > unpacked.size()) {
        report += "CORE.GT4: ukuran hash kedua tidak valid.\n";
        return false;
    }
    cursor += hash2Size;
    const uint32_t sectionCount = readLe32(unpacked.data() + cursor);
    cursor += 4;
    const uint32_t entry = readLe32(unpacked.data() + cursor);
    cursor += 4;
    if (sectionCount == 0 || sectionCount > 32) {
        report += "CORE.GT4: jumlah section tidak masuk akal.\n";
        return false;
    }
    bool entryCovered = false;
    for (uint32_t i = 0; i < sectionCount; ++i) {
        if (cursor + 8 > unpacked.size()) {
            report += "CORE.GT4: tabel section terpotong.\n";
            return false;
        }
        const uint32_t loadAddress = readLe32(unpacked.data() + cursor);
        const uint32_t sectionBytes = readLe32(unpacked.data() + cursor + 4);
        cursor += 8;
        if (sectionBytes == 0 || sectionBytes > unpacked.size() - cursor ||
            uint64_t(loadAddress) + sectionBytes > 0x100000000ULL) {
            report += "CORE.GT4: rentang data section tidak valid.\n";
            return false;
        }
        if (entry >= loadAddress && uint64_t(entry) < uint64_t(loadAddress) + sectionBytes)
            entryCovered = true;
        cursor += sectionBytes;
    }
    char coreDetail[160];
    std::snprintf(coreDetail, sizeof(coreDetail),
        "Header CORE: %u section, entry 0x%08X, hash %u/%u byte.\n",
        sectionCount, entry, hash1Size, hash2Size);
    report += coreDetail;
    if (!entryCovered) {
        report += "CORE.GT4: entry point tidak berada di dalam section mana pun.\n";
        return false;
    }
    report += "CORE.GT4: kompresi dan struktur section valid. Hash belum diverifikasi; belum menjalankan mesin PS2.\n";
    return true;
}

static uint16_t readLe16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

static bool inspectGt4RofsHeader(int fd, uint64_t volumeOffset,
                                    uint64_t volumeBytes, std::string& report) {
    // Retail GT4 uses a RoFS v3.1 volume. Its real TOC header is at page
    // 0x2159 (0x800 bytes/page); the apparent GT3-like header at byte 0 is a decoy.
    constexpr uint64_t pageBytes = 0x800;
    constexpr uint64_t retailTocPage = 0x2159;
    constexpr uint32_t offsetKey = 0x14ac327a;
    constexpr uint32_t maxPages = 1u << 20;
    constexpr uint32_t maxTocBytes = 64u * 1024u * 1024u;
    const uint64_t headerOffset = retailTocPage * pageBytes;
    if (volumeBytes < headerOffset + 0x40 ||
        volumeOffset > UINT64_MAX - headerOffset) {
        report += "RoFS: volume terlalu kecil untuk header TOC retail GT4.\n";
        return false;
    }
    uint8_t header[0x40]{};
    if (pread(fd, header, sizeof(header),
              static_cast<off_t>(volumeOffset + headerOffset)) != sizeof(header)) {
        report += "RoFS: gagal membaca header TOC.\n";
        return false;
    }
    const uint32_t magic = readLe32(header);
    const uint16_t major = readLe16(header + 4);
    const uint16_t minor = readLe16(header + 6);
    const uint32_t compressedTocBytes = readLe32(header + 8);
    const uint32_t totalPages = readLe32(header + 12);
    const uint16_t pageLength = readLe16(header + 16);
    const uint16_t tocPages = readLe16(header + 18);
    const bool encryptedMagic = magic == 0xAD90B9ACu;
    const bool plainMagic = magic == 0x53466F52u; // "RoFS"
    char summary[256];
    std::snprintf(summary, sizeof(summary),
        "RoFS header @0x%llX: magic=0x%08X, version=%u.%u, page=0x%X, pages=%u, TOC pages=%u, compressed TOC=%u bytes.\n",
        static_cast<unsigned long long>(headerOffset), magic, major, minor,
        pageLength, totalPages, tocPages, compressedTocBytes);
    report += summary;
    if ((!encryptedMagic && !plainMagic) || major != 3 || minor != 1 ||
        pageLength != pageBytes || totalPages == 0 || totalPages > maxPages ||
        tocPages == 0 || tocPages > totalPages ||
        compressedTocBytes == 0 || compressedTocBytes > maxTocBytes) {
        report += "RoFS: header bukan RoFS v3.1 retail yang valid atau field di luar batas.\n";
        return false;
    }
    // The page-offset array has totalPages+1 little-endian values.
    // Retail stores them XOR-obfuscated; plain RoFS magic means no encryption.
    const uint64_t offsetCount = uint64_t(totalPages) + 1;
    if (offsetCount > (UINT64_MAX - 0x40) / 4) return false;
    const uint64_t tableBytes = offsetCount * 4;
    if (0x40 + tableBytes > volumeBytes - headerOffset) {
        report += "RoFS: tabel page-offset melewati batas file volume.\n";
        return false;
    }
    std::vector<uint8_t> table(static_cast<size_t>(tableBytes));
    if (pread(fd, table.data(), table.size(),
              static_cast<off_t>(volumeOffset + headerOffset + 0x40)) !=
        static_cast<ssize_t>(table.size())) {
        report += "RoFS: gagal membaca tabel page-offset.\n";
        return false;
    }
    uint32_t previous = 0;
    bool offsetsValid = true;
    for (uint32_t i = 0; i <= totalPages; ++i) {
        const uint32_t stored = readLe32(table.data() + size_t(i) * 4);
        const uint32_t decoded = plainMagic
            ? stored : (stored ^ (uint32_t(i * offsetKey) + offsetKey));
        if (i && decoded < previous) offsetsValid = false;
        if (decoded > compressedTocBytes) offsetsValid = false;
        previous = decoded;
    }
    if (!offsetsValid) {
        report += "RoFS: tabel offset tidak monoton/di luar rentang TOC; tidak akan diekstrak.\n";
        return false;
    }
    report += encryptedMagic
        ? "RoFS: magic dan tabel offset terenkripsi dikenali; indeks node belum didekode.\n"
        : "RoFS: magic tidak terenkripsi; indeks node belum didekode.\n";
    return true;
}

static uint32_t readLe32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}


struct IsoEntry {
    std::string name;
    std::string path;
    uint64_t offset;
    uint64_t size;
    bool directory;
};

static bool scanIsoDirectory(int fd, uint64_t fileSize, uint64_t lba,
                             uint64_t byteCount, const std::string& parent,
                             unsigned depth, std::vector<IsoEntry>& entries,
                             std::vector<uint64_t>& visited, uint64_t& bytesRead) {
    constexpr uint64_t sector = 2048;
    constexpr uint64_t maxDirectory = 4u * 1024u * 1024u;
    constexpr uint64_t maxTotal = 32u * 1024u * 1024u;
    constexpr size_t maxEntries = 4096;
    if (depth > 8 || byteCount == 0 || byteCount > maxDirectory ||
        byteCount > maxTotal - bytesRead || entries.size() >= maxEntries ||
        lba > UINT64_MAX / sector) return false;
    const uint64_t offset = lba * sector;
    if (offset > fileSize || byteCount > fileSize - offset ||
        offset > uint64_t(INT64_MAX)) return false;
    for (uint64_t seen : visited) if (seen == lba) return true;
    visited.push_back(lba);
    std::vector<uint8_t> data(static_cast<size_t>(byteCount));
    if (pread(fd, data.data(), data.size(), static_cast<off_t>(offset)) !=
        static_cast<ssize_t>(data.size())) return false;
    bytesRead += byteCount;
    size_t pos = 0;
    std::vector<IsoEntry> children;
    while (pos < data.size() && entries.size() + children.size() < maxEntries) {
        const uint8_t len = data[pos];
        if (len == 0) {
            pos = ((pos / sector) + 1) * sector;
            continue;
        }
        if (len < 34 || pos + len > data.size()) break;
        const uint8_t* rec = data.data() + pos;
        const uint8_t nameLen = rec[32];
        if (nameLen > 0 && size_t(33 + nameLen) <= len) {
            const bool dot = nameLen == 1 && (rec[33] == 0 || rec[33] == 1);
            if (!dot) {
                std::string name(reinterpret_cast<const char*>(rec + 33), nameLen);
                const size_t version = name.find(';');
                if (version != std::string::npos) name.resize(version);
                while (!name.empty() && name.back() == '.') name.pop_back();
                if (!name.empty()) {
                    const uint64_t extent = readLe32(rec + 2);
                    const uint64_t size = readLe32(rec + 10);
                    const bool isDir = (rec[25] & 0x02) != 0;
                    if (extent <= UINT64_MAX / sector) {
                        IsoEntry item{name, parent.empty() ? name : parent + "/" + name,
                                      extent * sector, size, isDir};
                        children.push_back(item);
                    }
                }
            }
        }
        pos += len;
    }
    for (const auto& item : children) entries.push_back(item);
    if (depth < 8) {
        for (const auto& item : children) {
            if (item.directory && entries.size() < maxEntries) {
                if (!scanIsoDirectory(fd, fileSize, item.offset / sector, item.size,
                                      item.path, depth + 1, entries, visited, bytesRead))
                    continue;
            }
        }
    }
    return true;
}

static void appendReport(char* report, size_t capacity, const std::string& value) {
    const size_t used = std::strlen(report);
    if (used >= capacity - 1) return;
    std::strncat(report, value.c_str(), capacity - used - 1);
}
static bool sameIsoName(const std::string& a, const char* b) {
    if (a.size() != std::strlen(b)) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = char(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = char(y - 'a' + 'A');
        if (x != y) return false;
    }
    return true;
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
        appendReport(report, sizeof(report), "Record direktori root tidak valid.");
        return env->NewStringUTF(report);
    }
    const uint64_t rootLba = readLe32(root + 2);
    const uint64_t rootLength = readLe32(root + 10);
    std::vector<IsoEntry> entries;
    std::vector<uint64_t> visited;
    uint64_t directoryBytes = 0;
    if (!scanIsoDirectory(fd, fileSize, rootLba, rootLength, "", 0,
                          entries, visited, directoryBytes)) {
        appendReport(report, sizeof(report), "Gagal membaca struktur direktori ISO dengan batas keamanan.");
        return env->NewStringUTF(report);
    }

    appendReport(report, sizeof(report), "Pemindaian direktori rekursif ISO-9660 (maks. 8 tingkat):\n");
    char countText[128];
    std::snprintf(countText, sizeof(countText), "%zu entri ditemukan; %zu direktori dikunjungi.\n",
                  entries.size(), visited.size());
    appendReport(report, sizeof(report), countText);

    const IsoEntry* systemCnf = nullptr;
    const IsoEntry* bootElf = nullptr;
    const IsoEntry* coreFile = nullptr;
    const IsoEntry* volumeFile = nullptr;
    for (const auto& item : entries) {
        if (item.directory) continue;
        if (sameIsoName(item.name, "SYSTEM.CNF")) systemCnf = &item;
        if (sameIsoName(item.name, "CORE.GT4")) coreFile = &item;
        if (sameIsoName(item.name, "GT4.VOL")) volumeFile = &item;
    }

    std::string bootPath;
    if (systemCnf && systemCnf->size > 0 && systemCnf->size <= 4096 &&
        systemCnf->offset <= fileSize && systemCnf->size <= fileSize - systemCnf->offset) {
        std::vector<char> cfg(static_cast<size_t>(systemCnf->size) + 1, 0);
        if (pread(fd, cfg.data(), static_cast<size_t>(systemCnf->size),
                  static_cast<off_t>(systemCnf->offset)) == static_cast<ssize_t>(systemCnf->size)) {
            std::string text(cfg.data());
            size_t p = text.find("BOOT2");
            if (p != std::string::npos) {
                p = text.find('=', p);
                if (p != std::string::npos) {
                    ++p;
                    while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '"')) ++p;
                    size_t e = text.find_first_of("\r\n\";", p);
                    if (e == std::string::npos) e = text.size();
                    bootPath = text.substr(p, e - p);
                }
            }
        }
    }
    if (!systemCnf) appendReport(report, sizeof(report), "SYSTEM.CNF tidak ditemukan pada pohon direktori.\n");
    else appendReport(report, sizeof(report), ("SYSTEM.CNF: " + systemCnf->path + "\n").c_str());

    if (!bootPath.empty()) {
        const size_t slash = bootPath.find_last_of("\\/");
        const std::string bootName = slash == std::string::npos ? bootPath : bootPath.substr(slash + 1);
        appendReport(report, sizeof(report), ("BOOT2: " + bootPath + "\n").c_str());
        for (const auto& item : entries) {
            if (!item.directory && sameIsoName(item.name, bootName.c_str())) {
                bootElf = &item;
                break;
            }
        }
        if (bootElf && bootElf->size >= 52 && bootElf->offset <= fileSize &&
            52 <= fileSize - bootElf->offset) {
            uint8_t elf[52]{};
            if (pread(fd, elf, sizeof(elf), static_cast<off_t>(bootElf->offset)) == sizeof(elf)) {
                if (std::memcmp(elf, "\x7f" "ELF", 4) == 0 && elf[4] == 1 && elf[5] == 1) {
                    const uint16_t machine = uint16_t(elf[18]) | (uint16_t(elf[19]) << 8);
                    char detail[160];
                    std::snprintf(detail, sizeof(detail),
                        "Executable: ELF32 little-endian, machine=%u, entry=0x%08X\n",
                        machine, readLe32(elf + 24));
                    appendReport(report, sizeof(report), detail);
                    if (machine == 8) appendReport(report, sizeof(report), "Arsitektur MIPS terdeteksi.\n");
                } else appendReport(report, sizeof(report), "Header BOOT2 bukan ELF32 little-endian standar.\n");
            }
        } else appendReport(report, sizeof(report), "File BOOT2 tidak ditemukan/ukurannya tidak valid.\n");
    }

    if (coreFile) {
        char detail[512];
        std::snprintf(detail, sizeof(detail), "CORE.GT4: %s — %.2f MiB [ditemukan]\n",
                      coreFile->path.c_str(), double(coreFile->size) / (1024.0 * 1024.0));
        appendReport(report, sizeof(report), detail);
        if (coreFile->offset <= fileSize && coreFile->size <= fileSize - coreFile->offset) {
            std::string coreReport;
            inspectCore(fd, coreFile->offset, coreFile->size, coreReport);
            appendReport(report, sizeof(report), coreReport);
        } else appendReport(report, sizeof(report), "CORE.GT4: rentang file di luar image.\n");
    } else appendReport(report, sizeof(report), "CORE.GT4 tidak ditemukan dalam pohon ISO-9660.\n");

    if (volumeFile) {
        char detail[512];
        std::snprintf(detail, sizeof(detail), "GT4.VOL: %s — %.2f MiB [ditemukan]\n",
                      volumeFile->path.c_str(), double(volumeFile->size) / (1024.0 * 1024.0));
        appendReport(report, sizeof(report), detail);
        std::string rofsReport;
        inspectGt4RofsHeader(fd, volumeFile->offset, volumeFile->size, rofsReport);
        appendReport(report, sizeof(report), rofsReport);
    } else appendReport(report, sizeof(report), "GT4.VOL tidak ditemukan dalam pohon ISO-9660.\n");

    appendReport(report, sizeof(report), "Contoh entri:\n");
    size_t shown = 0;
    for (const auto& item : entries) {
        if (shown >= 12) break;
        appendReport(report, sizeof(report), std::string(item.directory ? "[DIR] " : "[FILE] ") +
                     item.path + "\n");
        ++shown;
    }
    appendReport(report, sizeof(report),
        "Catatan: pemindaian ini hanya membaca metadata ISO-9660; UDF-only, multi-extent, dan GT4.VOL parsing belum didukung. Game belum dijalankan.");
    return env->NewStringUTF(report);
}
