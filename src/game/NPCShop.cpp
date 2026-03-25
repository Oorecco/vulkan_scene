// NPCShop.cpp — This NPC has seen things. Specifically, the player throwing cubes
// at walls for twenty minutes. They've formed an opinion.
#include "NPCShop.h"
#include <algorithm>
#include <cmath>

const NPCLocation NPCShop::LOCATIONS[3] = {
    {{ 6.0f, GROUND_Y + 0.9f,  6.0f }, "Center Market"},
    {{-8.0f, GROUND_Y + 0.9f, -8.0f }, "Northwest Corner"},
    {{10.0f, GROUND_Y + 0.9f,-12.0f }, "South Post"},
};

// Gender-neutral, culturally varied US first names.
// Mix of classic, modern, and regional US naming trends.
// (Nobody named "Bob" is allowed to sell cursed items. It's a rule.)
const char* NPCShop::NAME_POOL[] = {
    "Alex", "Jordan", "Morgan", "Taylor", "Casey", "Riley", "Drew",
    "Quinn", "Blake", "Avery", "Charlie", "Jesse", "Logan", "Peyton",
    "Skyler", "Devon", "Reese", "Jamie", "Kendall", "Cameron",
    "Marcus", "Diane", "Tessa", "Omar", "Priya", "Leon", "Naomi",
    "Ellis", "Harper", "Sage"
};
const int NPCShop::NAME_POOL_SIZE = 30;

static float lcgF(unsigned int& s) {
    s = s * 1664525u + 1013904223u;
    return (float)(s & 0xFFFFFF) / (float)0xFFFFFF;
}

void NPCShop::pickNewName() {
    int idx = (int)(lcgF(m_rng) * NAME_POOL_SIZE) % NAME_POOL_SIZE;
    m_npcName = NAME_POOL[idx];
}

