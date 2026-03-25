// ItemSystem.cpp — What's in the box? Only one way to find out.
#include "ItemSystem.h"
#include <cstdlib>
#include <cstring>
#include <cmath>

// ── Item definitions ──────────────────────────────────────────────────────
const ItemDef ItemSystem::ITEM_TABLE[(int)ItemType::COUNT] = {
    { ItemType::None,         "None",          "",                                    "",                 0.0f  },
    { ItemType::SpeedBoost,   "Speed Boost",   "Sprint at 180%% speed for 15 sec",   "item_speed.png",   3.0f  },
    { ItemType::GravityFlip,  "Gravity Flip",  "Reverses cube gravity briefly",       "item_gravity.png", 2.5f  },
    { ItemType::CubeLauncher, "Cube Launcher", "Next throw is 5x more powerful",      "item_launch.png",  2.5f  },
    { ItemType::MagnetPulse,  "Magnet Pulse",  "Snaps cube to you instantly",         "item_magnet.png",  3.5f  },
    { ItemType::CoinShower,   "Coin Shower",   "It's raining money. Literally.",      "item_coins.png",   2.0f  },
    { ItemType::DiamondPulse, "Diamond Pulse", "Reveals cursed objects for 8 sec",   "item_diamond.png", 1.5f  },
    { ItemType::FreezeTime,   "Time Freeze",   "Freezes the world for 5 sec",         "item_freeze.png",  1.5f  },
    { ItemType::Earthquake,   "Earthquake",    "Shakes everything violently",         "item_quake.png",   2.0f  },
    { ItemType::BlackHole,    "Black Hole",    "Gravity well pulls you toward cube",  "item_hole.png",    1.0f  },
};

const ItemDef* ItemSystem::getDef(ItemType t) {
    int idx = (int)t;
    if (idx < 0 || idx >= (int)ItemType::COUNT) return &ITEM_TABLE[0];
    return &ITEM_TABLE[idx];
}

// ── Crate reward roller ───────────────────────────────────────────────────
CrateReward ItemSystem::rollCrateReward(bool diamondsUnlocked, int seed) const {
    // Use a simple LCG so seed affects output predictably
    unsigned int rng = (unsigned int)(seed ^ 0xDEADBEEF);
    auto rnd = [&]() -> float {
        rng = rng * 1664525u + 1013904223u;
        return (float)(rng & 0xFFFFFF) / (float)0xFFFFFF;
    };

    CrateReward r;

    // Roll what category this crate contains:
    // 55% money  |  15% diamond (if unlocked, else re-roll to money)
    // 20% item   |  10% nothing (a note that just says "404")
    float roll = rnd();

    if (roll < 0.10f) {
        // Nothing — but make it funny
        r.type    = CrateReward::Type::Nothing;
        r.message = "The crate was empty. There's a sticky note: 'Out of order'";
        return r;
    }

    if (roll < 0.65f) {
        // Money — exponential spread: mostly small, rarely huge
        r.type = CrateReward::Type::Money;
        float tier = rnd();
        if (tier < 0.55f)       r.money = 50.0  + rnd()*150.0;   // 50–200
        else if (tier < 0.80f)  r.money = 200.0 + rnd()*800.0;   // 200–1000
        else if (tier < 0.95f)  r.money = 1000.0+ rnd()*4000.0;  // 1K–5K
        else                    r.money = 10000.0+ rnd()*40000.0; // 10K–50K (jackpot)
        char buf[64];
        snprintf(buf, sizeof(buf), "Found $%.0f in the crate!", r.money);
        r.message = buf;
        return r;
    }

    if (roll < 0.80f) {
        // Diamond (only if unlocked, else gives bonus money instead)
        if (diamondsUnlocked) {
            r.type     = CrateReward::Type::Diamond;
            r.diamonds = (rnd() < 0.85f) ? 1 : 2; // 85% = 1, 15% = 2
            char buf[64];
            snprintf(buf, sizeof(buf), "Found %d Diamond%s!",
                r.diamonds, r.diamonds>1?"s":"");
            r.message = buf;
        } else {
            r.type  = CrateReward::Type::Money;
            r.money = 500.0 + rnd()*1500.0;
            r.message = "Found a locked gem... converted to cash somehow.";
        }
        return r;
    }

    // Unique item
    r.type = CrateReward::Type::UniqueItem;

    // Weighted random item pick (exclude None = index 0)
    float totalWeight = 0.0f;
    for (int i = 1; i < (int)ItemType::COUNT; i++)
        totalWeight += ITEM_TABLE[i].rarityWeight;

    float pick = rnd() * totalWeight;
    float acc  = 0.0f;
    ItemType chosen = ItemType::SpeedBoost; // fallback
    for (int i = 1; i < (int)ItemType::COUNT; i++) {
        acc += ITEM_TABLE[i].rarityWeight;
        if (pick <= acc) { chosen = (ItemType)i; break; }
    }

    r.item.type  = chosen;
    r.item.count = 1;
    char buf[128];
    snprintf(buf, sizeof(buf), "Found: %s — %s",
        getDef(chosen)->name, getDef(chosen)->description);
    r.message = buf;
    return r;
}

// ── Item use ──────────────────────────────────────────────────────────────
bool ItemSystem::useItem() {
    if (heldItem.type == ItemType::None) return false;

    switch (heldItem.type) {
    case ItemType::SpeedBoost:
        if (onSpeedBoost) onSpeedBoost(15.0f);
        break;
    case ItemType::GravityFlip:
        if (onGravityFlip) onGravityFlip();
        break;
    case ItemType::CubeLauncher:
        if (onCubeLauncher) onCubeLauncher();
        break;
    case ItemType::MagnetPulse:
        if (onMagnetPulse) onMagnetPulse();
        break;
    case ItemType::CoinShower:
        if (onCoinShower) onCoinShower(3000.0 + (rand()%5000));
        break;
    case ItemType::DiamondPulse:
        if (onDiamondPulse) onDiamondPulse();
        break;
    case ItemType::FreezeTime:
        if (onFreezeTime) onFreezeTime(5.0f);
        break;
    case ItemType::Earthquake:
        if (onEarthquake) onEarthquake(3.0f);
        break;
    case ItemType::BlackHole:
        if (onBlackHole) onBlackHole();
        break;
    default:
        break;
    }

    heldItem = {};  // consume item
    return true;
}
