// ui_font.cpp — bitmap-font renderer (MINE, final; consumes generated header data).
#include "ui_font.h"
#include "engine.h"
#include "assets.h" // generated header (single source of truth)
#include <cstring>
#include <vector>

using namespace DirectX;

namespace tetris
{

void UiFont::Begin()
{
    // Straight-alpha blending: DirectXTK's AlphaBlend() is the *premultiplied* (ONE, INV_SRC_ALPHA)
    // state and would leave translucent fills (ghost piece, pause dimmer) far too bright.
    m_engine->GetSpriteBatch()->Begin(SpriteSortMode_Deferred, m_engine->GetCommonStates()->NonPremultiplied());
}

void UiFont::End()
{
    m_engine->GetSpriteBatch()->End();
}

void UiFont::FillRect(float x, float y, float w, float h, Math::Color c)
{
    RECT r{(LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h)};
    m_engine->GetSpriteBatch()->Draw(m_engine->GetWhiteTexture(), r, c);
}

void UiFont::DrawText(int fontId, const wchar_t* s, float x, float y, Math::Color c, float scale, float centerW)
{
    auto batch = m_engine->GetSpriteBatch();
    const auto* font = m_engine->GetFont(fontId); // DirectXTK SpriteFont from generated kFontGlyphs
    float cx = x;
    if (centerW > 0.f) cx = x + (centerW - TextWidth(fontId, s, scale).x) * 0.5f;

    // DirectXTK SpriteFont::DrawString — layout via generated metrics (px-accurate)
    font->DrawString(batch, s, Math::Vector2(cx, y), c, 0.f, Math::Vector2::Zero, scale);
}

void UiFont::DrawTextA(int fontId, const char* s, float x, float y, Math::Color c, float scale, float centerW)
{
    wchar_t buf[512];
    int i = 0;
    for (; *s && i < 511; ++s)
        buf[i++] = (wchar_t)(unsigned char)*s;
    buf[i] = 0;

    DrawText(fontId, buf, x, y, c, scale, centerW);
}

Math::Vector2 UiFont::TextWidth(int fontId, const wchar_t* s, float scale) const
{
    const auto* font = m_engine->GetFont(fontId);
    float w = 0.f;
    const wchar_t* p = s;

    while (*p)
    {
        const auto* g = font->FindGlyph(*p);
        // same advance SpriteFont::DrawString uses: ink width + the generated extra spacing
        if (g) w += float(g->Subrect.right - g->Subrect.left) + g->XAdvance;
        ++p;
    }

    return Math::Vector2(w * scale, 0.f);
}

Math::Vector2 UiFont::TextWidthA(int fontId, const char* s, float scale) const
{
    wchar_t buf[512];
    int i = 0;
    for (; *s && i < 511; ++s)
        buf[i++] = (wchar_t)(unsigned char)*s;
    buf[i] = 0;

    return TextWidth(fontId, buf, scale);
}

} // namespace tetris
