// Settings.cpp
#include "Settings.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <shlobj.h>

const SettingCat Settings::CATS[CAT_COUNT] = {
    { SHADOW,      "  Graphics"  },
    { AMBIENT,     "  Lighting"  },  // the setting category nobody asked for but everyone benefits from
    { RESOLUTION,  "  Display"   },
    { PAUSE_FOCUS, "  Gameplay"  },
};

void Settings::initResLabels() {
    ResEntry base[RES_COUNT] = {
        {1920,1200,"16:10",{}},{1920,1080,"16:9", {}},{1680,1050,"16:10",{}},
        {1600, 900,"16:9", {}},{1440, 900,"16:10",{}},{1366, 768,"16:9", {}},
        {1280, 800,"16:10",{}},{1280, 720,"16:9", {}},{1024, 768,"4:3",  {}},
        {1024, 576,"16:9", {}},{ 960, 540,"16:9", {}},{ 800, 600,"4:3",  {}},
    };
    for (int i = 0; i < RES_COUNT; i++) {
        resolutions[i] = base[i];
        snprintf(resolutions[i].label, sizeof(resolutions[i].label),
            "%ux%u (%s)", base[i].w, base[i].h, base[i].aspect);
    }
}

void Settings::Init() {
    initResLabels();

    e[SHADOW]        = { SettingType::Dropdown, "Shadow Quality",    2, 4, {"Low","Medium","High","Ultra"} };
    e[VSYNC]         = { SettingType::Dropdown, "VSync",             0, 2, {"On","Off"} };
    e[FOV]           = { SettingType::Slider,   "Field of View",     0, 0, {}, 75.f, 30.f, 120.f, 1.f, "deg" };
    e[FOG]           = { SettingType::Dropdown, "Fog Density",       1, 3, {"Off","Light","Heavy"} };
    e[VK_VER]        = { SettingType::Dropdown, "Vulkan Version",    0, 3, {"1.3","1.2","1.1"} };
    e[RENDER_DIST]   = { SettingType::Slider,   "Render Distance",   0, 0, {}, 300.f, 50.f, 2000.f, 50.f, "m" };
    // Lighting — finally some sliders that VISIBLY DO SOMETHING
    e[AMBIENT]       = { SettingType::Slider,   "Ambient Light",     0, 0, {}, 0.35f, 0.05f, 1.0f, 0.05f, "" };
    e[SUN_INTENSITY] = { SettingType::Slider,   "Sun Intensity",     0, 0, {}, 1.0f,  0.1f,  2.0f, 0.1f,  "" };
    e[GAMMA]         = { SettingType::Slider,   "Gamma",             0, 0, {}, 2.2f,  1.0f,  3.0f, 0.1f,  "" };
    e[RESOLUTION]  = { SettingType::Dropdown, "Resolution",        0, RES_COUNT, {} };
    for (int i = 0; i < RES_COUNT; i++) e[RESOLUTION].opts[i] = resolutions[i].label;
    e[WIN_MODE]    = { SettingType::Dropdown, "Window Mode",       0, 3, {"Windowed","Borderless","Fullscreen"} };
    e[PAUSE_FOCUS] = { SettingType::Dropdown, "Pause on Focus Loss", 0, 2, {"On","Off"} };

    // Default resolution = closest match to desktop
    int dw = GetSystemMetrics(SM_CXSCREEN), dh = GetSystemMetrics(SM_CYSCREEN);
    int best = 0; long long bd = LLONG_MAX;
    for (int i = 0; i < RES_COUNT; i++) {
        long long d = std::abs((long long)dw*dh - (long long)resolutions[i].w*resolutions[i].h);
        if (d < bd) { bd = d; best = i; }
    }
    e[RESOLUTION].cur = best;
    for (int i = 0; i < COUNT; i++) pend[i] = e[i].cur;
}

void Settings::Step(int row, int dir) {
    auto& s = e[row];
    if (s.type == SettingType::Slider)
        s.sv = std::max(s.smin, std::min(s.smax, s.sv + dir * s.sstep));
    else
        pend[row] = std::max(0, std::min(s.count - 1, pend[row] + dir));
}
void Settings::Commit() { for (int i = 0; i < COUNT; i++) e[i].cur = pend[i]; }
void Settings::Reset()  { for (int i = 0; i < COUNT; i++) pend[i] = e[i].cur; }

// ── Accessors ─────────────────────────────────────────────────────────────
uint32_t Settings::ShadowDim() const {
    static uint32_t d[] = { 512, 1024, 2048, 4096 };
    return d[std::min(e[SHADOW].cur, 3)];
}
float Settings::FogDensity() const {
    static float t[] = { 0.f, 0.18f, 0.45f };
    return t[std::min(e[FOG].cur, 2)];
}

// ── File paths ────────────────────────────────────────────────────────────
std::string Settings::Dir() {
    char d[MAX_PATH] = {};
    SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, d);
    return std::string(d) + "\\VkScene\\";
}
std::string Settings::Path() { return Dir() + "settings.xml"; }

