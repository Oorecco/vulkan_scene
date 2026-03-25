#pragma once
// ItemSystem.h — Unique item types that drop from world crates.
// Left click to use them. Each one does something that meaningfully changes gameplay.

#include "../Common.h"
#include <string>
#include <vector>
#include <functional>
#include <optional>

// ── Item type registry ────────────────────────────────────────────────────
enum class ItemType {
    None,

    // Movement / physics
    SpeedBoost,      // +80% sprint speed for 15 seconds — feel like sonic
    GravityFlip,     // temporarily reverses gravity on the physics cube only
    CubeLauncher,    // next push/throw sends the cube flying at 5x force
    MagnetPulse,     // pulls the cube to the player instantly (dramatic thud)

    // Economy
    CoinShower,      // 30 coins rain from above over 3 seconds (visual + payout)
    DiamondPulse,    // briefly reveals hidden cursed geometry objects through walls

    // Chaos
    FreezeTime,      // pauses sim time for 5 seconds (everything frozen, you move)
    Earthquake,      // shakes the window + all world objects vibrate for 3s
    BlackHole,       // creates a brief gravity well at cube position — pulls you in

    COUNT
};

struct ItemDef {
    ItemType    type;
    const char* name;
    const char* description;   // shown in HUD when holding
    const char* icon;          // texture name in ui/icons/
    float       rarityWeight;  // higher = more common (normalized internally)
};

// Item instance (what you're holding / what's in a crate)
struct Item {
    ItemType type  = ItemType::None;
    int      count = 0; // for stackable items in future
};

// What a crate contains when opened
struct CrateReward {
    enum class Type { Money, Diamond, UniqueItem, Nothing };
    Type   type  = Type::Nothing;
    double money = 0;
    int    diamonds = 0;
    Item   item;
    std::string message; // shown in HUD notification
};

// ─────────────────────────────────────────────────────────────────────────
class ItemSystem {
public:
    ItemSystem() = default;

    // Roll a random crate reward. diamondsUnlocked gates diamond drops.
    CrateReward rollCrateReward(bool diamondsUnlocked, int rng_seed = 0) const;

    // Holds the currently held item (one slot, simple inventory)
    Item heldItem;
    bool hasItem() const { return heldItem.type != ItemType::None; }

    // Use the held item — returns true if it was used successfully.
    // The callback provides access to game state through lambdas.
    // All side effects are dispatched via the callbacks set at init.
    bool useItem();

    // ── Side-effect callbacks (set by Game on init) ────────────────────────
    // These let ItemSystem poke game state without depending on Game directly.
    std::function<void(float duration)>  onSpeedBoost;    // speed boost for Ns
    std::function<void()>                onGravityFlip;   // flip cube gravity
    std::function<void()>                onCubeLauncher;  // arm super-throw
    std::function<void()>                onMagnetPulse;   // snap cube to player
    std::function<void(double amount)>   onCoinShower;    // money + visual
    std::function<void()>                onDiamondPulse;  // reveal hidden gems
    std::function<void(float duration)>  onFreezeTime;    // freeze sim
    std::function<void(float duration)>  onEarthquake;    // window shake + jitter
    std::function<void()>                onBlackHole;     // gravity well event

    // ── Item table ─────────────────────────────────────────────────────────
    static const ItemDef ITEM_TABLE[(int)ItemType::COUNT];
    static const ItemDef* getDef(ItemType t);
};
