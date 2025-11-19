#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <wrl/client.h>

// Board module public API
HRESULT Board_CreateDeviceIndependentResources();
void    Board_Cleanup();
void    Board_OnResize(int width, int height);
void    Board_Render(HWND hwnd);

// Start a new game with options
void    Board_StartNewGame(int boardSize, bool opponentIsAI, int aiDifficulty);

// Mouse interaction for board
void    Board_OnMouseMove(int x, int y);
void    Board_OnMouseLeave();
void    Board_OnLButtonDown(int x, int y);
void    Board_OnLButtonUp(int x, int y);

// Callback from board to application (e.g. return to menu)
typedef void(*ModeChangeCallback)();
void    Board_SetModeChangeCallback(ModeChangeCallback cb);

using Microsoft::WRL::ComPtr;