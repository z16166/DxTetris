// main.cpp — window + WNDPROC + frame loop (MINE, final; no subagents).
// RAII: engine owns all D3D objects (COM members); game/ui own nothing raw.
#include "engine.h"
#include "game.h"
#include "ui_font.h"
#include <windows.h>
#include <imm.h>      // ImmDisableIME — keep the IME from eating game keys
#include <mmsystem.h> // timeBeginPeriod
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "imm32.lib")

using namespace tetris;

static Engine* g_engine = nullptr; // heap, constructed inside main's try (static-init chain must not throw)
static Game* g_game = nullptr;
static UiFont* g_ui = nullptr;
static HWND g_hwnd = nullptr;
static bool g_running = true;

// key-map (game.h Idx contract: L,R,CW,CCW,R180,SOFT,HOLD,HARD,START,PAUSE — see game.cpp Idx)
static InputSnapshot g_keys; // current frame snapshot (edge flags)
static bool s_keyDown[256];  // physical key state, updated from WM_KEYDOWN/WM_KEYUP (no polling API needed)
static bool s_edge[11];      // this frame's key-press edges (Idx order)

static void PumpInput()
{
    g_keys.anyEdge = false;
    static bool prev[11]; // L,R,CW,CCW,R180,SOFT,HOLD,HARD,START,PAUSE,SS (Idx order; game.h contract)
    const int MAP[11] = {
        VK_LEFT, VK_RIGHT, 'X', 'Z',       VK_SPACE, VK_DOWN,
        'E',     VK_UP,    'Y', VK_ESCAPE, 'P'}; // CW='X' CCW='Z' R180=SPACE SOFT=DOWN HOLD='E' HARD=UP START='Y' PAUSE=ESC SS='P'

    for (int i = 0; i < 11; ++i)
    {
        bool was = prev[i];
        prev[i] = s_keyDown[MAP[i] & 0xFF] != 0;
        g_keys.p[i] = prev[i];
        s_edge[i] = prev[i] && !was;
        if (s_edge[i]) g_keys.anyEdge = true;
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CLOSE: g_running = false; return 0;
    case WM_DESTROY: return 0;

    case WM_ERASEBKGND: return 1; // we repaint every frame; skipping the erase avoids resize flicker
    case WM_SIZE:
        if (g_engine && wp != SIZE_MINIMIZED && LOWORD(lp) > 0 && HIWORD(lp) > 0)
            g_engine->Resize(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_GETMINMAXINFO: // keep the UI readable at the smallest useful size
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        RECT rc{0, 0, 720, 600};
        AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
        mmi->ptMinTrackSize.x = rc.right - rc.left;
        mmi->ptMinTrackSize.y = rc.bottom - rc.top;
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wp < 256) s_keyDown[wp] = true;
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wp < 256) s_keyDown[wp] = false;
        return 0;
    case WM_KILLFOCUS:
        memset(s_keyDown, 0, sizeof s_keyDown); // focus loss must not leave keys stuck down
        return 0;
    default: return DefWindowProcW(h, msg, wp, lp); // explicit W: without UNICODE the macro maps to the ANSI thunk
    }
}

static Math::Color PieceColor(Piece p)
{
    switch (p)
    {
    case Piece::I: return Math::Color(0.20f, 0.85f, 0.90f, 1.f);
    case Piece::O: return Math::Color(0.95f, 0.85f, 0.20f, 1.f);
    case Piece::T: return Math::Color(0.70f, 0.35f, 0.90f, 1.f);
    case Piece::S: return Math::Color(0.35f, 0.85f, 0.40f, 1.f);
    case Piece::Z: return Math::Color(0.90f, 0.30f, 0.30f, 1.f);
    case Piece::J: return Math::Color(0.30f, 0.45f, 0.90f, 1.f);
    case Piece::L: return Math::Color(0.95f, 0.60f, 0.20f, 1.f);
    default: return Math::Color(0.10f, 0.11f, 0.14f, 1.f);
    }
}

