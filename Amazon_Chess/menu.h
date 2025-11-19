#pragma once

// prevent Windows headers from defining min/max macros that conflict with std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#include <Windows.h>
#include <d2d1.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MenuAction
{
    MENU_ACTION_NONE = 0,
    MENU_ACTION_START = 1,
    MENU_ACTION_NEWGAME = 2,
    MENU_ACTION_LOAD = 3,
    MENU_ACTION_HELP = 4
} MenuAction;

// Lifecycle / draw
HRESULT Menu_CreateDeviceIndependentResources();
void    Menu_Cleanup();
void    Menu_OnResize(int width, int height);
void    Menu_Render(HWND hwnd);

// Input
void      Menu_OnMouseMove(int x, int y);
MenuAction Menu_OnLButtonDown(int x, int y);

// Controls
void      Menu_SetHasResume(bool hasResume);
bool      Menu_HasResume();

#ifdef __cplusplus
}
#endif
