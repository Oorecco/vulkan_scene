// DevConsole.cpp — The console that judges your game decisions in real time.
// Syntax: "varname = value" to set, "varname" to read, "help" to list all.
// "clear" clears the log. "quit" closes it. Arrow keys = history.
#include "DevConsole.h"
#include "ui/UIBatch.h"
#include "ui/Font.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>

static std::string trim(const std::string& s) {
    size_t a=s.find_first_not_of(" \t"),b=s.find_last_not_of(" \t");
    return (a==std::string::npos)?"":s.substr(a,b-a+1);
}
static std::string toLower(std::string s) {
    for (auto& c:s) c=(char)tolower((unsigned char)c); return s;
}

DevConsole::DevConsole() {
    addEntry(ConLevel::Info, "DevConsole ready. Type 'help' for commands.", 0.f);
    addEntry(ConLevel::Info, "Syntax: varname = value  |  varname  |  help  |  clear", 0.f);
}

// ── Registration ──────────────────────────────────────────────────────────
void DevConsole::registerFloat(const std::string& n, float* p, const std::string& d, float mn, float mx) {
    m_vars[toLower(n)] = {n, d, p, mn, mx};
}
void DevConsole::registerInt(const std::string& n, int* p, const std::string& d, int mn, int mx) {
    ConVar v; v.name=n; v.description=d; v.ptr=p; v.minVal=(float)mn; v.maxVal=(float)mx;
    m_vars[toLower(n)] = v;
}
void DevConsole::registerBool(const std::string& n, bool* p, const std::string& d) {
    ConVar v; v.name=n; v.description=d; v.ptr=p;
    m_vars[toLower(n)] = v;
}
void DevConsole::registerCmd(const std::string& n,
                              std::function<std::string(const std::string&)> fn,
                              const std::string& d) {
    m_cmds[toLower(n)] = {fn, d};
}

// ── Logging ───────────────────────────────────────────────────────────────
void DevConsole::addEntry(ConLevel level, const std::string& text, float ts) {
    m_entries.push_back({level, text, ts});
    if ((int)m_entries.size() > MAX_ENTRIES)
        m_entries.erase(m_entries.begin());
    // Mirror to VS Output window — helpful when the game crashes before you can read it
    std::string prefix;
    switch(level){
    case ConLevel::Info:    prefix="[CON] "; break;
    case ConLevel::Warn:    prefix="[WARN] "; break;
    case ConLevel::Error:   prefix="[ERR] "; break;
    case ConLevel::Command: prefix="> "; break;
    case ConLevel::Result:  prefix="  "; break;
    }
    OutputDebugStringA((prefix+text+"\n").c_str());
}
void DevConsole::log  (const std::string& m,float ts){addEntry(ConLevel::Info,   m,ts);}
void DevConsole::warn  (const std::string& m,float ts){addEntry(ConLevel::Warn,   m,ts);}
void DevConsole::error (const std::string& m,float ts){addEntry(ConLevel::Error,  m,ts);}

// ── Input ─────────────────────────────────────────────────────────────────
bool DevConsole::handleChar(char c) {
    if (!m_open) return false;
    if (!m_inputActive) return true;
    if (c >= 32 && c < 127 && m_inputLen < MAX_INPUT-1) {
        for (int i = m_inputLen; i >= m_cursorPos; --i) m_inputBuf[i+1] = m_inputBuf[i];
        m_inputBuf[m_cursorPos++] = c;
        m_inputLen++;
    }
    return true;
}

