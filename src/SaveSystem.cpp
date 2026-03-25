// SaveSystem.cpp — The file that remembers things so you don't have to.
// Data layout: [magic(4)] [version(4)] [payload_len(4)] [encoded_payload]
// The encoding is XOR with a rolling key derived from SEED + byte position.
// Byte order is little-endian because we live on x86 and don't apologise for it.
#include "SaveSystem.h"
#include <fstream>
#include <shlobj.h>
#include <cstring>

static std::string saveDir() {
    char d[MAX_PATH] = {};
    SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, d);
    return std::string(d) + "\\VkScene\\";
}
std::string SaveSystem::savePath() { return saveDir() + "save.vks"; }

bool SaveSystem::hasSave() {
    std::ifstream f(savePath(), std::ios::binary);
    return f.good();
}
void SaveSystem::deleteSave() { remove(savePath().c_str()); }

// ── Encode / decode — the "security" ─────────────────────────────────────
// It's XOR with a position-dependent key. Not AES. Don't tell anyone.
void SaveSystem::encode(std::vector<uint8_t>& bytes, uint32_t seed) {
    uint32_t k = seed;
    for (size_t i = 0; i < bytes.size(); i++) {
        // Advance LCG each byte so the key changes
        k = k * 1664525u + 1013904223u;
        bytes[i] ^= (uint8_t)(k >> 17); // use high bits, they're less boring
    }
    // Bonus: swap adjacent bytes (just to be extra annoying to hex editors)
    for (size_t i = 0; i + 1 < bytes.size(); i += 2)
        std::swap(bytes[i], bytes[i+1]);
}

bool SaveSystem::decode(std::vector<uint8_t>& bytes, uint32_t seed) {
    // Swap back first (reverse of encode)
    for (size_t i = 0; i + 1 < bytes.size(); i += 2)
        std::swap(bytes[i], bytes[i+1]);
    // XOR is its own inverse, re-running the same key undoes it
    uint32_t k = seed;
    for (size_t i = 0; i < bytes.size(); i++) {
        k = k * 1664525u + 1013904223u;
        bytes[i] ^= (uint8_t)(k >> 17);
    }
    return true; // decode can't really fail, validation happens in load()
}

// ── Save ──────────────────────────────────────────────────────────────────
bool SaveSystem::save(const SaveData& data) {
    CreateDirectoryA(saveDir().c_str(), nullptr);

    // Serialise SaveData into a raw byte buffer
    // We write field by field so adding new fields doesn't break alignment
    std::vector<uint8_t> payload;
    auto write = [&](const void* ptr, size_t sz) {
        const uint8_t* b = (const uint8_t*)ptr;
        payload.insert(payload.end(), b, b + sz);
    };
    write(&data.version,         sizeof(data.version));
    write(&data.money,           sizeof(data.money));
    write(&data.diamonds,        sizeof(data.diamonds));
    write(&data.diamondsUnlocked,sizeof(data.diamondsUnlocked));
    write(&data.cursedFound,     sizeof(data.cursedFound));
    write(data.cursedFlags,      sizeof(data.cursedFlags));
    write(&data.totalPlaySeconds,sizeof(data.totalPlaySeconds));
    write(&data.timesOpenedCrates,sizeof(data.timesOpenedCrates));
    write(&data.timesUsedItems,  sizeof(data.timesUsedItems));
    write(&data.totalMoneyEarned,sizeof(data.totalMoneyEarned));

    // Encode the payload
    encode(payload, SEED);

    // Write the file
    std::ofstream f(savePath(), std::ios::binary);
    if (!f.is_open()) {
        LOG_WARN("SaveSystem: cannot write save file — is Documents read-only? That would be weird.");
        return false;
    }
    // Header
    f.write((char*)&MAGIC,   sizeof(MAGIC));
    f.write((char*)&VERSION, sizeof(VERSION));
    uint32_t payloadLen = (uint32_t)payload.size();
    f.write((char*)&payloadLen, sizeof(payloadLen));
    // Payload
    f.write((char*)payload.data(), payload.size());
    LOG_INFO(Fmt("SaveSystem: saved OK (%u bytes)", (uint32_t)(12 + payload.size())));
    return true;
}

// ── Load ──────────────────────────────────────────────────────────────────
bool SaveSystem::load(SaveData& data) {
    std::ifstream f(savePath(), std::ios::binary);
    if (!f.is_open()) return false; // no save, that's fine

    // Read and validate header
    uint32_t magic=0, version=0, payloadLen=0;
    f.read((char*)&magic,      sizeof(magic));
    f.read((char*)&version,    sizeof(version));
    f.read((char*)&payloadLen, sizeof(payloadLen));

    if (magic != MAGIC) {
        LOG_WARN("SaveSystem: bad magic — file corrupted or someone was poking around.");
        return false;
    }
    if (version > VERSION) {
        LOG_WARN("SaveSystem: save is from a future version. Time travel save?");
        return false;
    }
    if (payloadLen > 4096) { // sanity cap — no legitimate save is 4KB
        LOG_WARN("SaveSystem: payload suspiciously large. Discarding.");
        return false;
    }

    std::vector<uint8_t> payload(payloadLen);
    f.read((char*)payload.data(), payloadLen);
    if (!f) { LOG_WARN("SaveSystem: truncated file."); return false; }

    decode(payload, SEED);

    // Deserialise
    size_t pos = 0;
    auto read = [&](void* ptr, size_t sz) -> bool {
        if (pos + sz > payload.size()) return false;
        memcpy(ptr, payload.data() + pos, sz);
        pos += sz;
        return true;
    };

    uint32_t savedVer = 0;
    if (!read(&savedVer, sizeof(savedVer))) return false;

    // Read fields — if version < current, use defaults for missing fields
    if (!read(&data.money,            sizeof(data.money)))            return false;
    if (!read(&data.diamonds,         sizeof(data.diamonds)))         return false;
    if (!read(&data.diamondsUnlocked, sizeof(data.diamondsUnlocked))) return false;
    if (!read(&data.cursedFound,      sizeof(data.cursedFound)))      return false;
    if (!read(data.cursedFlags,       sizeof(data.cursedFlags)))      return false;
    if (!read(&data.totalPlaySeconds, sizeof(data.totalPlaySeconds))) return false;
    if (!read(&data.timesOpenedCrates,sizeof(data.timesOpenedCrates)))return false;
    if (!read(&data.timesUsedItems,   sizeof(data.timesUsedItems)))   return false;
    if (!read(&data.totalMoneyEarned, sizeof(data.totalMoneyEarned))) return false;

    // Sanity clamp — because what if someone DID hex-edit it
    data.money      = std::max(0.0, data.money);
    data.diamonds   = std::clamp(data.diamonds, 0, 9999);
    data.cursedFound= std::clamp(data.cursedFound, 0, 5);

    data.version = VERSION;
    LOG_INFO(Fmt("SaveSystem: loaded OK — money=$%.0f diamonds=%d cursed=%d/5",
        data.money, data.diamonds, data.cursedFound));
    return true;
}
