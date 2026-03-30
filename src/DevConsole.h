#pragma once
// DevConsole.h — A developer console that actually works.
// Supports reading and writing named float/int/bool variables at runtime.
// Uses C++ assignment syntax: "gravity = 9.8" or "player.energy = 100"
// OutputDebugString integration so it shows in VS Output window too.
// Toggle with Shift+F1 (debug panel) or ` key if enabled.

#include "Common.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <variant>

// ── Log entry ─────────────────────────────────────────────────────────────
enum class ConLevel { Info, Warn, Error, Command, Result };

struct ConEntry {
    ConLevel    level;
    std::string text;
    float       timestamp; // totalTime when logged
};

// ── Variable binding (supports float*, int*, bool* with optional bounds) ──
struct ConVar {
    std::string name;
    std::string description;
    // Pointer to the actual variable — no copies, writes go live immediately.
    // Using variant so we can store different types cleanly.
    std::variant<float*, int*, bool*> ptr;
    float minVal = -1e9f, maxVal = 1e9f; // only used for float/int
};

class DevConsole {
public:
    static constexpr int MAX_ENTRIES = 200; // scrollback limit
    static constexpr int MAX_INPUT   = 256;

    DevConsole();

    // ── Registration ──────────────────────────────────────────────────────
    void registerFloat(const std::string& name, float* ptr, const std::string& desc = "",
                       float minV = -1e9f, float maxV = 1e9f);
    void registerInt  (const std::string& name, int*   ptr, const std::string& desc = "",
                       int   minV = INT_MIN, int   maxV = INT_MAX);
    void registerBool (const std::string& name, bool*  ptr, const std::string& desc = "");

    // Register a command that executes a callback (no variable involved)
    void registerCmd  (const std::string& name,
                       std::function<std::string(const std::string& args)> fn,
                       const std::string& desc = "");

    // ── Logging (replaces OutputDebugStringA for in-game visibility) ──────
    void log  (const std::string& msg, float timestamp = 0.f);
    void warn  (const std::string& msg, float timestamp = 0.f);
    void error (const std::string& msg, float timestamp = 0.f);

    // ── Input processing ──────────────────────────────────────────────────
    // Feed characters from WM_CHAR; returns true if console is open.
    bool handleChar(char c);
    // Feed VK key from WM_KEYDOWN (for backspace, enter, arrow keys).
    bool handleKey(WPARAM vk);

    bool isOpen()   const { return m_open; }
    void toggle()         { m_open = !m_open; m_inputBuf[0] = '\0'; m_inputLen = 0; m_cursorPos = 0; m_inputActive = false; }
    void close()          { m_open = false; }

    // ── Draw (called from Game's UI draw path) ────────────────────────────
    // b is the UIBatch. W/H are screen dimensions.
    void draw(struct UIBatch& b, uint32_t W, uint32_t H, float totalTime);

    // ── Access to entries for external rendering ──────────────────────────
    const std::vector<ConEntry>& entries() const { return m_entries; }

private:
    void submitInput();
    void executeCommand(const std::string& input);
    std::string getVarInfo(const std::string& name) const;
    void addEntry(ConLevel level, const std::string& text, float ts);

    bool m_open = false;
    std::vector<ConEntry> m_entries;
    std::unordered_map<std::string, ConVar> m_vars;
    std::unordered_map<std::string,
        std::pair<std::function<std::string(const std::string&)>, std::string>> m_cmds;

    char  m_inputBuf[MAX_INPUT] = {};
    int   m_inputLen = 0;
    int   m_cursorPos = 0;
    bool  m_inputActive = false;
    int   m_scrollOffset = 0; // lines scrolled up from bottom
    int   m_historyIdx   = -1;
    std::vector<std::string> m_history; // command history (up arrow)

    // Autocomplete state
    std::string m_lastTabInput;
    int         m_tabCycle = 0;
};
