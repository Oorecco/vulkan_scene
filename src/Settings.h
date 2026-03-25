#pragma once
// Settings.h — Everything the player can tweak. Saved to XML with human-readable names.
// "Shadow=2" is for robots. "ShadowQuality=High" is for people.

#include "Common.h"
#include <string>
#include <array>

enum class SettingType { Dropdown, Slider };

struct SettingEntry {
    SettingType type  = SettingType::Dropdown;
    const char* name  = "";
    int  cur = 0, count = 0;
    std::array<const char*, 12> opts = {};
    float sv = 0, smin = 0, smax = 1, sstep = 1;
    const char* sunit = "";
};

struct SettingCat { int beforeRow; const char* label; };

struct ResEntry {
    uint32_t w = 0, h = 0;
    const char* aspect = "";
    char label[32] = {};
};
static constexpr int RES_COUNT = 12;

class Settings {
public:
    enum Idx {
        // Graphics — "make it look good, or at least try"
        SHADOW=0, VSYNC, FOV, FOG, VK_VER, RENDER_DIST,
        // Lighting — because it looked too dark at 3am
        AMBIENT, SUN_INTENSITY, GAMMA,
        // Display
        RESOLUTION, WIN_MODE,
        // Gameplay — yes, "pause on focus loss" is gameplay, don't @ me
        PAUSE_FOCUS,
        COUNT
    };
    static constexpr int CAT_COUNT = 4;  // added Lighting category
    static const SettingCat CATS[CAT_COUNT];

    SettingEntry e[COUNT];
    int  pend[COUNT] = {};
    int  selRow      = 0;
    std::array<ResEntry, RES_COUNT> resolutions;

    // Hold-repeat for slider nav (0=up 1=dn 2=lt 3=rt)
    float navHold[4] = {};
    bool  navHeld[4] = {};

    // ── Accessors ─────────────────────────────────────────────────────────
    uint32_t ShadowDim()     const;
    bool     VSync()         const { return e[VSYNC].cur == 0; }
    float    FOV_()          const { return e[FOV].sv; }
    float    FogDensity()    const;
    float    FarZ()          const { return e[RENDER_DIST].sv; }
    int      VkVerIdx()      const { return e[VK_VER].cur; }
    ResEntry Res()           const { return resolutions[e[RESOLUTION].cur]; }
    WinMode  WMode()         const { return (WinMode)e[WIN_MODE].cur; }
    bool     PauseFocus()    const { return e[PAUSE_FOCUS].cur == 0; }
    // Lighting accessors — these actually do something visible, unlike FOV
    float    AmbientLevel()  const { return e[AMBIENT].sv; }
    float    SunIntensity()  const { return e[SUN_INTENSITY].sv; }
    float    Gamma()         const { return e[GAMMA].sv; }

    void Init();
    void Step(int row, int dir);
    void Commit();
    void Reset();

    static std::string Dir();
    static std::string Path();
    void Save() const;
    void Load();

private:
    static int FindOpt(const SettingEntry& s, const std::string& val);
    void initResLabels();
};
