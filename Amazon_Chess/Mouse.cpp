#include "Mouse.h"
#include <wrl/client.h>
#include <d2d1.h>
#include <windows.h>

using Microsoft::WRL::ComPtr;

static float g_mouseX = 0.0f;
static float g_mouseY = 0.0f;
static bool  g_mouseInside = false;
static bool  g_whiteCursor = true;
static bool  g_parsing = false;

static ComPtr<ID2D1Factory> g_factory;
static ComPtr<ID2D1SolidColorBrush> g_brushFill;
static ComPtr<ID2D1SolidColorBrush> g_brushEdge;
static ComPtr<ID2D1SolidColorBrush> g_brushShadow;

void Mouse_OnMove(float x, float y) { g_mouseX = x; g_mouseY = y; g_mouseInside = true; }
void Mouse_OnLeave() { g_mouseInside = false; }
void Mouse_SetWhiteCursor(bool white) { g_whiteCursor = white; g_brushFill.Reset(); g_brushEdge.Reset(); }
void Mouse_SetParsing(bool parsing) { g_parsing = parsing; }
void Mouse_Cleanup() { g_brushFill.Reset(); g_brushEdge.Reset(); g_brushShadow.Reset(); g_factory.Reset(); }
void Mouse_SetFactory(ID2D1Factory* factory) { g_factory = factory; }

static void EnsureBrushes(ID2D1RenderTarget* rt)
{
    if (!g_brushFill)
    {
        // Emoji-like skin when whiteCursor==true, otherwise dark color
        D2D1_COLOR_F cf = g_whiteCursor ? D2D1::ColorF(0.98f,0.86f,0.64f,1.0f) : D2D1::ColorF(0.12f,0.12f,0.12f,1.0f);
        rt->CreateSolidColorBrush(cf, &g_brushFill);
    }
    if (!g_brushEdge)
    {
        D2D1_COLOR_F edge = g_whiteCursor ? D2D1::ColorF(0.06f,0.06f,0.06f,0.95f) : D2D1::ColorF(1,1,1,0.95f);
        rt->CreateSolidColorBrush(edge, &g_brushEdge);
    }
    if (!g_brushShadow)
    {
        rt->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.22f), &g_brushShadow);
    }
}

static void DrawHandCursor(ID2D1RenderTarget* rt)
{
    if (!g_factory) return;
    EnsureBrushes(rt);

    float dpiX=96.0f, dpiY=96.0f; rt->GetDpi(&dpiX, &dpiY);
    float scale = dpiX/96.0f; if (scale < 0.6f) scale = 0.6f;

    // layout: tip corresponds to top of middle finger; place shapes relative to that
    float fingerW = 8.0f * scale;
    float fingerH = 18.0f * scale;
    float spacing = 2.0f * scale;
    float palmW = 20.0f * scale;
    float palmH = 18.0f * scale;

    // compute top-left origin for fingers such that middle finger tip is at mouse position
    float topX = g_mouseX - (fingerW + spacing + fingerW) * 0.5f; // center fingers around mouse
    float topY = g_mouseY;

    // Draw shadow for whole hand (rounded palm + fingers)
    D2D1_ROUNDED_RECT palmShadow = D2D1::RoundedRect(D2D1::RectF(topX + fingerW*0.5f + spacing, topY + fingerH, topX + fingerW*0.5f + spacing + palmW, topY + fingerH + palmH), 4.0f*scale, 4.0f*scale);
    rt->FillRoundedRectangle(palmShadow, g_brushShadow.Get());

    // fingers: draw four rounded rects (left to right)
    for (int i = 0; i < 4; ++i)
    {
        float fx = topX + i * (fingerW + spacing);
        D2D1_ROUNDED_RECT fr = D2D1::RoundedRect(D2D1::RectF(fx, topY, fx + fingerW, topY + fingerH), 3.0f*scale, 3.0f*scale);
        rt->FillRoundedRectangle(fr, g_brushFill.Get());
        rt->DrawRoundedRectangle(fr, g_brushEdge.Get(), 1.0f * scale);
    }

    // palm under fingers
    float palmX = topX + fingerW*0.5f + spacing;
    float palmY = topY + fingerH - (palmH * 0.2f);
    D2D1_ROUNDED_RECT palm = D2D1::RoundedRect(D2D1::RectF(palmX, palmY, palmX + palmW, palmY + palmH), 6.0f*scale, 6.0f*scale);
    rt->FillRoundedRectangle(palm, g_brushFill.Get());
    rt->DrawRoundedRectangle(palm, g_brushEdge.Get(), 1.2f * scale);

    // thumb: small rounded rect on left lower side
    D2D1_ROUNDED_RECT thumb = D2D1::RoundedRect(D2D1::RectF(palmX - (fingerW*0.6f), palmY + palmH*0.2f, palmX + fingerW*0.15f, palmY + palmH*0.65f), 3.0f*scale, 3.0f*scale);
    rt->FillRoundedRectangle(thumb, g_brushFill.Get());
    rt->DrawRoundedRectangle(thumb, g_brushEdge.Get(), 1.0f * scale);
}

void Mouse_Draw(ID2D1RenderTarget* rt)
{
    if (!g_mouseInside || !rt) return;
    DrawHandCursor(rt);
}
