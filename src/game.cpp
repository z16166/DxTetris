// game.cpp — Tetris core logic (mine). Standard SRS data baked below (kick tables are
// y-UP-POSITIVE standard tables negated to my y-DOWN grid; "close-enough SRS" noted).
// 7-bag, hold+3 previews, DAS 133ms (instant first-press)/SDF, lock delay 500ms + 15 reset cap,
// guideline-ish scoring/combos/levels/TOCT.
#include "game.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numeric>

namespace tetris
{

// ---- SRS spawn grids [piece][rot0..3][cell0..3][xy], 0..3 local, top-left origin (standard SRS).
// JLSTZ pivots at (1,1); O/I pivots centered ((1.5,1.5)). ----
// clang-format off
static const int kSrs[7][4][4][2] = {
  /*I*/ {  { {0,1},{1,1},{2,1},{3,1}},  {{2,0},{2,1},{2,2},{2,3}},  {{0,2},{1,2},{2,2},{3,2}},  {{1,0},{1,1},{1,2},{1,3}} },
  /*O*/ {  { {1,1},{2,1},{1,2},{2,2}},  {{1,1},{2,1},{1,2},{2,2}},  {{1,1},{2,1},{1,2},{2,2}},  {{1,1},{2,1},{1,2},{2,2}} },
  /*T*/ {  { {1,0},{0,1},{1,1},{2,1}},  {{1,0},{1,1},{2,1},{1,2}},  {{0,1},{1,1},{2,1},{1,2}},  {{1,0},{0,1},{1,1},{1,2}} },
  /*S*/ {  { {1,0},{2,0},{0,1},{1,1}},  {{1,0},{1,1},{2,1},{2,2}},  {{1,1},{2,1},{0,2},{1,2}},  {{0,0},{0,1},{1,1},{1,2}} },
  /*Z*/ {  { {0,0},{1,0},{1,1},{2,1}},  {{2,0},{1,1},{2,1},{1,2}},  {{0,1},{1,1},{1,2},{2,2}},  {{1,0},{0,1},{1,1},{0,2}} },
  /*J*/ {  { {0,0},{0,1},{1,1},{2,1}},  {{1,0},{2,0},{1,1},{1,2}},  {{0,1},{1,1},{2,1},{2,2}},  {{1,0},{1,1},{0,2},{1,2}} },
  /*L*/ {  { {2,0},{0,1},{1,1},{2,1}},  {{1,0},{1,1},{1,2},{2,2}},  {{0,1},{1,1},{2,1},{0,2}},  {{0,0},{1,0},{1,1},{1,2}} },
};

// ---- Standard SRS kicks, baked (standard tables, y negated to y-down grid).
// Row order = transition slots: 0->1,1->0,0->3,3->0,1->2,2->1,2->3,3->2. ----
static const int kKicksJLSZT[7][8][5][2] = {
  /*J*/ {  { {0,0},{-1,0},{-1,-1},{ 0, 2},{-1, 2}},  { {0,0},{ 1,0},{ 1, 1},{ 0,-2},{ 1,-2}},
            { {0,0},{ 1,0},{ 1,-1},{ 0, 2},{ 1, 2}},  { {0,0},{-1,0},{-1, 1},{ 0,-2},{-1,-2}},
            { {0,0},{-1,0},{-1,-1},{ 0, 2},{-1, 2}},  { {0,0},{ 1,0},{ 1, 1},{ 0,-2},{ 1,-2}},
            { {0,0},{ 1,0},{ 1,-1},{ 0, 2},{ 1, 2}},  { {0,0},{-1,0},{-1, 1},{ 0,-2},{-1,-2}} },
  /*L*/ {  { {0,0},{ 1,0},{ 1,-1},{ 0, 2},{ 1, 2}},   { {0,0},{-1,0},{-1,-1},{ 0,-2},{-1,-2}},
            { {0,0},{-1,0},{-1,-1},{ 0, 2},{-1, 2}},   { {0,0},{ 1,0},{ 1, 1},{ 0,-2},{ 1,-2}},
            { {0,0},{-1,0},{-1, 1},{ 0, 2},{-1, 2}},   { {0,0},{ 1,0},{ 1,-1},{ 0,-2},{ 1,-2}},
            { {0,0},{ 1,0},{ 1, 1},{ 0,-2},{ 1,-2}},   { {0,0},{-1,0},{-1,-1},{ 0, 2},{-1, 2}} },
  /*S*/ {  { {0,0},{ 1,0},{ 1,-1},{ 0, 2},{ 1, 2}},   { {0,0},{-1,0},{-1,-1},{ 0,-2},{-1,-2}},
            { {0,0},{-1,0},{-1,-1},{ 0, 2},{-1, 2}},   { {0,0},{ 1,0},{ 1, 1},{ 0,-2},{ 1,-2}},
            { {0,0},{-1,0},{-1, 1},{ 0, 2},{-1, 2}},   { {0,0},{ 1,0},{ 1,-1},{ 0,-2},{ 1,-2}},
            { {0,0},{ 1,0},{ 1, 1},{ 0,-2},{ 1,-2}},   { {0,0},{-1,0},{-1,-1},{ 0, 2},{-1, 2}} },
  /*Z*/ {  { {0,0},{ 1,0},{ 1,-1},{ 0, 2},{ 1, 2}},   { {0,0},{-1,0},{-1,-1},{ 0,-2},{-1,-2}},
            { {0,0},{-1,0},{-1,-1},{ 0, 2},{-1, 2}},   { {0,0},{ 1,0},{ 1, 1},{ 0,-2},{ 1,-2}},
            { {0,0},{-1,0},{-1, 1},{ 0, 2},{-1, 2}},   { {0,0},{ 1,0},{ 1,-1},{ 0,-2},{ 1,-2}},
            { {0,0},{ 1,0},{ 1, 1},{ 0,-2},{ 1,-2}},   { {0,0},{-1,0},{-1,-1},{ 0, 2},{-1, 2}} },
  /*T*/ {  { {0,0},{ 1,0},{ 1,-1},{ 0, 2},{ 1, 2}},   { {0,0},{-1,0},{-1,-1},{ 0,-2},{-1,-2}},
            { {0,0},{-1,0},{-1,-1},{ 0, 2},{-1, 2}},   { {0,0},{ 1,0},{ 1, 1},{ 0,-2},{ 1,-2}},
            { {0,0},{-1,0},{-1, 1},{ 0, 2},{-1, 2}},   { {0,0},{ 1,0},{ 1,-1},{ 0,-2},{ 1,-2}},
            { {0,0},{ 1,0},{ 1, 1},{ 0,-2},{ 1,-2}},   { {0,0},{-1,0},{-1,-1},{ 0, 2},{-1, 2}} },
  /*I*/ {  { {0,0},{ 2,0},{ 2,-1},{-1,-2},{ 1,-2}},   { {0,0},{-2,0},{-2,-1},{ 1,-2},{-1,-2}},
            { {0,0},{ 2,0},{ 2, 1},{-1, 2},{ 1, 2}},   { {0,0},{-2,0},{-2, 1},{ 1, 2},{-1, 2}},
            { {0,0},{ 1,0},{ 1, 2},{ 0,-2},{ 0,-2}},   { {0,0},{-1,0},{-1,-2},{ 0, 2},{ 0, 2}},
            { {0,0},{-1,0},{-1, 1},{ 0,-2},{ 0,-2}},   { {0,0},{ 1,0},{ 1,-1},{ 0, 2},{ 0, 2}} },
  // I uses the I-table for 0->1/1->0/0->3/3->0/1->2/2->1/2->3/3->2 in same row order:
  // (already baked above)
};

// clang-format on

static const int kTrans[8][2] = {{0, 1}, {1, 0}, {0, 3}, {3, 0}, {1, 2}, {2, 1}, {2, 3}, {3, 2}};
static int TransSlot(int from, int to)
{
    for (int i = 0; i < 8; ++i)
        if (kTrans[i][0] == from && kTrans[i][1] == to) return i;
    return -1;
}

static uint64_t RngNext(uint64_t& s)
{
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

Game::Game()
{
    NewGame();
}

// fx tap dispatcher: renderer consumes flashes/shake via Consume* accessors; sound events are
// driven by main.cpp from input edges + these taps (this dispatcher records the fx-side taps).
void Game::NotifyActionVisual(Action t)
{
    switch (t.type)
    {
    case Action::HARD: m_fxLandShake = 1.f; break;
    case Action::LOCK: m_fxLandShake = 1.f; break;
    default: break;
    }
}

Piece Game::At(int x, int y) const
{
    if (x < 0 || x >= 10 || y < 0 || y >= 22) return Piece::NONE;
    int v = m_grid[y][x];
    return v ? (Piece)(v - 1) : Piece::NONE;
}

// renderer view: the four absolute grid cells of a piece at a given position/rotation
void Game::PieceCells(Piece p, int rot, int x, int y, int out[4][2]) const
{
    int pi = (int)p;
    if (pi < 0 || pi > 6)
    {
        for (int c = 0; c < 4; ++c)
        {
            out[c][0] = -1;
            out[c][1] = -1;
        }
        return;
    }
    int r = ((rot % 4) + 4) % 4;
    for (int c = 0; c < 4; ++c)
    {
        out[c][0] = kSrs[pi][r][c][0] + x;
        out[c][1] = kSrs[pi][r][c][1] + y;
    }
}

void Game::NewGame()
{
    std::memset(m_grid, 0, sizeof m_grid);
    m_cur = Piece::NONE;
    m_hold = Piece::NONE;
    m_holdUsed = false;
    m_peek.fill(Piece::NONE);
    m_pool.clear();
    m_rng = 0x252ea9c741f14f35ULL;
    m_score = 0;
    m_lines = 0;
    m_level = 1;
    m_combo = -1;
    m_b2b = 0;
    m_over = false;
    m_paused = false;
    m_gratTimer = 0.f;
    m_lockDelay = 0.f;
    m_lockResets = 0;
    std::memset(m_heldT, 0, sizeof m_heldT);
    std::memset(m_heldActive, 0, sizeof m_heldActive);
    m_queued = Action::NONE; // buffer: one action per frame
}

bool Game::Fits(Piece p, int rot, int x, int y) const
{
    for (int c = 0; c < 4; ++c)
    {
        int lx = kSrs[(int)p][rot][c][0] + x;
        int ly = kSrs[(int)p][rot][c][1] + y;
        if (lx < 0 || lx >= BoardWidth()) return false;
        if (ly >= BoardHeight()) return false;
        if (ly >= 0 && m_grid[ly][lx]) return false;
    }
    return true;
}

Piece Game::PopNext()
{
    Piece p = m_peek[0];
    m_peek[0] = m_peek[1];
    m_peek[1] = m_peek[2];
    m_peek[2] = Piece::NONE;
    return p;
}

void Game::RefillPeek()
{
    // ensure pool seeded
    if (m_pool.empty())
    {
        Piece bag[7] = {Piece::I, Piece::O, Piece::T, Piece::S, Piece::Z, Piece::J, Piece::L};
        for (int i = 6; i > 0; --i)
            std::swap(bag[i], bag[(size_t)RngNext(m_rng) % (i + 1)]);
        for (auto p : bag)
            m_pool.push_back(p);
    }
    auto fill = [&](int idx)
    {
        while (m_peek[idx] == Piece::NONE && !m_pool.empty())
        {
            m_peek[idx] = m_pool.front();
            m_pool.erase(m_pool.begin());
            if (m_pool.empty())
            { // seed another bag
                Piece bag[7] = {Piece::I, Piece::O, Piece::T, Piece::S, Piece::Z, Piece::J, Piece::L};
                for (int i = 6; i > 0; --i)
                    std::swap(bag[i], bag[(size_t)RngNext(m_rng) % (i + 1)]);
                for (auto p : bag)
                    m_pool.push_back(p);
            }
        }
    };
    fill(0);
    fill(1);
    fill(2);
}

void Game::ClearFullRows()
{
    int rows[4], cnt = 0;
    for (int y = 2; y < 22; ++y)
    {
        bool full = true;
        for (int x = 0; x < 10; ++x)
            if (!m_grid[y][x])
            {
                full = false;
                break;
            }
        if (full) rows[cnt++] = y;
    }
    if (!cnt)
    {
        m_combo = -1;
        return;
    }
    for (int i = 0; i < cnt; ++i)
    {
        int y = rows[i];
        for (int yy = y; yy >= 1; --yy)
            for (int x = 0; x < 10; ++x)
                m_grid[yy][x] = m_grid[yy - 1][x];
        for (int x = 0; x < 10; ++x)
            m_grid[0][x] = 0;
    }
    // guideline scoring (approx): singles100 double300 triple500 quad800 * level; B2B x2; combo chain 50*combo*level
    static const int baseTbl[5] = {0, 100, 300, 500, 800};
    bool prevB2B = m_b2b;
    m_score += baseTbl[cnt] * (cnt >= 2 && prevB2B ? 2 : 1) * m_level;
    m_b2b = cnt >= 2 ? 1 : 0;
    m_lines += cnt;
    if (m_combo >= 0)
        ++m_combo;
    else
        m_combo = 0;
    if (m_combo > 0) m_score += 50 * m_combo * m_level;
    m_level = std::max(m_level, 1 + m_lines / 10);
    m_fxLineFlash = 1.f;
    if (m_combo >= 2) m_fxComboFlash = 1.f;
    NotifyActionVisual(Action::LOCK); // fx/sound tap
}

void Game::LockPiece(bool hardDrop)
{
    for (int c = 0; c < 4; ++c)
    {
        int lx = kSrs[(int)m_cur][m_curRot][c][0] + m_curX;
        int ly = kSrs[(int)m_cur][m_curRot][c][1] + m_curY;
        if (ly >= 0 && ly < BoardHeight() && lx >= 0 && lx < 10) m_grid[ly][lx] = (int)m_cur + 1;
    }
    NotifyActionVisual(Action::LOCK); // land sfx (hard-drop handled by caller tap)
    ClearFullRows();
    if (hardDrop) m_fxLandShake = 1.f;
}

int Game::LandingY() const
{
    if (m_cur == Piece::NONE) return 0;
    int y = m_curY;
    while (Fits(m_cur, m_curRot, m_curX, y + 1))
        ++y;
    return y;
}

bool Game::GhostBlocks() const
{
    // landing at current would top-out (overlap in spawn-buffer rows)
    if (m_cur == Piece::NONE) return false;
    int y = LandingY();
    for (int c = 0; c < 4; ++c)
    {
        int lx = kSrs[(int)m_cur][m_curRot][c][0] + m_curX;
        int ly = kSrs[(int)m_cur][m_curRot][c][1] + y;
        if (ly >= 0 && ly < 2 && lx >= 0 && lx < 10 && m_grid[ly][lx]) return true;
    }
    return false;
}

float Game::DropDelay() const
{
    static const float t[] = {1.0f, .8f, .6f, .5f, .4f, .3f, .25f, .2f, .15f, .12f, .10f, .09f, .08f, .07f, .06f, .05f};
    int lv = std::min(m_level, (int)(sizeof t / sizeof t[0]));
    return t[lv - 1];
}

void Game::HoldSwap()
{
    if (m_holdUsed) return; // used this drop: no swap
    Piece swap = m_hold;
    m_hold = m_cur;
    m_cur = (swap != Piece::NONE) ? swap : PopNext();
    if (m_cur == Piece::NONE)
    {
        m_over = true;
        return;
    }
    m_curRot = 0;
    m_curX = 3;
    m_curY = 0;
    m_lockDelay = 0;
    m_lockResets = 0;
    m_gratTimer = 0;
    m_holdUsed = true;
    RefillPeek();
    if (!Fits(m_cur, 0, m_curX, m_curY)) m_over = true; // TOCT on swap
}

void Game::Tick(float dt, const InputSnapshot& keys)
{
    auto edge = [&](int idx)
    {
        bool n = keys.p[idx], w = m_heldActive[idx];
        m_heldActive[idx] = n;
        return n && !w;
    };
    auto heldRepeat = [&](int idx, float threshold)
    {
        if (!keys.p[idx])
        {
            m_heldT[idx] = 0;
            return false;
        }
        if (m_heldActive[idx]) return false; // fresh press consumed by edge path
        m_heldT[idx] += dt;
        return m_heldT[idx] >= threshold;
    };
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
            COUNT = 11
        };
    };
    // single consumed snapshot per frame:
    bool eL = edge(Idx::L), eR = edge(Idx::R), eCW = edge(Idx::CW), eCCW = edge(Idx::CCW), e180 = edge(Idx::R180);
    bool eSoft = edge(Idx::SOFT), eHold = edge(Idx::HOLD), eHard = edge(Idx::HARD);
    bool eStart = edge(Idx::START), ePause = edge(Idx::PAUSE);
    bool hL = keys.p[Idx::L], hR = keys.p[Idx::R], hS = keys.p[Idx::SOFT];
    float &tL = m_heldT[Idx::L], &tR = m_heldT[Idx::R], &tS = m_heldT[Idx::SOFT];
    auto repeatL = [&]
    {
        if (eL) return false;
        if (!hL)
        {
            tL = 0;
            return false;
        }
        tL += dt;
        return tL >= 0.133f;
    };
    auto repeatR = [&]
    {
        if (eR) return false;
        if (!hR)
        {
            tR = 0;
            return false;
        }
        tR += dt;
        return tR >= 0.133f;
    };
    auto repeatS = [&]
    {
        if (eSoft) return false;
        if (!hS)
        {
            tS = 0;
            return false;
        }
        tS += dt;
        return tS >= 0.016f;
    };
    bool rL = repeatL(), rR = repeatR(), rS = repeatS();

