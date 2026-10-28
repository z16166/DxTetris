#pragma once
// game.h — Tetris logic (mine). Rendering via engine/ui; this file owns NO rendering.
#include <array>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace tetris
{

enum class Piece : uint8_t
{
    I,
    O,
    T,
    S,
    Z,
    J,
    L,
    NONE
};

struct InputSnapshot
{
    bool p[16];
    bool anyEdge = false;
}; // key-map indices fixed by main.cpp contract: L,R,CW,CCW,R180,SOFT,HOLD,HARD,START,PAUSE (see game.cpp Idx)

struct Action
{
    enum Type : uint8_t
    {
        NONE,
        MOVE_L,
        MOVE_R,
        ROT_CW,
        ROT_CCW,
        ROT_180,
        SOFT,
        HARD,
        HOLD,
        LOCK
    };
    Type type = NONE;
    Action() = default;
    Action(Type t) : type(t) {} // implicit from enum (game.cpp assigns bare enum constants)
};

class Game
{
public:
    Game(); // implemented in game.cpp (NewGame + initial state)
    void NewGame();
    void Tick(float dtSeconds, const InputSnapshot& keys); // per-frame update (DAS/SDF/lock-delay/buffer inside)
    // view accessors for renderer:
    int BoardWidth() const { return 10; }
    int BoardHeight() const { return 22; } // 20 visible + 2 spawn buffer rows
    Piece At(int x, int y) const;          // locked cell view (y: 0=top row incl. buffer, 21=bottom)
    Piece Current() const { return m_cur; }
    int CurX() const { return m_curX; }
    int CurY() const { return m_curY; }
    int CurRot() const { return m_curRot; }
    Piece NextAt(int slot) const { return m_peek[slot]; } // 3 previews
    Piece Held() const { return m_hold; }
    bool HoldUsed() const { return m_holdUsed; }
    void PieceCells(Piece p, int rot, int x, int y, int out[4][2]) const; // absolute grid cells (renderer)
    int Score() const { return m_score; }
    int Lines() const { return m_lines; }
    int Level() const { return m_level; }
    int Combo() const { return m_combo; }
    bool IsOver() const { return m_over; }
    bool IsPaused() const { return m_paused; }
    float DropDelay() const;  // seconds per row (level-driven)
    int LandingY() const;     // hard-drop landing row of current
    bool GhostBlocks() const; // true = landing at current would top-out (TOCT indicator)

    // fx taps (renderer/ui layer consumes; main.cpp triggers matching sounds):
    float ConsumeLineFlash()
    {
        float v = m_fxLineFlash;
        m_fxLineFlash = 0;
        return v;
    }
    float ConsumeComboFlash()
    {
        float v = m_fxComboFlash;
        m_fxComboFlash = 0;
        return v;
    }
    float ConsumeLandShake()
    {
        float v = m_fxLandShake;
        m_fxLandShake = 0;
        return v;
    }

private:
    void NotifyActionVisual(Action t);                 // fx tap dispatcher (drains taps for renderer)
    Piece At2(int x, int y) const { return At(x, y); } // (kept for symmetry; main uses At)
    bool Fits(Piece p, int rot, int x, int y) const;   // collision check (full spawn-state grids)
    int KickTransition(int from, int to) const;
    void RotateApply(int from, int to);
    void ApplyMove(int dx);
    void SoftStep();
    void HardDrop();
    void HoldSwap();
    void LockPiece(bool hardDrop);
    void ClearFullRows();
    void RefillPeek();
    Piece PopNext();

    int m_grid[22][10]{}; // 0=empty, 1..7=piece id+1; row 0 top (buffer), 21 bottom
    Piece m_cur = Piece::NONE;
    int m_curX = 3, m_curY = 0, m_curRot = 0;
    Piece m_hold = Piece::NONE;
    bool m_holdUsed = false;
    std::array<Piece, 3> m_peek{Piece::NONE, Piece::NONE, Piece::NONE};
    std::vector<Piece> m_pool; // 7-bag refills into peek window
    uint64_t m_rng = 0;        // xorshift, deterministic seed
    // timing
    float m_gratTimer = 0; // gravity timer -> DropDelay()
    float m_lockDelay = 0;
    const float m_lockCap = 0.5f; // lock delay 500ms (cap 15 resets)
    int m_lockResets = 0;
    const int m_maxResets = 15;
    // DAS/SDF held state per key index (see Idx)
    float m_heldT[11]{};
    bool m_heldActive[11]{};
    // scoring/state
    int m_score = 0, m_lines = 0, m_level = 1, m_combo = -1, m_b2b = 0;
    bool m_over = false, m_paused = false;
    // fx taps
    float m_fxLineFlash = 0, m_fxComboFlash = 0, m_fxLandShake = 0;
    struct Idx
    {
        enum : int
        {
            L = 0,
            R = 1,
            CW = 2,
            CCW = 3,
            R180 = 4,
            SOFT = 5,
            HOLD = 6,
            HARD = 7,
            START = 8,
            PAUSE = 9,
            SS = 10,
            COUNT = 11
        };
    };
    Action m_queued; // input buffer (one action/frame)
};

} // namespace tetris
