#pragma once
// SaveSystem.h — Custom encoded save file. Not encryption, just obfuscation.
// Because saving "money=9999999" in plaintext is embarrassing.
// The algorithm is roughly: XOR + byte shuffle + magic header.
// Will it stop a determined cheater? No. Will it stop a casual one? Probably.

#include "Common.h"
#include <string>

// Everything worth saving goes here
struct SaveData {
    double  money            = 0.0;
    int     diamonds         = 0;
    bool    diamondsUnlocked = false;
    int     cursedFound      = 0;      // 0–5
    bool    cursedFlags[5]   = {};     // which of the 5 cursed objects found
    int     totalPlaySeconds = 0;      // bragging rights
    int     timesOpenedCrates= 0;
    int     timesUsedItems   = 0;
    double  totalMoneyEarned = 0.0;
    // Version field so we can migrate old saves gracefully
    uint32_t version         = 1;
};

class SaveSystem {
public:
    // Returns path to save file: Documents\VkScene\save.vks
    static std::string savePath();

    // Write save — returns false if disk hates us today
    static bool save(const SaveData& data);

    // Load save — returns false if no save exists or file is corrupted
    // On failure, data is left at defaults (fresh start)
    static bool load(SaveData& data);

    // Nuke the save (for debugging or a rage-quit fresh start)
    static void deleteSave();

    static bool hasSave();

private:
    // The "encryption". Don't call it encryption.
    static void encode(std::vector<uint8_t>& bytes, uint32_t seed);
    static bool decode(std::vector<uint8_t>& bytes, uint32_t seed);

    static constexpr uint32_t MAGIC   = 0x564B5343; // "VKSC"
    static constexpr uint32_t SEED    = 0xDEADC0DE; // very creative seed
    static constexpr uint32_t VERSION = 1;
};
