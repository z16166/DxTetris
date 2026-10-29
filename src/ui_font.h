#pragma once
// ui_font.h — bitmap-font text renderer via engine SpriteBatch (MINE, final).
// Consumes generated header static data (kFontGlyphs/kFontCount/kFontLineSpacing +
// atlas regions); no runtime JSON. All D3D objects COM/RAII via engine members.
#include "core.h"
#include <string>

namespace tetris
{

class Engine; // owns SpriteBatch + textures + fonts (engine.h)

class UiFont
{
public:
    explicit UiFont(Engine& engine) : m_engine(&engine) {}
    void Begin(); // sprite batch begin (default sort; draws must be between Begin/End)
    void End();
    // fontId = stable hash id (generated data); draws in px (top-left origin, device-space pixels).
    // scale multiplies the generated glyph metrics; centerW (>0) centres the run inside that screen-space width.
    void DrawText(int fontId, const wchar_t* s, float x, float y, Math::Color c, float scale = 1.f,
                  float centerW = 0.f);
    void DrawTextA(int fontId, const char* s, float x, float y, Math::Color c, float scale = 1.f,
                   float centerW = 0.f);
    Math::Vector2 TextWidth(int fontId, const wchar_t* s, float scale = 1.f) const;
    Math::Vector2 TextWidthA(int fontId, const char* s, float scale = 1.f) const;
    void FillRect(float x, float y, float w, float h, Math::Color c); // solid quad (board/pieces)
    static constexpr int kDefaultFont = (int)HashFnv32("font");       // stable id contract (single font)
private:
    Engine* m_engine;
};

} // namespace tetris
