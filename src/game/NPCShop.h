#pragma once
// NPCShop.h — The NPC that wanders, remembers, discounts, and occasionally
// sells things that really should not exist.
// Changes from Part 3:
//   - Removed playerFoundAll5 (redundant with diamondsUnlocked)
//   - Premium items gated on diamondsUnlocked AND having ≥1 diamond
//   - NPC now has a random US first name that changes each location move
//   - Name displayed in shop title and dialogue

#include "../Common.h"
#include "DayNight.h"
#include "ItemSystem.h"
#include "../renderer/VulkanRenderer.h"
#include "../renderer/VulkanBuffer.h"
#include <vector>
#include <string>
#include <functional>

struct ShopItem {
    std::string name;
    std::string description;
    double      priceMoney;
    int         priceDiamonds;
    ItemType    grantItem;   // ItemType::None = cosmetic/non-item purchase
    int         stockLeft;   // -1 = infinite
};

struct NPCLocation {
    glm::vec3   pos;
    const char* label; // used in debug panel
};

class NPCShop {
public:
    static constexpr float INTERACT_R       = 3.0f;
    static constexpr float LOYALTY_DISCOUNT = 0.15f; // 15% off after 3+ visits

    void init(const VulkanContext& ctx);
    void destroy(VkDevice dev);

    // Update position, stock, dialogue. diamondsUnlocked = found all 5 cursed objects.
    // diamondCount = actual diamonds in player inventory (needed for premium gating).
    void update(float dt, const glm::vec3& playerPos, const DayNight& time,
                bool diamondsUnlocked, int diamondCount);

    bool playerNear()  const { return m_playerNear; }
    bool isAway()      const { return m_isAway; }
    bool shopOpen()    const { return m_shopOpen; }
    void openShop()    { m_shopOpen = true; if (onShopOpen) onShopOpen(); }
    void closeShop()   { m_shopOpen = false; }

    // Returns true + modifies currency if affordable.
    // grantedItem is set to the item the player receives (may be None).
    bool tryBuy(int index, double& money, int& diamonds,
                ItemType& grantedItem, std::string& outMsg);

    const std::vector<ShopItem>& stock()    const { return m_stock; }
    const std::string&           dialogue() const { return m_dialogue; }
    const std::string&           npcName()  const { return m_npcName; }
    glm::vec3                    pos()      const { return m_currentPos; }
    int                          visitCount() const { return m_visitCount; }

    void draw(VulkanRenderer& renderer, bool shadow);

    std::function<void()> onShopOpen;

private:
    void updateLocation(const DayNight& time);
    void refreshStock(const DayNight& time, bool diamondsUnlocked, int diamondCount);
    void updateDialogue(bool diamondsUnlocked);
    double effectivePrice(double base) const;
    void pickNewName();

    glm::vec3   m_currentPos  = {6.0f, GROUND_Y + 0.9f, 6.0f};
    glm::vec3   m_targetPos   = m_currentPos;
    int         m_locationIdx = 0;
    bool        m_playerNear  = false;
    bool        m_shopOpen    = false;
    int         m_visitCount  = 0;
    // Schedule constants (real seconds; 1 game hour = 1 real minute)
    static constexpr float STAY_DURATION = 360.0f; // 6 game hours at location
    static constexpr float AWAY_DURATION = 120.0f; // 2 game hours between locations

    float   m_stayTimer  = 0.f;  // counts up while present
    bool    m_isAway     = false; // true while travelling/absent
    float   m_awayTimer  = 0.f;  // counts up while away
    std::string m_dialogue;
    std::string m_npcName;
    std::vector<ShopItem> m_stock;

    unsigned int m_rng = 0xABCDEF01;
    glm::vec3    m_lastPlayerDir = {0.f, 0.f, 1.f}; // direction NPC faces (toward player)

    static const NPCLocation LOCATIONS[3];

    // ~30 common US first names — gender-neutral and culturally varied mix
    static const char* NAME_POOL[];
    static const int   NAME_POOL_SIZE;

    VulkanBuffer m_vb, m_ib;
    uint32_t     m_idxCount = 0;
};