bool DevConsole::handleKey(WPARAM vk) {
    if (!m_open) return false;
    if (!m_inputActive) {
        if (vk == VK_RETURN) { m_inputActive = true; return true; }
        if (vk == VK_ESCAPE) { close(); return true; }
        if (vk == VK_PRIOR) { m_scrollOffset += 3; return true; }
        if (vk == VK_NEXT)  { m_scrollOffset = std::max(0,m_scrollOffset-3); return true; }
        return true;
    }
    if (vk == VK_RETURN) {
        submitInput(); return true;
    }
    if (vk == VK_BACK && m_cursorPos > 0) {
        for (int i = m_cursorPos - 1; i < m_inputLen; ++i) m_inputBuf[i] = m_inputBuf[i+1];
        m_cursorPos--;
        m_inputLen--;
        return true;
    }
    if (vk == VK_DELETE && m_cursorPos < m_inputLen) {
        for (int i = m_cursorPos; i < m_inputLen; ++i) m_inputBuf[i] = m_inputBuf[i+1];
        m_inputLen--;
        return true;
    }
    if (vk == VK_LEFT) {
        m_cursorPos = std::max(0, m_cursorPos - 1); return true;
    }
    if (vk == VK_RIGHT) {
        m_cursorPos = std::min(m_inputLen, m_cursorPos + 1); return true;
    }
    if (vk == VK_HOME) { m_cursorPos = 0; return true; }
    if (vk == VK_END)  { m_cursorPos = m_inputLen; return true; }
    if (vk == VK_ESCAPE) {
        m_inputActive = false; return true;
    }
    if (vk == VK_UP && !m_history.empty()) {
        m_historyIdx = std::min((int)m_history.size()-1, m_historyIdx+1);
        std::string h=m_history[m_history.size()-1-m_historyIdx];
        strncpy_s(m_inputBuf,sizeof(m_inputBuf),h.c_str(),_TRUNCATE);
        m_inputLen=(int)strlen(m_inputBuf);
        m_cursorPos = m_inputLen;
        return true;
    }
    if (vk == VK_DOWN) {
        m_historyIdx=std::max(-1,m_historyIdx-1);
        if(m_historyIdx<0){m_inputBuf[0]='\0';m_inputLen=0;}
        else{std::string h=m_history[m_history.size()-1-m_historyIdx];strncpy_s(m_inputBuf,sizeof(m_inputBuf),h.c_str(),_TRUNCATE);m_inputLen=(int)strlen(m_inputBuf);}
        m_cursorPos = m_inputLen;
        return true;
    }
    if (vk == VK_PRIOR) { m_scrollOffset += 3; return true; } // Page up
    if (vk == VK_NEXT)  { m_scrollOffset = std::max(0,m_scrollOffset-3); return true; }
    if (vk == VK_TAB) {
        // Autocomplete: cycle through matching variable names
        std::string partial = toLower(std::string(m_inputBuf));
        std::vector<std::string> matches;
        for (auto& [k,v]:m_vars)  if (k.find(partial)==0) matches.push_back(v.name);
        for (auto& [k,p]:m_cmds)  if (k.find(partial)==0) matches.push_back(k);
        if (!matches.empty()){
            std::sort(matches.begin(),matches.end());
            int idx=m_tabCycle%=(int)matches.size();
            strncpy_s(m_inputBuf,sizeof(m_inputBuf),matches[idx].c_str(),_TRUNCATE);
            m_inputLen=(int)strlen(m_inputBuf);
            m_cursorPos = m_inputLen;
            m_tabCycle++;
        }
        return true;
    }
    m_tabCycle=0;
    return true;
}

void DevConsole::submitInput() {
    std::string input = trim(std::string(m_inputBuf));
    m_inputBuf[0]='\0'; m_inputLen=0; m_cursorPos=0; m_historyIdx=-1;
    if (input.empty()) return;
    m_history.push_back(input);
    if ((int)m_history.size()>64) m_history.erase(m_history.begin());
    addEntry(ConLevel::Command, input, 0.f);
    executeCommand(input);
    m_scrollOffset = 0; // snap to bottom on new command
}

