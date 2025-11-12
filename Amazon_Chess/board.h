#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <wrl/client.h>

// Board module public API
HRESULT Board_CreateDeviceIndependentResources();
void    Board_Cleanup();
void    Board_OnResize(int width, int height);
void    Board_Render(HWND hwnd);

// Mouse interaction for board
void    Board_OnMouseMove(int x, int y);
void    Board_OnMouseLeave();
void    Board_OnLButtonDown(int x, int y);

using Microsoft::WRL::ComPtr;