// ── Save — full XML names, true/false booleans ────────────────────────────
void Settings::Save() const {
    CreateDirectoryA(Dir().c_str(), nullptr);
    std::ofstream f(Path());
    if (!f.is_open()) { LOG_WARN("Could not save settings.xml"); return; }
    auto wb = [](bool v) { return v ? "true" : "false"; };
    f << "<?xml version=\"1.0\"?>\n<VkScene>\n";
    f << "  <ShadowQuality>"    << e[SHADOW].opts[e[SHADOW].cur]     << "</ShadowQuality>\n";
    f << "  <VSync>"            << wb(e[VSYNC].cur == 0)             << "</VSync>\n";
    f << "  <FieldOfView>"      << (int)e[FOV].sv                    << "</FieldOfView>\n";
    f << "  <FogDensity>"       << e[FOG].opts[e[FOG].cur]           << "</FogDensity>\n";
    f << "  <VulkanVersion>"    << e[VK_VER].opts[e[VK_VER].cur]     << "</VulkanVersion>\n";
    f << "  <RenderDistance>"   << (int)e[RENDER_DIST].sv            << "</RenderDistance>\n";
    f << "  <AmbientLight>"     << e[AMBIENT].sv                     << "</AmbientLight>\n";
    f << "  <SunIntensity>"     << e[SUN_INTENSITY].sv               << "</SunIntensity>\n";
    f << "  <Gamma>"            << e[GAMMA].sv                       << "</Gamma>\n";
    f << "  <Resolution>"       << resolutions[e[RESOLUTION].cur].w
                                << "x"
                                << resolutions[e[RESOLUTION].cur].h  << "</Resolution>\n";
    f << "  <WindowMode>"       << e[WIN_MODE].opts[e[WIN_MODE].cur] << "</WindowMode>\n";
    f << "  <PauseOnFocusLoss>" << wb(e[PAUSE_FOCUS].cur == 0)       << "</PauseOnFocusLoss>\n";
    f << "</VkScene>\n";
}

// ── Load — case-insensitive, graceful on missing keys ─────────────────────
int Settings::FindOpt(const SettingEntry& s, const std::string& val) {
    for (int i = 0; i < s.count; i++)
        if (s.opts[i] && _stricmp(s.opts[i], val.c_str()) == 0) return i;
    return -1;
}

void Settings::Load() {
    std::ifstream f(Path());
    if (!f.is_open()) return;

    // Pull value string between <tag> and </tag>
    auto rv = [](const std::string& line, const char* tag, std::string& out) -> bool {
        std::string o = std::string("<") + tag + ">",
                    c = std::string("</") + tag + ">";
        auto p = line.find(o);
        if (p == std::string::npos) return false;
        p += o.size();
        auto q = line.find(c, p);
        if (q == std::string::npos) return false;
        out = line.substr(p, q - p);
        return true;
    };

    std::string line, val;
    while (std::getline(f, line)) {
        if (rv(line, "ShadowQuality",    val)) { int x=FindOpt(e[SHADOW],   val); if(x>=0) e[SHADOW].cur=x; }
        if (rv(line, "VSync",            val)) { e[VSYNC].cur = (_stricmp(val.c_str(),"true")==0)?0:1; }
        if (rv(line, "FieldOfView",      val)) { float v=stof(val); e[FOV].sv=std::max(30.f,std::min(120.f,v)); }
        if (rv(line, "FogDensity",       val)) { int x=FindOpt(e[FOG],      val); if(x>=0) e[FOG].cur=x; }
        if (rv(line, "VulkanVersion",    val)) { int x=FindOpt(e[VK_VER],   val); if(x>=0) e[VK_VER].cur=x; }
        if (rv(line, "RenderDistance",   val)) { float v=stof(val); e[RENDER_DIST].sv=std::max(50.f,std::min(2000.f,v)); }
        if (rv(line, "AmbientLight",      val)) { float v=stof(val); e[AMBIENT].sv=std::max(0.05f,std::min(1.f,v)); }
        if (rv(line, "SunIntensity",      val)) { float v=stof(val); e[SUN_INTENSITY].sv=std::max(0.1f,std::min(2.f,v)); }
        if (rv(line, "Gamma",             val)) { float v=stof(val); e[GAMMA].sv=std::max(1.f,std::min(3.f,v)); }
        if (rv(line, "Resolution",       val)) {
            int rw=0, rh=0;
            sscanf_s(val.c_str(), "%dx%d", &rw, &rh);
            for (int i=0;i<RES_COUNT;i++)
                if ((int)resolutions[i].w==rw&&(int)resolutions[i].h==rh) { e[RESOLUTION].cur=i; break; }
        }
        if (rv(line, "WindowMode",       val)) { int x=FindOpt(e[WIN_MODE],   val); if(x>=0) e[WIN_MODE].cur=x; }
        if (rv(line, "PauseOnFocusLoss", val)) { e[PAUSE_FOCUS].cur=(_stricmp(val.c_str(),"true")==0)?0:1; }
    }
    for (int i = 0; i < COUNT; i++) pend[i] = e[i].cur;
}
