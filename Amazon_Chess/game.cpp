#include "game.h"
#include <algorithm>
#include <d2d1.h>
#include <windows.h>
#include <mmsystem.h>
#include <sstream>
#include <vector>
#include <string>

#pragma comment(lib, "winmm")

static std::vector<GamePiece> s_pieces;
static int s_boardSize = 10;
static bool s_aiIsBlack = true; // which color AI controls
static bool s_blackToMove = true; // black moves first
static bool s_opponentIsAI = true;
static std::vector<int> s_grid; // 0 empty, 1 piece, 2 arrow
static std::vector<std::wstring> s_history; // textual history for UI
static bool s_gameOver = false; // when true, no further moves allowed

// New: Move struct and stacks for applied / undone moves
struct MoveRecord { int fr, fc, tr, tc, ar, ac; bool isWhite; };
static std::vector<MoveRecord> s_appliedMoves; // acts as a stack of applied moves
static std::vector<MoveRecord> s_undoneMoves;  // stack of undone moves (for redo)

// new: current applied moves count (kept in sync with s_appliedMoves.size())
static int s_currentMoveIndex = 0;
// new: history changed callback
static GameHistoryChangedCallback s_historyCb = nullptr;

static inline int Index(int r, int c) { return r * s_boardSize + c; }

// helper: parse history entry of form "[W] C1 D1 C1"
static bool ParseHistoryEntry(const std::wstring& entry, int &fromR, int &fromC, int &toR, int &toC, int &arrowR, int &arrowC)
{
    size_t pos = entry.find(L"] ");
    if (pos == std::wstring::npos) return false;
    std::wstring rest = entry.substr(pos + 2);
    std::wstringstream ss(rest);
    std::wstring a,b,c;
    if (!(ss >> a >> b >> c)) return false;
    if (a.size() < 2 || b.size() < 2 || c.size() < 2) return false;
    fromC = a[0] - L'A';
    fromR = _wtoi(a.substr(1).c_str()) - 1;
    toC = b[0] - L'A';
    toR = _wtoi(b.substr(1).c_str()) - 1;
    arrowC = c[0] - L'A';
    arrowR = _wtoi(c.substr(1).c_str()) - 1;
    return true;
}

// helper to setup initial pieces/grid for current board size and AI settings
static void ResetInitialSetup()
{
    s_pieces.clear();
    s_grid.assign(s_boardSize * s_boardSize, 0);
    // NOTE: do not clear s_history here; Game_Init handles history clearing

    auto add = [&](char file, int rank, bool white)
    {
        int col = (int)(file - 'A');
        int row = rank - 1;
        if (row < 0) row = 0;
        if (row >= s_boardSize) row = s_boardSize - 1;
        s_pieces.push_back({ row, col, white });
        s_grid[Index(row,col)] = 1;
    };

    if (s_boardSize == 10)
    {
        add('D', 10, true);
        add('G', 10, true);
        add('A', 7, true);
        add('J', 7, true);
        add('D', 1, false);
        add('G', 1, false);
        add('A', 4, false);
        add('J', 4, false);
    }
    else
    {
        add('C', 1, false);
        add('F', 1, false);
        add('A', 3, false);
        add('H', 3, false);
        add('C', 8, true);
        add('F', 8, true);
        add('A', 6, true);
        add('H', 6, true);
    }
}

void Game_Init(int boardSize, bool opponentIsAI, int aiDifficulty, bool aiFirst)
{
    s_pieces.clear();
    s_boardSize = (boardSize == 8) ? 8 : 10;
    s_grid.assign(s_boardSize * s_boardSize, 0);
    // Reset textual history when creating a new game
    s_history.clear();

    s_opponentIsAI = opponentIsAI;
    bool blackIsAI = (opponentIsAI && aiFirst);
    s_aiIsBlack = blackIsAI;
    s_blackToMove = true; // black always starts
    s_gameOver = false;

    // clear move stacks when initializing the board state
    s_appliedMoves.clear();
    s_undoneMoves.clear();
    s_currentMoveIndex = 0;

    ResetInitialSetup();

    // notify UI that history / state changed (board reset)
    if (s_historyCb) s_historyCb();
}

void Game_DrawPieces(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* whiteBrush, ID2D1SolidColorBrush* blackBrush, float boardLeft, float boardTop, float tileSize, int excludeRow, int excludeCol)
{
    if (!rt) return;
    float radius = (6.0f > tileSize * 0.28f) ? 6.0f : (tileSize * 0.28f);
    for (const auto &p : s_pieces)
    {
        if (p.row == excludeRow && p.col == excludeCol) continue;
        float cx = boardLeft + (p.col + 0.5f) * tileSize;
        float cy = boardTop + (p.row + 0.5f) * tileSize;
        D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
        if (p.isWhite)
        {
            if (whiteBrush) rt->FillEllipse(e, whiteBrush);
            if (blackBrush) rt->DrawEllipse(e, blackBrush, 1.0f);
        }
        else
        {
            if (blackBrush) rt->FillEllipse(e, blackBrush);
            if (whiteBrush) rt->DrawEllipse(e, whiteBrush, 1.0f);
        }
    }
}