void DevConsole::executeCommand(const std::string& input) {
    // ── Built-in commands ─────────────────────────────────────────────────
    std::string lower = toLower(trim(input));
    if (lower == "clear") { m_entries.clear(); return; }
    if (lower == "quit" || lower == "close") { close(); return; }
    if (lower == "help") {
        addEntry(ConLevel::Result, "=== Variables ===", 0);
        for (auto& [k,v]:m_vars) {
            std::string line = "  "+v.name;
            if (!v.description.empty()) line += " — "+v.description;
            addEntry(ConLevel::Result, line, 0);
        }
        addEntry(ConLevel::Result, "=== Commands ===", 0);
        for (auto& [k,p]:m_cmds) {
            std::string line = "  "+k;
            if (!p.second.empty()) line += " — "+p.second;
            addEntry(ConLevel::Result, line, 0);
        }
        return;
    }

    // ── Check registered commands ─────────────────────────────────────────
    {
        size_t sp = lower.find(' ');
        std::string cmdName  = sp==std::string::npos ? lower : lower.substr(0,sp);
        std::string cmdArgs  = sp==std::string::npos ? "" : trim(input.substr(sp+1));
        auto it = m_cmds.find(cmdName);
        if (it != m_cmds.end()) {
            std::string result = it->second.first(cmdArgs);
            if (!result.empty()) addEntry(ConLevel::Result, result, 0);
            return;
        }
    }

    // ── Parse "name = value" or "name" ────────────────────────────────────
    size_t eq = input.find('=');
    if (eq != std::string::npos) {
        std::string lhs = toLower(trim(input.substr(0, eq)));
        std::string rhs = trim(input.substr(eq+1));

        auto it = m_vars.find(lhs);
        if (it == m_vars.end()) {
            addEntry(ConLevel::Error, "Unknown variable: '" + lhs + "'. Type 'help' to list all.", 0);
            return;
        }
        ConVar& cv = it->second;
        try {
            std::visit([&](auto* ptr) {
                using T = std::remove_pointer_t<decltype(ptr)>;
                if constexpr (std::is_same_v<T, float>) {
                    float val = std::stof(rhs);
                    val = std::clamp(val, cv.minVal, cv.maxVal);
                    *ptr = val;
                    addEntry(ConLevel::Result, cv.name+" = "+std::to_string(val), 0);
                } else if constexpr (std::is_same_v<T, int>) {
                    int val = std::stoi(rhs);
                    val = std::clamp(val, (int)cv.minVal, (int)cv.maxVal);
                    *ptr = val;
                    addEntry(ConLevel::Result, cv.name+" = "+std::to_string(val), 0);
                } else if constexpr (std::is_same_v<T, bool>) {
                    std::string rv = toLower(trim(rhs));
                    bool val = (rv=="1"||rv=="true"||rv=="yes"||rv=="on");
                    *ptr = val;
                    addEntry(ConLevel::Result, cv.name+" = "+(val?"true":"false"), 0);
                }
            }, cv.ptr);
        } catch (...) {
            addEntry(ConLevel::Error, "Bad value '"+rhs+"' — must be a valid C++ literal.", 0);
        }
        return;
    }

    // ── Read variable ──────────────────────────────────────────────────────
    {
        std::string key = toLower(trim(input));
        auto it = m_vars.find(key);
        if (it != m_vars.end()) {
            addEntry(ConLevel::Result, getVarInfo(key), 0);
            return;
        }
    }

    addEntry(ConLevel::Error, "Unknown command: '"+input+"'. Type 'help'.", 0);
}

std::string DevConsole::getVarInfo(const std::string& name) const {
    auto it = m_vars.find(toLower(name));
    if (it == m_vars.end()) return "not found";
    const ConVar& cv = it->second;
    std::string val;
    std::visit([&](auto* ptr) {
        using T = std::remove_pointer_t<decltype(ptr)>;
        if constexpr (std::is_same_v<T, float>) val = std::to_string(*ptr);
        else if constexpr (std::is_same_v<T, int>)  val = std::to_string(*ptr);
        else if constexpr (std::is_same_v<T, bool>) val = (*ptr?"true":"false");
    }, cv.ptr);
    return cv.name+" = "+val+(cv.description.empty()?"":" ("+cv.description+")");
}

