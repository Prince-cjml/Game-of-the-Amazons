#include "Mouse.h"
#include <wrl/client.h>
#include <d2d1.h>
#include <windows.h>
#include <cmath>

using Microsoft::WRL::ComPtr;

static float g_mouseX = 0.0f;
static float g_mouseY = 0.0f;
static bool  g_mouseInside = false;
static bool  g_whiteCursor = true; // true => white cursor, false => black cursor
static bool  g_parsing = false; // parsing flag (ignored visually for now)

static ComPtr<ID2D1Factory> g_factory; // provided by main

// cached brushes
static ComPtr<ID2D1SolidColorBrush> g_brushFill;
static ComPtr<ID2D1SolidColorBrush> g_brushEdge;
static ComPtr<ID2D1SolidColorBrush> g_brushShadow;

void Mouse_OnMove(float x, float y)
{
    g_mouseX = x; g_mouseY = y; g_mouseInside = true;
}
void Mouse_OnLeave()
{
    g_mouseInside = false;
}

void Mouse_SetWhiteCursor(bool white)
{
    g_whiteCursor = white;
    g_brushFill.Reset();
    g_brushEdge.Reset();
}

void Mouse_SetParsing(bool parsing)
{
    g_parsing = parsing;
}

void Mouse_Cleanup()
{
    g_brushFill.Reset();
    g_brushEdge.Reset();
    g_brushShadow.Reset();
    g_factory.Reset();
}

void Mouse_SetFactory(ID2D1Factory* factory)
{
    g_factory = factory;
}

static void EnsureBrushes(ID2D1RenderTarget* rt)
{
    if (!g_brushFill)
    {
        D2D1_COLOR_F cf = g_whiteCursor ? D2D1::ColorF(1,1,1,1) : D2D1::ColorF(0,0,0,1);
        rt->CreateSolidColorBrush(cf, &g_brushFill);
    }
    if (!g_brushEdge)
    {
        rt->CreateSolidColorBrush(D2D1::ColorF(0.06f,0.06f,0.06f,0.95f), &g_brushEdge);
    }
    if (!g_brushShadow)
    {
        rt->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.25f), &g_brushShadow);
    }
}

static void DrawStandardCursor(ID2D1RenderTarget* rt)
{
    // standard triangular cursor scaled by DPI
    float dpiX=96.0f, dpiY=96.0f; rt->GetDpi(&dpiX, &dpiY);
    float scale = dpiX / 96.0f; if (scale < 0.6f) scale = 0.6f;
    float w = 14.0f * scale; float h = 22.0f * scale;

    D2D1_POINT_2F p0 = D2D1::Point2F(g_mouseX, g_mouseY);
    D2D1_POINT_2F p1 = D2D1::Point2F(g_mouseX + w, g_mouseY + h*0.5f);
    D2D1_POINT_2F p2 = D2D1::Point2F(g_mouseX + w*0.4f, g_mouseY + h*0.6f);

    // shadow offset
    ComPtr<ID2D1PathGeometry> pgShadow;
    if (g_factory && SUCCEEDED(g_factory->CreatePathGeometry(&pgShadow)))
    {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(pgShadow->Open(&sink)))
        {
            sink->BeginFigure(D2D1::Point2F(p0.x+2.0f, p0.y+2.0f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(p1.x+2.0f, p1.y+2.0f));
            sink->AddLine(D2D1::Point2F(p2.x+2.0f, p2.y+2.0f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            rt->FillGeometry(pgShadow.Get(), g_brushShadow.Get());
        }
    }

    // main triangle
    ComPtr<ID2D1PathGeometry> pg;
    if (g_factory && SUCCEEDED(g_factory->CreatePathGeometry(&pg)))
    {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(pg->Open(&sink)))
        {
            sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(p1);
            sink->AddLine(p2);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();

            EnsureBrushes(rt);
            rt->FillGeometry(pg.Get(), g_brushFill.Get());
            rt->DrawGeometry(pg.Get(), g_brushEdge.Get(), 1.4f);
        }
    }
}

void Mouse_Draw(ID2D1RenderTarget* rt)
{
    if (!g_mouseInside || !rt) return;
    EnsureBrushes(rt);
    // draw standard cursor
    DrawStandardCursor(rt);
}