const std::vector<GamePiece>& Game_GetPieces() { return s_pieces; }
int Game_GetBoardSize() { return s_boardSize; }

bool Game_IsBlackToMove() { return s_blackToMove; }
void Game_ToggleTurn() { s_blackToMove = !s_blackToMove; }

bool Game_IsOccupied(int row, int col) { return s_grid[Index(row,col)] != 0; }
const GamePiece* Game_GetPieceAt(int row, int col)
{
    for (const auto &p : s_pieces) if (p.row == row && p.col == col) return &p;
    return nullptr;
}

std::vector<std::pair<int,int>> Game_GetLegalMoves(int row, int col)
{
    std::vector<std::pair<int,int>> out;
    if (s_gameOver) return out; // no moves allowed after game over
    if (row<0||col<0||row>=s_boardSize||col>=s_boardSize) return out;
    const GamePiece* p = Game_GetPieceAt(row,col);
    if (!p) return out;
    if (p->isWhite == false && s_blackToMove == false) return out;
    if (p->isWhite == true && s_blackToMove == true) return out;
    static const int d[8][2] = {{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1}};
    for (int i=0;i<8;i++)
    {
        int rr = row + d[i][0], cc = col + d[i][1];
        while (rr>=0 && rr < s_boardSize && cc>=0 && cc < s_boardSize)
        {
            if (s_grid[Index(rr,cc)] != 0) break;
            out.push_back({rr,cc});
            rr += d[i][0]; cc += d[i][1];
        }
    }
    return out;
}

std::vector<std::pair<int,int>> Game_GetLegalArrows(int fromRow, int fromCol, int excludeRow, int excludeCol)
{
    std::vector<std::pair<int,int>> out;
    if (s_gameOver) return out; // no arrows allowed after game over
    int origIdx = Index(fromRow, fromCol);
    int movedIdx = Index(excludeRow, excludeCol);
    int savedOrig = s_grid[origIdx];
    int savedMoved = s_grid[movedIdx];
    // simulate move: free origin and occupy destination
    s_grid[origIdx] = 0;
    s_grid[movedIdx] = 1;
    static const int d[8][2] = {{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1}};
    for (int i=0;i<8;i++)
    {
        int rr = excludeRow + d[i][0], cc = excludeCol + d[i][1];
        while (rr>=0 && rr < s_boardSize && cc>=0 && cc < s_boardSize)
        {
            // allow placing arrow on the original square we moved from
            if (s_grid[Index(rr,cc)] != 0) break;
            out.push_back({rr,cc});
            rr += d[i][0]; cc += d[i][1];
        }
    }
    s_grid[origIdx] = savedOrig;
    s_grid[movedIdx] = savedMoved;
    return out;
}

// helper: apply a MoveRecord to the current board (used for redo)
static void ApplyMoveRecordToBoard(const MoveRecord &m)
{
    // find the piece at the from-square (should be present)
    for (auto &p : s_pieces)
    {
        if (p.row == m.fr && p.col == m.fc && p.isWhite == m.isWhite)
        {
            // move it
            s_grid[Index(m.fr,m.fc)] = 0;
            p.row = m.tr; p.col = m.tc;
            s_grid[Index(m.tr,m.tc)] = 1;
            s_grid[Index(m.ar,m.ac)] = 2; // place arrow
            return;
        }
    }
}

// helper: undo a MoveRecord from the current board (used for undo)
static void UndoMoveRecordFromBoard(const MoveRecord &m)
{
    // remove arrow
    s_grid[Index(m.ar,m.ac)] = 0;
    // find the piece at the to-square (moved piece)
    for (auto &p : s_pieces)
    {
        if (p.row == m.tr && p.col == m.tc && p.isWhite == m.isWhite)
        {
            // move it back
            s_grid[Index(m.tr,m.tc)] = 0;
            p.row = m.fr; p.col = m.fc;
            s_grid[Index(m.fr,m.fc)] = 1;
            return;
        }
    }
}

void Game_MakeMove(int fromRow, int fromCol, int toRow, int toCol, int arrowRow, int arrowCol)
{
    if (s_gameOver) return; // don't allow moves after game over
    for (auto &p : s_pieces)
    {
        if (p.row == fromRow && p.col == fromCol)
        {
            // perform the move on board
            s_grid[Index(fromRow,fromCol)] = 0;
            p.row = toRow; p.col = toCol;
            s_grid[Index(toRow,toCol)] = 1;
            s_grid[Index(arrowRow,arrowCol)] = 2;

            // compose history entry string
            auto filecol = [](int col){ wchar_t buf[3]; buf[0] = L'A' + col; buf[1]=0; return std::wstring(buf); };
            auto rank = [&](int row){ int r = row + 1; wchar_t buf[8]; swprintf_s(buf, L"%d", r); return std::wstring(buf); };
            std::wstring entry;
            entry += (p.isWhite?L"[W] ":L"[B] ");
            entry += filecol(fromCol) + rank(fromRow) + L" ";
            entry += filecol(toCol) + rank(toRow) + L" ";
            entry += filecol(arrowCol) + rank(arrowRow);

            // If we had undone moves (redo stack), a new move should clear future moves and redo stack
            if (!s_undoneMoves.empty())
            {
                s_undoneMoves.clear();
            }
            // If textual history had entries beyond current index, truncate them
            if (s_currentMoveIndex < (int)s_history.size())
            {
                s_history.erase(s_history.begin() + s_currentMoveIndex, s_history.end());
            }

            // push to applied moves stack and textual history
            MoveRecord mr = { fromRow, fromCol, toRow, toCol, arrowRow, arrowCol, p.isWhite };
            s_appliedMoves.push_back(mr);
            s_history.push_back(entry);
            s_currentMoveIndex = (int)s_appliedMoves.size();

            Game_ToggleTurn();

            // notify UI that history changed
            if (s_historyCb) s_historyCb();
            return;
        }
    }
}

