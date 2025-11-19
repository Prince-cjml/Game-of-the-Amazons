#pragma once

#include <d2d1.h>
#include <vector>
#include <string>

struct GamePiece { int row; int col; bool isWhite; };

// Initialize game state for a new game
void Game_Init(int boardSize, bool opponentIsAI, int aiDifficulty, bool aiFirst);

// Draw pieces; renderer provides a render target and brushes
// excludeRow/excludeCol if set will skip drawing the piece at that location (used for UI overlays)
void Game_DrawPieces(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* whiteBrush, ID2D1SolidColorBrush* blackBrush, float boardLeft, float boardTop, float tileSize, int excludeRow = -1, int excludeCol = -1);

// Query pieces
const std::vector<GamePiece>& Game_GetPieces();
int Game_GetBoardSize();

// occupancy
bool Game_IsOccupied(int row, int col); // piece or arrow
// piece at
const GamePiece* Game_GetPieceAt(int row, int col); // nullptr if none

// legal moves (queen-like moves) for a piece at row,col
std::vector<std::pair<int,int>> Game_GetLegalMoves(int row, int col);
// legal arrow placements from a given square (queen-like)
std::vector<std::pair<int,int>> Game_GetLegalArrows(int fromRow, int fromCol, int excludeRow, int excludeCol);

// make a move (assumes validity): move piece from->to and place arrow at arrowRow/arrowCol
void Game_MakeMove(int fromRow, int fromCol, int toRow, int toCol, int arrowRow, int arrowCol);

// turn state
bool Game_IsBlackToMove();
void Game_ToggleTurn();

// history access
const std::vector<std::wstring>& Game_GetHistory();

// which side AI controls (if any)
bool Game_IsAIBlack();

// whether opponent is AI
bool Game_IsOpponentAI();

// Check end condition: returns 0 if no winner yet, 1 if White wins, 2 if Black wins
int Game_CheckForWinner();

// Rewind functions: set state to after first N moves (N >= 0). If N == 0, restores initial setup.
void Game_RewindToMoveCount(int keepMoves);
// Rewind a single move (if any)
void Game_RewindOneStep();

// New: history changed callback (notify UI)
typedef void (*GameHistoryChangedCallback)();
void Game_SetHistoryChangedCallback(GameHistoryChangedCallback cb);

// New: current move index and stepping forward/back
int Game_GetCurrentMoveIndex(); // number of moves currently applied (0 .. history.size())
int Game_GetTotalMoves();
bool Game_CanStepForward();
void Game_StepForward();
void Game_StepBackward();