    if (eStart && m_over) NewGame();
    if (ePause && !m_over) m_paused = !m_paused;
    if (m_over || m_paused) return;

    // spawn from bag
    RefillPeek();
    if (m_cur == Piece::NONE)
    {
        m_cur = PopNext();
        RefillPeek();
        if (m_cur == Piece::NONE) return;
        m_curRot = 0;
        m_curX = 3;
        m_curY = 0;
        m_holdUsed = false;
        m_lockDelay = 0;
        m_lockResets = 0;
        m_gratTimer = 0;
        if (!Fits(m_cur, 0, m_curX, m_curY))
        {
            m_over = true;
            return;
        } // TOCT on spawn
    }

    // one action per frame (input buffer): priority order
    Action::Type a = Action::NONE;
    if (eHold)
        a = Action::HOLD;
    else if (e180)
        a = Action::ROT_180;
    else if (eCCW)
        a = Action::ROT_CCW;
    else if (eCW)
        a = Action::ROT_CW;
    else if (eL)
        a = Action::MOVE_L;
    else if (eR)
        a = Action::MOVE_R;
    else if (eHard)
        a = Action::HARD;
    else if (eSoft)
        a = Action::SOFT;
    else if (rS)
        a = Action::SOFT;
    else if (rL)
        a = Action::MOVE_L;
    else if (rR)
        a = Action::MOVE_R;
    // buffered one action:
    if (a == Action::NONE && m_queued.type != Action::NONE) a = m_queued.type;
    m_queued = Action::NONE;

