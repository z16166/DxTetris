// main.cpp — window + WNDPROC + frame loop (MINE, final; no subagents).
// RAII: engine owns all D3D objects (COM members); game/ui own nothing raw.
#include "engine.h"
#include "game.h"
#include "ui_font.h"
#include <windows.h>
#include <imm.h>      // ImmDisableIME — keep the IME from eating game keys
#include <mmsystem.h> // timeBeginPeriod
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

// board + pieces + HUD (must be called between g_ui->Begin()/End())
static void DrawPlayfield()
{
    const float CELL = 32.f;
    const float BX = 60.f, BY = 100.f;
    const float BW = CELL * 10.f, BH = CELL * 22.f; // rows 0..21: the 2 spawn-buffer rows stay visible
    int cells[4][2];

    g_ui->FillRect(BX - 8.f, BY - 8.f, BW + 16.f, BH + 16.f, Math::Color(0.11f, 0.12f, 0.16f, 1.f)); // frame
    g_ui->FillRect(BX, BY, BW, BH, Math::Color(0.05f, 0.06f, 0.08f, 1.f));                           // well

    for (int y = 0; y < 22; ++y)
        for (int x = 0; x < 10; ++x)
        {
            Piece p = g_game->At(x, y);
            if (p == Piece::NONE) continue;
            g_ui->FillRect(BX + x * CELL + 1.f, BY + y * CELL + 1.f, CELL - 2.f, CELL - 2.f, PieceColor(p));
        }

    if (g_game->Current() != Piece::NONE)
    {
        Math::Color ghost = PieceColor(g_game->Current());
        ghost.w = 0.22f;
        g_game->PieceCells(g_game->Current(), g_game->CurRot(), g_game->CurX(), g_game->LandingY(), cells);
        for (int i = 0; i < 4; ++i)
            if (cells[i][1] >= 0 && cells[i][1] < 22)
                g_ui->FillRect(BX + cells[i][0] * CELL + 5.f, BY + cells[i][1] * CELL + 5.f, CELL - 10.f, CELL - 10.f,
                               ghost);

        g_game->PieceCells(g_game->Current(), g_game->CurRot(), g_game->CurX(), g_game->CurY(), cells);
        for (int i = 0; i < 4; ++i)
            if (cells[i][1] >= 0 && cells[i][1] < 22)
                g_ui->FillRect(BX + cells[i][0] * CELL + 1.f, BY + cells[i][1] * CELL + 1.f, CELL - 2.f, CELL - 2.f,
                               PieceColor(g_game->Current()));
    }

    // side panel
    const float NX = BX + BW + 70.f;
    char buf[128];
    g_ui->DrawTextA(UiFont::kDefaultFont, "NEXT", NX, 40.f, Math::Color(0.70f, 0.72f, 0.78f, 1.f));
    for (int n = 0; n < 3; ++n)
    {
        Piece p = g_game->NextAt(n);
        if (p == Piece::NONE) continue;
        g_game->PieceCells(p, 0, 0, 0, cells);
        const float ox = NX, oy = 118.f + n * 104.f;
        for (int i = 0; i < 4; ++i)
            g_ui->FillRect(ox + cells[i][0] * 24.f, oy + cells[i][1] * 24.f, 22.f, 22.f, PieceColor(p));
    }

    g_ui->DrawTextA(UiFont::kDefaultFont, "HOLD", NX, 420.f, Math::Color(0.70f, 0.72f, 0.78f, 1.f));
    if (g_game->Held() != Piece::NONE)
    {
        g_game->PieceCells(g_game->Held(), 0, 0, 0, cells);
        for (int i = 0; i < 4; ++i)
            g_ui->FillRect(NX + cells[i][0] * 24.f, 480.f + cells[i][1] * 24.f, 22.f, 22.f, PieceColor(g_game->Held()));
    }

    snprintf(buf, sizeof buf, "SCORE %d", g_game->Score());
    g_ui->DrawTextA(UiFont::kDefaultFont, buf, NX, 660.f, Math::Color(0.86f, 0.88f, 0.92f, 1.f));
    snprintf(buf, sizeof buf, "LINES %d", g_game->Lines());
    g_ui->DrawTextA(UiFont::kDefaultFont, buf, NX, 730.f, Math::Color(0.80f, 0.82f, 0.86f, 1.f));
    snprintf(buf, sizeof buf, "LEVEL %d", g_game->Level());
    g_ui->DrawTextA(UiFont::kDefaultFont, buf, NX, 800.f, Math::Color(0.80f, 0.82f, 0.86f, 1.f));

    if (g_game->IsOver())
        g_ui->DrawTextA(UiFont::kDefaultFont, "GAME OVER - press Y", 640, 40.f, Math::Color(0.95f, 0.45f, 0.45f, 1.f));
    else if (g_game->IsPaused())
        g_ui->DrawTextA(UiFont::kDefaultFont, "PAUSED", 640, 40.f, Math::Color(0.95f, 0.85f, 0.40f, 1.f));

    if (!g_engine->AudioReady()) // no playback device: say so instead of looking broken
        g_ui->DrawTextA(UiFont::kDefaultFont, "AUDIO OFF", 640, 150.f, Math::Color(0.70f, 0.50f, 0.50f, 1.f));
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

        HWND hwnd = g_engine->CreateWindowHandle(hInst, 1600, 900,
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
            g_ui->DrawText(UiFont::kDefaultFont, L"TETRIS", 60, 18, Math::Color(0.86f, 0.88f, 0.92f, 1.f), 0.f);
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