// ---------------------------------------------------------------------------
// Layout
// The well is tall (10 x 22 cells), so the title and the HUD sit to its right.
// A fixed design box is centred in the client area and scaled uniformly, which
// keeps the composition balanced whatever the window size.
// ---------------------------------------------------------------------------
namespace layout
{
constexpr float kCell = 36.f;
constexpr float kCols = 10.f;
constexpr float kRows = 22.f;
constexpr float kBoardW = kCell * kCols; // 360 design px
constexpr float kBoardH = kCell * kRows; // 792 design px
constexpr float kFrame = 8.f;            // border around the well
constexpr float kGap = 48.f;             // well border -> side panel
constexpr float kPanelW = 440.f;         // side panel (title, HUD, controls)
constexpr float kPad = 24.f;             // margin around the whole design box

constexpr float kDesignW = kBoardW + kGap + kPanelW + 2.f * kFrame + 2.f * kPad; // 912
constexpr float kDesignH = kBoardH + 2.f * kFrame + 2.f * kPad;                  // 856
} // namespace layout

struct Layout
{
    float s = 1.f;      // design px -> screen px
    float x = 0.f;      // well top-left (screen px)
    float y = 0.f;
    float cell = 0.f;   // one board cell (screen px)
    float panelX = 0.f; // side panel left edge (screen px)
    float X(float v) const { return x + v * s; }
    float Y(float v) const { return y + v * s; }
};

static float Snap(float v)
{
    return std::floor(v + 0.5f);
}

static Layout ComputeLayout()
{
    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    const float w = (float)std::max<LONG>(1, rc.right - rc.left);
    const float h = (float)std::max<LONG>(1, rc.bottom - rc.top);

    // Whole-pixel cells keep the board grid crisp; everything else follows that scale.
    float s = std::min(w / layout::kDesignW, h / layout::kDesignH);
    float cell = std::floor(layout::kCell * s);
    cell = std::max(cell, 12.f);
    s = cell / layout::kCell;

    Layout L;
    L.s = s;
    L.cell = cell;
    L.x = Snap((w - layout::kDesignW * s) * 0.5f + (layout::kPad + layout::kFrame) * s);
    L.y = Snap((h - layout::kDesignH * s) * 0.5f + (layout::kPad + layout::kFrame) * s);
    L.panelX = Snap(L.X(layout::kBoardW + layout::kGap));
    return L;
}

static const Math::Color kTitleColor(0.86f, 0.88f, 0.92f, 1.f);
static const Math::Color kLabelColor(0.62f, 0.66f, 0.74f, 1.f);
static const Math::Color kValueColor(0.90f, 0.92f, 0.96f, 1.f);
static const Math::Color kKeyColor(0.98f, 0.78f, 0.32f, 1.f);
static const Math::Color kHintColor(0.72f, 0.75f, 0.82f, 1.f);

// text anchored to a design-space position measured from the well's top-left
static void DrawLabel(const Layout& L, const char* s, float dx, float dy, float baseScale, Math::Color c)
{
    g_ui->DrawTextA(UiFont::kDefaultFont, s, Snap(L.X(dx)), Snap(L.Y(dy)), c, baseScale * L.s);
}

static void DrawCentered(const Layout& L, const char* s, float centerX, float dy, float baseScale, Math::Color c)
{
    const float ts = baseScale * L.s;
    const float w = g_ui->TextWidthA(UiFont::kDefaultFont, s, ts).x;
    g_ui->DrawTextA(UiFont::kDefaultFont, s, Snap(centerX - w * 0.5f), Snap(L.Y(dy)), c, ts);
}