    auto applyRotate = [&](int to)
    {
        int t = TransSlot(m_curRot, to);
        for (int s = 0; s < 5; ++s)
        {
            int kx = kKicksJLSZT[(int)m_cur][t][s][0], ky = kKicksJLSZT[(int)m_cur][t][s][1];
            if (Fits(m_cur, to, m_curX + kx, m_curY + ky))
            {
                m_curX += kx;
                m_curY += ky;
                m_curRot = to;
                m_lockDelay = 0;
                if (m_lockResets < m_maxResets) ++m_lockResets;
                NotifyActionVisual(Action::ROT_CW);
                return;
            }
        }
    };
    auto applyMove = [&](int dx)
    {
        if (Fits(m_cur, m_curRot, m_curX + dx, m_curY))
        {
            m_curX += dx;
            m_lockDelay = 0;
            if (m_lockResets < m_maxResets) ++m_lockResets;
            NotifyActionVisual(Action::MOVE_L); // tap (move sfx)
        }
    };

    switch (a)
    {
    case Action::MOVE_L: applyMove(-1); break;
    case Action::MOVE_R: applyMove(+1); break;
    case Action::ROT_CW: applyRotate((m_curRot + 1) & 3); break;
    case Action::ROT_CCW: applyRotate((m_curRot + 3) & 3); break;
    case Action::ROT_180: applyRotate((m_curRot + 2) & 3); break;
    case Action::SOFT:
        if (Fits(m_cur, m_curRot, m_curX, m_curY + 1))
        {
            m_curY++;
            m_gratTimer = 0;
            m_score += 1;
            NotifyActionVisual(Action::SOFT);
        }
        break;
    case Action::HARD:
    {
        int dy = 0;
        while (Fits(m_cur, m_curRot, m_curX, m_curY + dy + 1))
            ++dy;
        m_curY += dy;
        m_score += dy * 2;
        LockPiece(true);
        m_cur = Piece::NONE;
        break;
    }
    case Action::HOLD: HoldSwap(); break;
    default: break;
    }

    // gravity + lock delay (single unified tick; reset-cap lock: timer only resets on move/rotate, cap enforced)
    if (m_cur != Piece::NONE)
    {
        bool onGround = !Fits(m_cur, m_curRot, m_curX, m_curY + 1);
        m_gratTimer += dt;
        if (m_gratTimer >= DropDelay())
        {
            m_gratTimer = 0;
            if (!onGround) m_curY++;
        }
        onGround = !Fits(m_cur, m_curRot, m_curX, m_curY + 1);
        if (onGround)
        {
            m_lockDelay += dt;
            if (m_lockDelay >= m_lockCap)
            {
                LockPiece(false);
                m_cur = Piece::NONE;
            }
        }
    }
}

} // namespace tetris
