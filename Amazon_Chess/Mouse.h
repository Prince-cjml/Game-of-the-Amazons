#pragma once
#include <d2d1.h>

// Mouse module API
void Mouse_OnMove(float x, float y);
void Mouse_OnLeave();
void Mouse_Draw(ID2D1RenderTarget* rt);

// Set cursor appearance
void Mouse_SetWhiteCursor(bool white);
void Mouse_SetParsing(bool parsing);

// Provide D2D factory for geometry creation
void Mouse_SetFactory(ID2D1Factory* factory);

// Cleanup
void Mouse_Cleanup();
