#include <jni.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
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

struct RoFsNode {
    uint32_t parent = 0;
    uint32_t nodeId = 0;
    uint32_t pageOffset = 0;
    uint32_t realSize = 0;
    uint32_t storedSize = 0;
    uint8_t type = 0;
    std::string name;
};

static uint32_t readBe32Local(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

static bool inspectGt4RofsHeader(int fd, uint64_t volumeOffset,
                                    uint64_t volumeBytes, std::string& report) {
    constexpr uint64_t pageBytes = 0x800;
    constexpr uint64_t retailTocPage = 0x2159;
    constexpr uint32_t offsetKey = 0x14ac327a;
    constexpr uint32_t maxPages = 1u << 18;
    constexpr uint32_t maxCompressedPage = 1024u * 1024u;
    const uint64_t headerOffset = retailTocPage * pageBytes;
    if (volumeBytes < headerOffset + 0x40 ||
        volumeOffset > UINT64_MAX - headerOffset) {
        report += "RoFS: volume terlalu kecil untuk TOC retail GT4.\n";
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
    const bool encrypted = magic == 0xAD90B9ACu;
    const bool plain = magic == 0x53466F52u;
    char summary[256];
    std::snprintf(summary, sizeof(summary),
        "RoFS v%u.%u: header 0x%llX, page 0x%X, total pages=%u, TOC pages=%u, compressed TOC=%u bytes.\n",
        major, minor, static_cast<unsigned long long>(headerOffset),
        pageLength, totalPages, tocPages, compressedTocBytes);
    report += summary;
    if ((!encrypted && !plain) || major != 3 || minor != 1 ||
        pageLength != pageBytes || totalPages == 0 || totalPages > maxPages ||
        tocPages == 0 || tocPages > totalPages ||
        compressedTocBytes == 0 || compressedTocBytes > 64u * 1024u * 1024u) {
        report += "RoFS: header bukan RoFS v3.1 yang valid atau field di luar batas.\n";
        return false;
    }
    const uint64_t offsetCount = uint64_t(tocPages) + 1;
    const uint64_t tableBytes = offsetCount * 4;
    if (tableBytes > volumeBytes - headerOffset - 0x40) {
        report += "RoFS: tabel page-offset melewati batas volume.\n";
        return false;
    }
    std::vector<uint8_t> table(static_cast<size_t>(tableBytes));
    if (pread(fd, table.data(), table.size(),
              static_cast<off_t>(volumeOffset + headerOffset + 0x40)) !=
        static_cast<ssize_t>(table.size())) {
        report += "RoFS: gagal membaca tabel page-offset.\n";
        return false;
    }
    std::vector<uint32_t> offsets;
    offsets.reserve(static_cast<size_t>(offsetCount));
    uint32_t previous = 0;
    uint64_t compressedSum = 0;
    for (uint32_t i = 0; i < offsetCount; ++i) {
        uint32_t value = readLe32(table.data() + size_t(i) * 4);
        if (encrypted) value ^= offsetKey * (i + 1u);
        if ((i && value <= previous) || value > volumeBytes ||
            (i == 0 && value < 0x40 + tableBytes)) {
            report += "RoFS: offset halaman tidak monoton atau di luar area TOC.\n";
            return false;
        }
        if (i) compressedSum += uint64_t(value - previous);
        offsets.push_back(value);
        previous = value;
    }
    if (compressedSum != compressedTocBytes) {
        report += "RoFS: jumlah panjang compressed page tidak cocok dengan header.\n";
        return false;
    }

    std::vector<RoFsNode> nodes;
    uint32_t recordPages = 0, indexPages = 0;
    for (uint32_t i = 0; i < tocPages; ++i) {
        const uint32_t begin = offsets[i], finish = offsets[i + 1];
        if (finish <= begin || finish - begin > maxCompressedPage ||
            uint64_t(begin) + volumeOffset > uint64_t(INT64_MAX)) {
            report += "RoFS: ukuran/rentang compressed page tidak valid.\n";
            return false;
        }
        std::vector<uint8_t> packed(finish - begin);
        if (pread(fd, packed.data(), packed.size(),
                  static_cast<off_t>(volumeOffset + begin)) !=
            static_cast<ssize_t>(packed.size())) {
            report += "RoFS: gagal membaca compressed page.\n";
            return false;
        }
        std::vector<uint8_t> page(pageLength);
        z_stream stream{};
        stream.next_in = packed.data();
        stream.avail_in = static_cast<uInt>(packed.size());
        stream.next_out = page.data();
        stream.avail_out = static_cast<uInt>(page.size());
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            report += "RoFS: zlib gagal inisialisasi.\n";
            return false;
        }
        const int inflateResult = inflate(&stream, Z_FINISH);
        const uint64_t produced = stream.total_out;
        const uint64_t consumed = stream.total_in;
        inflateEnd(&stream);
        if (inflateResult != Z_STREAM_END || produced != pageLength ||
            consumed != packed.size()) {
            report += "RoFS: TOC page gagal didekompresi ke ukuran page yang tepat.\n";
            return false;
        }
        if (encrypted) for (auto& byte : page) byte ^= 0x55;
        const uint16_t pageType = readLe16(page.data());
        const uint16_t entryCount = readLe16(page.data() + 2);
        const int32_t nextPage = static_cast<int32_t>(readLe32(page.data() + 4));
        const int32_t prevPage = static_cast<int32_t>(readLe32(page.data() + 8));
        if (pageType > 1 || entryCount > pageLength / 4 ||
            (entryCount & 1u) == 0 ||
            (nextPage != -1 && (nextPage < 0 || uint32_t(nextPage) >= tocPages)) ||
            (prevPage != -1 && (prevPage < 0 || uint32_t(prevPage) >= tocPages))) {
            report += "RoFS: header page tidak valid.\n";
            return false;
        }
        const uint32_t entryTotal = entryCount / 2;
        for (uint32_t n = 0; n < entryTotal; ++n) {
            const size_t tocPos = pageLength - 8u * (n + 1u);
            const uint16_t entryOffset = readLe16(page.data() + tocPos);
            const uint16_t entryLength = readLe16(page.data() + tocPos + 2);
            if (entryOffset < 12 || entryLength < 5 ||
                uint32_t(entryOffset) + entryLength > pageLength) {
                report += "RoFS: referensi entri TOC keluar dari batas page.\n";
                return false;
            }
            if (pageType == 0) {
                const uint16_t typeOffset = readLe16(page.data() + tocPos + 4);
                const uint16_t typeLength = readLe16(page.data() + tocPos + 6);
                if (typeLength < 5 || uint32_t(typeOffset) + typeLength > pageLength ||
                    typeOffset < 12 || page[typeOffset] > 2) {
                    report += "RoFS: metadata node pada record page tidak valid.\n";
                    return false;
                }
                RoFsNode node;
                node.parent = readBe32Local(page.data() + entryOffset);
                const size_t nameStart = size_t(entryOffset) + 4;
                size_t nameEnd = nameStart + entryLength - 4;
                while (nameEnd > nameStart && page[nameEnd - 1] == 0) --nameEnd;
                if (nameEnd == nameStart || nameEnd - nameStart > 255) {
                    report += "RoFS: nama node kosong/terlalu panjang.\n";
                    return false;
                }
                node.name.assign(reinterpret_cast<const char*>(page.data() + nameStart),
                                 nameEnd - nameStart);
                const uint8_t* meta = page.data() + typeOffset;
                node.type = meta[0];
                if (node.type == 0) {
                    if (typeLength < 5) return false;
                    node.nodeId = readLe32(meta + 1);
                } else if (node.type == 1) {
                    if (typeLength < 13) return false;
                    node.pageOffset = readLe32(meta + 1);
                    node.storedSize = node.realSize = readLe32(meta + 9);
                } else {
                    if (typeLength < 17) return false;
                    node.pageOffset = readLe32(meta + 1);
                    node.storedSize = readLe32(meta + 9);
                    node.realSize = readLe32(meta + 13);
                }
                if (nodes.size() >= 100000) {
                    report += "RoFS: jumlah node melewati batas pemeriksaan 100.000.\n";
                    return false;
                }
                nodes.push_back(std::move(node));
            } else {
                const uint32_t childPage = readLe32(page.data() + tocPos + 4);
                if (childPage >= tocPages) {
                    report += "RoFS: referensi child page pada index page tidak valid.\n";
                    return false;
                }
            }
        }
        if (pageType == 0) ++recordPages; else ++indexPages;
    }
    std::vector<std::string> paths(nodes.size());
    std::vector<uint8_t> pathState(nodes.size(), 0); // 0=unvisited, 1=visiting, 2=resolved/blocked
    std::unordered_map<uint32_t, size_t> directoryById;
    directoryById.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].type == 0) directoryById.emplace(nodes[i].nodeId, i);
    }
    size_t resolvedCount = 0, fileCount = 0, dirCount = 0, compressedCount = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (pathState[i] == 2) continue;
        std::vector<size_t> chain;
        size_t current = i;
        std::string basePath;
        bool canResolve = false;
        while (true) {
            if (pathState[current] == 2) {
                canResolve = !paths[current].empty() || nodes[current].parent == 0;
                basePath = paths[current];
                break;
            }
            if (pathState[current] == 1) break; // malformed parent cycle
            pathState[current] = 1;
            chain.push_back(current);
            const uint32_t parentId = nodes[current].parent;
            if (parentId == 0) {
                canResolve = true;
                break;
            }
            const auto parent = directoryById.find(parentId);
            if (parent == directoryById.end()) break;
            current = parent->second;
        }
        if (canResolve) {
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                const size_t index = *it;
                paths[index] = basePath.empty() ? nodes[index].name
                                                : basePath + "/" + nodes[index].name;
                basePath = paths[index];
                pathState[index] = 2;
            }
        } else {
            for (size_t index : chain) pathState[index] = 2;
        }
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (pathState[i] != 2 || paths[i].empty()) continue;
        ++resolvedCount;
        if (nodes[i].type == 0) ++dirCount;
        else {
            ++fileCount;
            if (nodes[i].type == 2) ++compressedCount;
        }
    }
    char result[256];
    std::snprintf(result, sizeof(result),
        "RoFS TOC: %u page (%u record, %u index); %zu node (%zu path terurai, %zu folder, %zu file, %zu compressed).\n",
        tocPages, recordPages, indexPages, nodes.size(), resolvedCount,
        dirCount, fileCount, compressedCount);
    report += result;
    report += "Contoh path dalam GT4.VOL:\n";
    size_t shown = 0;
    for (size_t i = 0; i < nodes.size() && shown < 18; ++i) {
        if (!resolved[i] || nodes[i].type == 0) continue;
        char detail[96];
        std::snprintf(detail, sizeof(detail), "  %s (%u byte%s)\n",
            paths[i].c_str(), nodes[i].realSize, nodes[i].type == 2 ? ", compressed" : "");
        report += detail;
        ++shown;
    }
    if (resolvedCount != nodes.size())
        report += "Peringatan: sebagian node belum punya parent folder yang dikenali; belum ditampilkan sebagai path.\n";
    report += "RoFS: path node berhasil dibangun. Pembacaan payload file dan dekompresi aset belum tersedia.\n";
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
