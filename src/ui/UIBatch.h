#pragma once
// UIBatch.h — 2D UI vertex batching. Solid quads first, text quads after.
// Solids and text are separated so the renderer can switch PSO mode once.

#include "../Common.h"
#include "Font.h"
#include <vector>
#include <string>

class UIBatch {
public:
    void begin(uint32_t screenW, uint32_t screenH) {
        m_w = screenW; m_h = screenH;
        m_solidVerts.clear(); m_textVerts.clear();
    }

    // Solid colored rectangle
    void rect(float x, float y, float w, float h,
              float r, float g, float b, float a)
    {
        float x0=ndcX(x), x1=ndcX(x+w), y0=ndcY(y), y1=ndcY(y+h);
        glm::vec4 c{r,g,b,a};
        // Two triangles, CCW winding
        m_solidVerts.push_back({{x0,y0},{0,0},c});
        m_solidVerts.push_back({{x1,y0},{1,0},c});
        m_solidVerts.push_back({{x0,y1},{0,1},c});
        m_solidVerts.push_back({{x1,y0},{1,0},c});
        m_solidVerts.push_back({{x1,y1},{1,1},c});
        m_solidVerts.push_back({{x0,y1},{0,1},c});
    }

    // Render text string
    void str(float px, float py, float sc,
             const char* text,
             float r, float g, float b, float a,
             float sp = 1.0f)
    {
        float cw = GLYPH_PX * sc, cx = px;
        glm::vec4 c{r,g,b,a};
        for (const char* p = text; *p; p++) {
            int id = (*p < 32 || *p > 126) ? 0 : (*p - 32);
            float u0 = (float)(id % ATLAS_COLS) / ATLAS_COLS;
            float u1 = (float)(id % ATLAS_COLS + 1) / ATLAS_COLS;
            float v0 = (float)(id / ATLAS_COLS) / ATLAS_ROWS;
            float v1 = (float)(id / ATLAS_COLS + 1) / ATLAS_ROWS;
            float x0=ndcX(cx), x1=ndcX(cx+cw), y0=ndcY(py), y1=ndcY(py+GLYPH_PX*sc);
            m_textVerts.push_back({{x0,y0},{u0,v0},c});
            m_textVerts.push_back({{x1,y0},{u1,v0},c});
            m_textVerts.push_back({{x0,y1},{u0,v1},c});
            m_textVerts.push_back({{x1,y0},{u1,v0},c});
            m_textVerts.push_back({{x1,y1},{u1,v1},c});
            m_textVerts.push_back({{x0,y1},{u0,v1},c});
            cx += cw + sp;
        }
    }

    // Diamond/rhombus shape — for currency icons and other HUD decorations.
    // (cx,cy) = center, hw = half-width, hh = half-height
    void diaSolid(float cx, float cy, float hw, float hh,
                  float r, float g, float b, float a)
    {
        float x0=ndcX(cx-hw), x1=ndcX(cx), x2=ndcX(cx+hw);
        float y0=ndcY(cy-hh), y1=ndcY(cy), y2=ndcY(cy+hh);
        glm::vec4 c{r,g,b,a};
        // Upper triangle (top → right+left midpoints)
        m_solidVerts.push_back({{x1,y0},{0.5f,0.f},c});
        m_solidVerts.push_back({{x2,y1},{1.0f,0.5f},c});
        m_solidVerts.push_back({{x0,y1},{0.0f,0.5f},c});
        // Lower triangle (right+left midpoints → bottom)
        m_solidVerts.push_back({{x0,y1},{0.0f,0.5f},c});
        m_solidVerts.push_back({{x2,y1},{1.0f,0.5f},c});
        m_solidVerts.push_back({{x1,y2},{0.5f,1.0f},c});
    }

    float strW(float sc, const char* s, float sp = 1.0f) const {
        int n = (int)strlen(s);
        if (!n) return 0.0f;
        return n * GLYPH_PX * sc + (n - 1) * sp;
    }

    // Merge: solids first, then text — single upload buffer
    std::vector<VertexUI> vertices() const {
        std::vector<VertexUI> out = m_solidVerts;
        out.insert(out.end(), m_textVerts.begin(), m_textVerts.end());
        return out;
    }
    uint32_t solidCount() const { return (uint32_t)m_solidVerts.size(); }
    uint32_t textCount()  const { return (uint32_t)m_textVerts.size(); }
    uint32_t totalVerts() const { return solidCount() + textCount(); }
    void clear() { m_solidVerts.clear(); m_textVerts.clear(); }

private:
    float ndcX(float px) const { return  px / m_w * 2.0f - 1.0f; }
    float ndcY(float py) const { return (py / m_h * 2.0f - 1.0f); } // Vulkan: Y=-1 top, Y=+1 bottom

    std::vector<VertexUI> m_solidVerts;
    std::vector<VertexUI> m_textVerts;
    uint32_t m_w = 1280, m_h = 720;
};