void NPCShop::init(const VulkanContext& ctx) {
    pickNewName(); // Pick a name immediately so it's never empty
    m_currentPos = LOCATIONS[0].pos;
    m_targetPos  = m_currentPos;

    // NPC is a golden-tuniced figure. Unmistakable. Slightly unsettling.
    const float W=0.3f; glm::vec3 bodyCol={0.85f,0.75f,0.30f}, headCol={0.90f,0.72f,0.58f};
    std::vector<Vertex3D> verts; std::vector<uint32_t> idxs;
    static const glm::vec3 N6[6] = {
        {0.f,0.f,1.f},{0.f,0.f,-1.f},{-1.f,0.f,0.f},
        {1.f,0.f,0.f},{0.f,1.f,0.f}, {0.f,-1.f,0.f}
    };

    auto addBox=[&](float bx,float by,float bz,float hw,float hh,float hd,glm::vec3 col){
        static const glm::vec3 TP[6][4]={
            {{-1.f,-1.f,+1.f},{+1.f,-1.f,+1.f},{+1.f,+1.f,+1.f},{-1.f,+1.f,+1.f}},
            {{+1.f,-1.f,-1.f},{-1.f,-1.f,-1.f},{-1.f,+1.f,-1.f},{+1.f,+1.f,-1.f}},
            {{-1.f,-1.f,-1.f},{-1.f,-1.f,+1.f},{-1.f,+1.f,+1.f},{-1.f,+1.f,-1.f}},
            {{+1.f,-1.f,+1.f},{+1.f,-1.f,-1.f},{+1.f,+1.f,-1.f},{+1.f,+1.f,+1.f}},
            {{-1.f,+1.f,+1.f},{+1.f,+1.f,+1.f},{+1.f,+1.f,-1.f},{-1.f,+1.f,-1.f}},
            {{-1.f,-1.f,-1.f},{+1.f,-1.f,-1.f},{+1.f,-1.f,+1.f},{-1.f,-1.f,+1.f}},
        };
        for(int f=0;f<6;f++){uint32_t b=(uint32_t)verts.size();for(int v=0;v<4;v++){glm::vec3 p={bx+TP[f][v].x*hw,by+TP[f][v].y*hh,bz+TP[f][v].z*hd};verts.push_back({p,N6[f],col,{0.f,0.f}});}idxs.insert(idxs.end(),{b,b+1,b+2,b,b+2,b+3});}
    };
    addBox(0.f,0.9f,0.f,W,0.9f,W,bodyCol);           // torso
    addBox(0.f,1.9f,0.f,0.2f,0.2f,0.2f,headCol);     // head
    addBox(-W-0.15f,1.0f,0.f,0.12f,0.5f,0.12f,bodyCol); // left arm
    addBox(+W+0.15f,1.0f,0.f,0.12f,0.5f,0.12f,bodyCol); // right arm
    // Legs
    addBox(-0.15f,0.0f,0.f,0.12f,0.45f,0.12f,bodyCol);
    addBox(+0.15f,0.0f,0.f,0.12f,0.45f,0.12f,bodyCol);

    m_vb=VulkanBuffer::createDeviceLocal(ctx,verts.data(),verts.size()*sizeof(Vertex3D),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m_ib=VulkanBuffer::createDeviceLocal(ctx,idxs.data(), idxs.size()*sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    m_idxCount=(uint32_t)idxs.size();
}

void NPCShop::destroy(VkDevice dev) {
    m_vb.destroy(dev); m_ib.destroy(dev);
}

void NPCShop::update(float dt, const glm::vec3& playerPos, const DayNight& time,
                     bool diamondsUnlocked, int diamondCount)
{
    // ── Schedule: 6 game hours at location, 2 game hours away ───────────────
    // 1 real second = 1 game minute, so 6 hours = 360 real seconds
    if (!m_isAway) {
        glm::vec3 diff = m_targetPos - m_currentPos;
        float dist = glm::length(diff);
        if (dist > 0.05f)
            m_currentPos += glm::normalize(diff) * std::min(dist, 1.2f * dt);

        m_stayTimer += dt;
        if (m_stayTimer >= STAY_DURATION) {
            m_isAway    = true;
            m_stayTimer = 0.f;
            m_awayTimer = 0.f;
            m_shopOpen  = false; // boot player out if browsing when NPC leaves
        }
    } else {
        // Away — reappear at new location after AWAY_DURATION
        m_awayTimer += dt;
        if (m_awayTimer >= AWAY_DURATION) {
            m_isAway      = false;
            m_awayTimer   = 0.f;
            m_locationIdx = (m_locationIdx + 1) % 3;
            m_targetPos   = LOCATIONS[m_locationIdx].pos;
            m_currentPos  = m_targetPos + glm::vec3(0.f, 0.f, 5.f);
            pickNewName();
        }
    }

    refreshStock(time, diamondsUnlocked, diamondCount);
    updateDialogue(diamondsUnlocked);

    // No interaction while away
    if (m_isAway) { m_playerNear = false; return; }

    // Proximity check + visit counter
    float dx=playerPos.x-m_currentPos.x, dz=playerPos.z-m_currentPos.z;
    bool wasNear = m_playerNear;
    m_playerNear = (dx*dx + dz*dz <= INTERACT_R*INTERACT_R);
    if (m_playerNear && !wasNear) m_visitCount++;
    // Update facing direction — always look toward player when they're nearby,
    // otherwise keep last known direction (looks more natural than snapping to 0,0,1)
    float pLen = sqrtf(dx*dx + dz*dz);
    if (pLen > 0.1f)
        m_lastPlayerDir = {dx/pLen, 0.f, dz/pLen};
}

double NPCShop::effectivePrice(double base) const {
    return (m_visitCount >= 3) ? base * (1.0 - LOYALTY_DISCOUNT) : base;
}

void NPCShop::refreshStock(const DayNight& time, bool diamondsUnlocked, int diamondCount) {
    bool night = time.isNight();
    // Whether the player can actually access diamond items:
    // Must have diamonds unlocked AND at least 1 diamond to spend.
    // "You found the gems but haven't earned one yet" is a fair gate.
    bool canUseDiamonds = diamondsUnlocked && diamondCount > 0;

    m_stock.clear();

    // ── Always available ──────────────────────────────────────────────────
    m_stock.push_back({"Physics Cube",   "A fresh spinning cube. Yours.",
        effectivePrice(1000), 0, ItemType::None, -1});
    m_stock.push_back({"Bouncy Ball",    "Defies gravity enthusiastically.",
        effectivePrice(10000), 0, ItemType::None, -1});
    m_stock.push_back({"Speed Boost",    "Go fast. Very fast. Suspiciously fast.",
        effectivePrice(500), 0, ItemType::SpeedBoost, 3});
    m_stock.push_back({"Magnet Pulse",   "Pull things toward you, rudely.",
        effectivePrice(800), 0, ItemType::MagnetPulse, 3});
    m_stock.push_back({"Time Freeze",    "5 seconds of 'what just happened'.",
        effectivePrice(2500), 0, ItemType::FreezeTime, 2});
    m_stock.push_back({"Coin Shower",    "Cash falls from the sky. Literally.",
        effectivePrice(5000), 0, ItemType::CoinShower, 2});

    // ── Night-only (sketchy merchandise, naturally sold after dark) ───────
    if (night) {
        m_stock.push_back({"Black Hole",   "Vendor accepts no liability.",
            effectivePrice(8000), 0, ItemType::BlackHole, 1});
        m_stock.push_back({"Earthquake",   "Structural damage not included.",
            effectivePrice(4500), 0, ItemType::Earthquake, 1});
    }

    // ── Diamond-gated items (must have gems unlocked AND ≥1 in wallet) ────
    if (canUseDiamonds) {
        m_stock.push_back({"Cube Launcher",  "5x throw force. Completely safe.",
            effectivePrice(200), 1, ItemType::CubeLauncher, 5});
        m_stock.push_back({"Diamond Pulse",  "See what you shouldn't for 8s.",
            effectivePrice(300), 1, ItemType::DiamondPulse, 3});
        m_stock.push_back({"Gravity Flip",   "Physics is a suggestion.",
            effectivePrice(400), 1, ItemType::GravityFlip, 5});
    } else if (diamondsUnlocked && diamondCount == 0) {
        // They have access but spent all diamonds — tease them
        m_stock.push_back({"[Diamond items]", "You're broke in diamonds. Earn some.",
            0, 0, ItemType::None, 0}); // stockLeft=0 means can't buy
    }
}

void NPCShop::updateDialogue(bool diamondsUnlocked) {
    // Dialogue reacts to state and visit count.
    // The NPC is self-aware and has no patience for people who just window-shop.
    if (m_visitCount == 0) {
        m_dialogue = "Hi. I'm " + m_npcName + ". Buy something.";
    } else if (m_visitCount == 1) {
        m_dialogue = m_npcName + " nods. 'Back already.'";
    } else if (m_visitCount == 2) {
        m_dialogue = "You're becoming a regular. I still don't know why.";
    } else if (m_visitCount >= 3 && !diamondsUnlocked) {
        m_dialogue = m_npcName + " leans in: 'Discount applied. Find those objects.'";
    } else if (m_visitCount >= 3 && diamondsUnlocked) {
        m_dialogue = "Diamonds, eh? " + m_npcName + " is impressed. Slightly.";
    } else {
        m_dialogue = m_npcName + " watches you browse in silence.";
    }
}

bool NPCShop::tryBuy(int index, double& money, int& diamonds,
                      ItemType& grantedItem, std::string& outMsg)
{
    if (index < 0 || index >= (int)m_stock.size()) return false;
    auto& item = m_stock[index];

    if (item.stockLeft == 0) {
        outMsg = "Out of stock. " + m_npcName + " shrugs."; return false;
    }
    if (money < item.priceMoney) {
        char buf[64]; snprintf(buf,sizeof(buf),"Need $%.0f more.",item.priceMoney-money);
        outMsg = std::string(buf) + " Push the cube harder."; return false;
    }
    if (item.priceDiamonds > 0 && diamonds < item.priceDiamonds) {
        outMsg = "Not enough diamonds. Keep looking."; return false;
    }

    money    -= item.priceMoney;
    diamonds -= item.priceDiamonds;
    grantedItem = item.grantItem;
    if (item.stockLeft > 0) item.stockLeft--;

    outMsg = "Sold. " + m_npcName + " pockets the payment wordlessly.";
    return true;
}

void NPCShop::draw(VulkanRenderer& renderer, bool shadow) {
    if (!m_vb.buffer || m_isAway) return; // gone fishin'
    float yaw = atan2f(m_lastPlayerDir.x, m_lastPlayerDir.z);
    glm::mat4 world = glm::translate(glm::mat4(1.0f), m_currentPos)
                    * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.f,1.f,0.f));
    renderer.drawMesh(m_vb.buffer, m_ib.buffer, m_idxCount, world, shadow);
}