static void DrawBoard(const Layout& L)
{
    const float bx = L.x, by = L.y;
    const float bw = layout::kBoardW * L.s, bh = layout::kBoardH * L.s;
    const float pad = layout::kFrame * L.s;
    const float cell = L.cell;
    int cells[4][2];

    g_ui->FillRect(bx - pad, by - pad, bw + 2.f * pad, bh + 2.f * pad, Math::Color(0.11f, 0.12f, 0.16f, 1.f)); // frame
    g_ui->FillRect(bx, by, bw, bh, Math::Color(0.05f, 0.06f, 0.08f, 1.f)); // well

    for (int y = 0; y < 22; ++y)
        for (int x = 0; x < 10; ++x)
        {
            Piece p = g_game->At(x, y);
            if (p == Piece::NONE) continue;
            g_ui->FillRect(bx + x * cell + 1.f, by + y * cell + 1.f, cell - 2.f, cell - 2.f, PieceColor(p));
        }

    if (g_game->Current() != Piece::NONE)
    {
        Math::Color ghost = PieceColor(g_game->Current());
        ghost.w = 0.22f;
        const float inset = std::max(2.f, cell * 0.16f);

        g_game->PieceCells(g_game->Current(), g_game->CurRot(), g_game->CurX(), g_game->LandingY(), cells);
        for (int i = 0; i < 4; ++i)
            if (cells[i][1] >= 0 && cells[i][1] < 22)
                g_ui->FillRect(bx + cells[i][0] * cell + inset, by + cells[i][1] * cell + inset, cell - 2.f * inset,
                               cell - 2.f * inset, ghost);

        g_game->PieceCells(g_game->Current(), g_game->CurRot(), g_game->CurX(), g_game->CurY(), cells);
        for (int i = 0; i < 4; ++i)
            if (cells[i][1] >= 0 && cells[i][1] < 22)
                g_ui->FillRect(bx + cells[i][0] * cell + 1.f, by + cells[i][1] * cell + 1.f, cell - 2.f, cell - 2.f,
                               PieceColor(g_game->Current()));
    }
}

// Every key the game reacts to is listed on screen (main.cpp PumpInput key map).
static const struct
{
    const char* key;
    const char* action;
} kHelpLines[] = {
    {"Left/Right", "Move"},      {"Up", "Hard drop"}, {"Down", "Soft drop"},
    {"X", "Rotate CW"},          {"Z", "Rotate CCW"}, {"Space", "Rotate 180"},
    {"E", "Hold"},               {"Esc", "Pause"},    {"Y", "Restart"},
};

// A piece centred inside a 4x2-cell box (preview / hold slot).
static void DrawPreviewPiece(const Game* game, Piece p, float boxX, float boxY, float cell, float gap)
{
    int cells[4][2];
    game->PieceCells(p, 0, 0, 0, cells);

    int minX = cells[0][0], maxX = minX, minY = cells[0][1], maxY = minY;
    for (int i = 1; i < 4; ++i)
    {
        minX = std::min(minX, cells[i][0]);
        maxX = std::max(maxX, cells[i][0]);
        minY = std::min(minY, cells[i][1]);
        maxY = std::max(maxY, cells[i][1]);
    }

    const float boxW = 4.f * cell, boxH = 2.f * cell;
    const float ox = boxX + (boxW - (maxX - minX + 1) * cell) * 0.5f - minX * cell;
    const float oy = boxY + (boxH - (maxY - minY + 1) * cell) * 0.5f - minY * cell;

    for (int i = 0; i < 4; ++i)
        g_ui->FillRect(ox + cells[i][0] * cell, oy + cells[i][1] * cell, cell - gap, cell - gap, PieceColor(p));
}

static void DrawSidePanel(const Layout& L)
{
    const float px = L.panelX;
    const float unit = L.s;
    char buf[128];

    // title: beside the well, not above it
    DrawLabel(L, "Tetris", layout::kBoardW + layout::kGap, -8.f, 1.f, kTitleColor);

    // Next: three previews in a row (4x2-cell boxes, piece centred inside), keeping the panel compact
    DrawLabel(L, "Next", layout::kBoardW + layout::kGap, 76.f, 0.5f, kLabelColor);
    for (int n = 0; n < 3; ++n)
    {
        Piece p = g_game->NextAt(n);
        if (p == Piece::NONE) continue;
        DrawPreviewPiece(g_game, p, px + (6.f + n * 120.f) * unit, L.Y(112.f), 28.f * unit, 2.f * unit);
    }

    DrawLabel(L, "Hold", layout::kBoardW + layout::kGap, 190.f, 0.5f, kLabelColor);
    if (g_game->Held() != Piece::NONE)
        DrawPreviewPiece(g_game, g_game->Held(), px + 6.f * unit, L.Y(216.f), 28.f * unit, 2.f * unit);

    snprintf(buf, sizeof buf, "Score %d", g_game->Score());
    DrawLabel(L, buf, layout::kBoardW + layout::kGap, 296.f, 0.5f, kValueColor);
    snprintf(buf, sizeof buf, "Lines %d", g_game->Lines());
    DrawLabel(L, buf, layout::kBoardW + layout::kGap, 336.f, 0.5f, kValueColor);
    snprintf(buf, sizeof buf, "Level %d", g_game->Level());
    DrawLabel(L, buf, layout::kBoardW + layout::kGap, 376.f, 0.5f, kValueColor);

    if (!g_engine->AudioReady()) // no playback device: say so instead of looking broken
        DrawLabel(L, "Audio off", layout::kBoardW + layout::kGap, 412.f, 0.4f, Math::Color(0.72f, 0.46f, 0.46f, 1.f));

    // controls: key column + action column (monospace, so the columns line up)
    DrawLabel(L, "Controls", layout::kBoardW + layout::kGap, 448.f, 0.5f, kLabelColor);
    const float help = 0.45f * unit;
    const float advance = g_ui->TextWidthA(UiFont::kDefaultFont, "M", 1.f).x; // one monospace cell at scale 1
    const int nHelp = (int)(sizeof(kHelpLines) / sizeof(kHelpLines[0]));
    for (int i = 0; i < nHelp; ++i)
    {
        const float y = Snap(L.Y(488.f + (float)i * 33.f));
        g_ui->DrawTextA(UiFont::kDefaultFont, kHelpLines[i].key, px, y, kKeyColor, help);
        g_ui->DrawTextA(UiFont::kDefaultFont, kHelpLines[i].action, px + 12.f * advance * help, y, kHintColor, help);
    }
}

