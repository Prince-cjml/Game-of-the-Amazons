#pragma once

#include <d2d1.h>
#include <vector>

struct GamePiece { int row; int col; bool isWhite; };

// Initialize game state for a new game
void Game_Init(int boardSize, bool opponentIsAI, int aiDifficulty, bool aiFirst);

// Draw pieces; renderer provides a render target and brushes
void Game_DrawPieces(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* whiteBrush, ID2D1SolidColorBrush* blackBrush, float boardLeft, float boardTop, float tileSize);

// Query pieces (optional)
const std::vector<GamePiece>& Game_GetPieces();

// Which side AI controls
bool Game_IsAIBlack();

// current board size
int Game_GetBoardSize();