// ── Draw ──────────────────────────────────────────────────────────────────
void DevConsole::draw(UIBatch& b, uint32_t W, uint32_t H, float /*ts*/) {
    if (!m_open) return;
    float CW=(float)W, CH=(float)H;
    float consH = std::min(CH*0.5f, 360.f); // console takes up top half, max 360px
    float consY = CH - consH; // bottom-anchored

    // Background
    b.rect(0, consY, CW, consH, 0.04f, 0.04f, 0.06f, 0.95f);
    b.rect(0, consY, CW, 2,     0.28f, 0.72f, 1.0f,  0.80f);

    // Title
    const char* title = "[ DEVELOPER CONSOLE ]";
    b.str(6, consY+4, 1.2f, title, 0.28f, 0.72f, 1.0f, 0.9f);
    const char* hdrHint = m_inputActive ? "ESC=unfocus" : "Enter=focus";
    b.str(CW - b.strW(1.1f,hdrHint)-6, consY+5, 1.1f, hdrHint, 0.4f,0.4f,0.5f,0.6f);

    // Log area
    const float LH = 13.f, SC = 1.0f, PAD = 4.f;
    float logTop  = consY + 20.f;
    float inputH  = 22.f;
    float logH    = consH - 20.f - inputH - 4.f;
    int   maxLines= (int)(logH / LH);
    int   total   = (int)m_entries.size();
    int   start   = std::max(0, total - maxLines - m_scrollOffset);
    int   end2    = std::min(total, start + maxLines);

    float ty = logTop;
    for (int i = start; i < end2; i++) {
        auto& e = m_entries[i];
        float r,g,bc;
        switch(e.level){
        case ConLevel::Info:    r=0.7f; g=0.8f; bc=0.9f; break;
        case ConLevel::Warn:    r=1.0f; g=0.8f; bc=0.2f; break;
        case ConLevel::Error:   r=1.0f; g=0.3f; bc=0.3f; break;
        case ConLevel::Command: r=0.4f; g=0.9f; bc=0.5f; break;
        case ConLevel::Result:  r=0.8f; g=0.8f; bc=0.8f; break;
        default:                r=0.7f; g=0.7f; bc=0.7f;
        }
        b.str(PAD, ty, SC, e.text.c_str(), r,g,bc, 0.9f);
        ty += LH;
    }

    // Scroll hint
    if (m_scrollOffset > 0) {
        char sh[32]; snprintf(sh,sizeof(sh),"^ scrolled +%d",m_scrollOffset);
        b.str(CW-b.strW(1.0f,sh)-4, logTop, 1.0f, sh, 0.5f,0.5f,0.6f,0.6f);
    }

    // Input line
    float iy = consY + consH - inputH;
    b.rect(0, iy, CW, inputH, 0.08f, 0.08f, 0.12f, 0.95f);
    b.rect(0, iy, CW, 1,      0.3f,  0.6f,  1.0f,  0.5f);
    // Prompt symbol
    b.str(4, iy+5, 1.3f, ">", 0.3f, 0.8f, 0.5f, 1.0f);
    // Input text
    b.str(18, iy+5, 1.3f, m_inputBuf, 0.9f, 0.9f, 0.95f, 1.0f);
    // Blinking cursor
    std::string leftText(m_inputBuf, m_inputBuf + std::min(m_cursorPos, m_inputLen));
    float cursorX = 18 + b.strW(1.3f, leftText.c_str());
    float blink = fmodf((float)GetTickCount64() / 1000.0f, 1.0f);
    if (blink < 0.5f && m_inputActive)
        b.rect(cursorX, iy+5, 1.5f, GLYPH_PX*1.3f, 0.8f, 0.9f, 1.0f, 0.85f);
}