static void DrawStatusOverlay(const Layout& L)
{
    if (!g_game->IsOver() && !g_game->IsPaused()) return;

    const float w = layout::kBoardW * L.s, h = layout::kBoardH * L.s;
    g_ui->FillRect(L.x, L.y, w, h, Math::Color(0.02f, 0.02f, 0.03f, 0.74f)); // dim the well

    const float cx = L.x + w * 0.5f;
    if (g_game->IsOver())
    {
        DrawCentered(L, "Game Over", cx, 330.f, 0.6f, Math::Color(0.95f, 0.45f, 0.45f, 1.f));
        DrawCentered(L, "Press Y to restart", cx, 400.f, 0.36f, kHintColor);
    }
    else
    {
        DrawCentered(L, "Paused", cx, 330.f, 0.66f, Math::Color(0.95f, 0.85f, 0.40f, 1.f));
        DrawCentered(L, "Esc to resume", cx, 400.f, 0.36f, kHintColor);
    }
}

// board + pieces + HUD (must be called between g_ui->Begin()/End())
static void DrawPlayfield()
{
    const Layout L = ComputeLayout();

    DrawBoard(L);
    DrawSidePanel(L);
    DrawStatusOverlay(L);
}

// sound effects: input edges plus game-state deltas. Audio is optional: PlaySfx no-ops without a device.
static void PumpAudio()
{
    if (!g_engine || !g_game) return;

    if (s_edge[0] || s_edge[1]) g_engine->PlaySfx(L"assets\\src\\sfx_key_move.wav");
    if (s_edge[2] || s_edge[3] || s_edge[4]) g_engine->PlaySfx(L"assets\\src\\sfx_key_rotate.wav");
    if (s_edge[5]) g_engine->PlaySfx(L"assets\\src\\sfx_softstep.wav");
    if (s_edge[6]) g_engine->PlaySfx(L"assets\\src\\sfx_key_hold.wav");
    if (s_edge[7]) g_engine->PlaySfx(L"assets\\src\\sfx_harddrop.wav");
    if (s_edge[9])
        g_engine->PlaySfx(g_game->IsPaused() ? L"assets\\src\\sfx_pause.wav" : L"assets\\src\\sfx_unpause.wav");

    static int prevLines = 0;
    static int prevLevel = 1;
    static bool prevOver = false;

    const int lines = g_game->Lines();
    if (lines > prevLines)
    {
        const int n = lines - prevLines;
        const wchar_t* clear = (n >= 4)   ? L"assets\\src\\sfx_clear4.wav"
                               : (n == 3) ? L"assets\\src\\sfx_clear3.wav"
                               : (n == 2) ? L"assets\\src\\sfx_clear2.wav"
                                          : L"assets\\src\\sfx_clear1.wav";
        g_engine->PlaySfx(clear);
        if (g_game->Combo() >= 1) g_engine->PlaySfx(L"assets\\src\\sfx_combo.wav");
    }

    if (g_game->Level() > prevLevel) g_engine->PlaySfx(L"assets\\src\\sfx_levelup.wav");

    const bool over = g_game->IsOver();
    if (over && !prevOver) g_engine->PlaySfx(L"assets\\src\\sfx_gameover.wav");
    if (!over && prevOver) g_engine->PlaySfx(L"assets\\src\\sfx_start.wav");

    prevLines = lines;
    prevLevel = g_game->Level();
    prevOver = over;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;

    ImmDisableIME(-1); // no IME: otherwise the composition window grabs X/Z/arrows and swallows game input
    const bool snapshotMode = (__argc > 1 && _wcsicmp(__wargv[1], L"--snapshot") == 0);
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED); // WIC/XAudio2 need COM initialized (MTA)

    try
    {
        g_engine = new Engine();
        g_game = new Game();
        g_ui = new UiFont(*g_engine);
        g_engine->CreateDevice(false); // hardware; engine falls back to WARP (--snapshot runs this mode)

        {
            FILE* f = fopen("step_start.txt", "w");
            if (f)
            {
                fputs("enter\n", f);
                fclose(f);
            }
        }

        if (snapshotMode) g_engine->EnableSnapshotMode(); // dump a presented frame to PNG, then exit
        int snapDelayMs = 0;                              // --delay=<ms>: capture after a warm-up (default: frame 1)
        for (int i = 1; i < __argc; ++i)
            if (_wcsnicmp(__wargv[i], L"--delay=", 8) == 0) snapDelayMs = _wtoi(__wargv[i] + 8);

        HWND hwnd = g_engine->CreateWindowHandle(hInst, 1120, 860,
                                                 WndProc); // engine registers class; main owns WNDPROC via callback
        g_hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOW);

        timeBeginPeriod(1);
        LARGE_INTEGER q0, qc, cur;
        QueryPerformanceFrequency(&qc);
        QueryPerformanceCounter(&q0);
        double t0 = q0.QuadPart / (double)qc.QuadPart;
        const double tStart = t0;

        bool showCursor = true;
        bool firstFrame = true;
        while (g_running)
        {
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.hwnd == hwnd && msg.message == WM_KEYDOWN)
                {
                } // (no accelerator table used)
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            if (!g_running) break;

            PumpInput();

            QueryPerformanceCounter(&cur);
            double t = cur.QuadPart / (double)qc.QuadPart;
            float dt = (float)(t - t0);
            t0 = t;
            if (dt > 0.25f) dt = 0.25f; // clamp on focus loss

            g_game->Tick(dt, g_keys);
            PumpAudio();

            // ---- frame render ----
            g_engine->Clear(Math::Color(0.04f, 0.05f, 0.07f, 1.f)); // dark UI background
            g_ui->Begin();
            DrawPlayfield();
            g_ui->End();

            if (g_engine->SnapshotRequested() && (t - tStart) * 1000.0 >= (double)snapDelayMs)
            {
                {
                    FILE* f = fopen("step_snapshot.txt", "w");
                    if (f)
                    {
                        fputs("enter\n", f);
                        fclose(f);
                    }
                }
                printf("STEP: snapshot begin\n");
                g_engine->SaveSnapshotPng("snapshot.png"); // dump first presented frame, then exit
                printf("STEP: snapshot done\n");
                g_running = false;
            }

            g_engine->Present(showCursor);
            showCursor = false;
            firstFrame = false;
        }

        timeEndPeriod(1);
    }
    catch (const std::exception& e)
    {
        {
            FILE* f = fopen("step_exc.txt", "w");
            if (f)
            {
                fprintf(f, "%s\n", e.what());
                fclose(f);
            }
        }

        printf("EXCEPTION: %s\n", e.what());
        if (!snapshotMode) MessageBoxA(nullptr, e.what(), "Tetris - startup error", MB_ICONERROR | MB_OK);
    }
    catch (...)
    {
        {
            FILE* f = fopen("step_exc.txt", "w");
            if (f)
            {
                fputs("unknown\n", f);
                fclose(f);
            }
        }

        printf("EXCEPTION: unknown\n");
        if (!snapshotMode)
            MessageBoxA(nullptr, "Unknown startup failure.", "Tetris - startup error", MB_ICONERROR | MB_OK);
    }

    if (SUCCEEDED(hrCo)) CoUninitialize();
    return 0;
}