const std::vector<std::wstring>& Game_GetHistory() { return s_history; }

bool Game_IsAIBlack() { return s_aiIsBlack; }
bool Game_IsOpponentAI() { return s_opponentIsAI; }

// Rewind to keepMoves (keepMoves >= 0). If keepMoves == 0, restore initial setup.
void Game_RewindToMoveCount(int keepMoves)
{
    if (keepMoves < 0) keepMoves = 0;
    if (keepMoves > (int)s_history.size()) keepMoves = (int)s_history.size();

    // If target is less than current, undo moves
    while ((int)s_appliedMoves.size() > keepMoves)
    {
        MoveRecord m = s_appliedMoves.back();
        // undo on board
        UndoMoveRecordFromBoard(m);
        // record in undone stack for potential redo
        s_undoneMoves.push_back(m);
        s_appliedMoves.pop_back();
        s_currentMoveIndex = (int)s_appliedMoves.size();
        // flip turn back
        Game_ToggleTurn();
    }

    // If target is greater than current, redo moves if available
    while ((int)s_appliedMoves.size() < keepMoves && !s_undoneMoves.empty())
    {
        MoveRecord m = s_undoneMoves.back();
        s_undoneMoves.pop_back();
        ApplyMoveRecordToBoard(m);
        s_appliedMoves.push_back(m);
        s_currentMoveIndex = (int)s_appliedMoves.size();
        // flip turn after reapplying
        Game_ToggleTurn();
    }

    // notify UI
    if (s_historyCb) s_historyCb();
}

void Game_RewindOneStep()
{
    if (s_appliedMoves.empty()) return;
    // undo last move
    MoveRecord m = s_appliedMoves.back();
    UndoMoveRecordFromBoard(m);
    s_appliedMoves.pop_back();
    s_undoneMoves.push_back(m);
    s_currentMoveIndex = (int)s_appliedMoves.size();
    Game_ToggleTurn();

    if (s_historyCb) s_historyCb();
}

// Check whether side to move has any legal moves; if not, they lose.
int Game_CheckForWinner()
{
    if (s_gameOver)
    {
        // determine previously set winner by checking which side has moves, but we simply return 0 here
        // (caller should have stored/displayed winner). For safety, recompute winner below.
    }

    bool blackToMove = s_blackToMove;
    // for each piece of the side to move, check if any legal move exists and any arrow exists after that
    for (const auto &p : s_pieces)
    {
        // skip pieces that don't belong to the side to move
        if (p.isWhite == blackToMove) continue;
        auto moves = Game_GetLegalMoves(p.row, p.col);
        for (auto &mv : moves)
        {
            auto arrows = Game_GetLegalArrows(p.row, p.col, mv.first, mv.second);
            if (!arrows.empty()) return 0; // found at least one full move
        }
    }
    // no moves for side to move => they lose
    s_gameOver = true;
    if (blackToMove) return 1; // white wins
    return 2; // black wins
}

// New API implementations
void Game_SetHistoryChangedCallback(GameHistoryChangedCallback cb)
{
    s_historyCb = cb;
}

int Game_GetCurrentMoveIndex() { return s_currentMoveIndex; }
int Game_GetTotalMoves() { return (int)s_history.size(); }
bool Game_CanStepForward() { return !s_undoneMoves.empty(); }

void Game_StepForward()
{
    if (s_undoneMoves.empty()) return;
    MoveRecord m = s_undoneMoves.back();
    s_undoneMoves.pop_back();
    ApplyMoveRecordToBoard(m);
    s_appliedMoves.push_back(m);
    s_currentMoveIndex = (int)s_appliedMoves.size();
    Game_ToggleTurn();
    if (s_historyCb) s_historyCb();
}

void Game_StepBackward()
{
    if (s_appliedMoves.empty()) return;
    MoveRecord m = s_appliedMoves.back();
    UndoMoveRecordFromBoard(m);
    s_appliedMoves.pop_back();
    s_undoneMoves.push_back(m);
    s_currentMoveIndex = (int)s_appliedMoves.size();
    Game_ToggleTurn();
    if (s_historyCb) s_historyCb();
}
