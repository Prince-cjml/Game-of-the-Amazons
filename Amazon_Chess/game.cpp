#include "game.h"
#include <algorithm>
#include <d2d1.h>

static std::vector<GamePiece> s_pieces;
static int s_boardSize = 10;
static bool s_aiIsBlack = true; // which color AI controls

void Game_Init(int boardSize, bool opponentIsAI, int aiDifficulty, bool aiFirst)
{
    s_pieces.clear();
    s_boardSize = (boardSize == 8) ? 8 : 10; // only 8 or 10 supported

    // determine colors: black moves first
    bool blackIsAI = (opponentIsAI && aiFirst);
    s_aiIsBlack = blackIsAI;

    // helper to add piece by file/rank
    auto add = [&](char file, int rank, bool white)
    {
        int col = (int)(file - 'A');
        int row = s_boardSize - rank; // rank 1 is bottom row
        s_pieces.push_back({ row, col, white});
    };

    if (s_boardSize == 10)
    {
        // white
        add('D', 10, true);
        add('G', 10, true);
        add('A', 7, true);
        add('J', 7, true);
        // black
        add('D', 1, false);
        add('G', 1, false);
        add('A', 4, false);
        add('J', 4, false);
    }
    else // 8x8
    {
        // black pieces
        add('C', 1, false);
        add('F', 1, false);
        add('A', 3, false);
        add('H', 3, false);
        // white
        add('C', 8, true);
        add('F', 8, true);
        add('A', 6, true);
        add('H', 6, true);
    }
}

void Game_DrawPieces(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* whiteBrush, ID2D1SolidColorBrush* blackBrush, float boardLeft, float boardTop, float tileSize)
{
    if (!rt) return;
    float radius = (6.0f > tileSize * 0.28f) ? 6.0f : (tileSize * 0.28f);
    for (const auto &p : s_pieces)
    {
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

const std::vector<GamePiece>& Game_GetPieces()
{
    return s_pieces;
}

bool Game_IsAIBlack()
{
    return s_aiIsBlack;
}

int Game_GetBoardSize()
{
    return s_boardSize;
}
