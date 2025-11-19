#define NOMINMAX 1
#include "menu.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <stdint.h>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")

using Microsoft::WRL::ComPtr;

static ComPtr<ID2D1Factory>        s_factory;
static ComPtr<ID2D1HwndRenderTarget> s_rt;
static ComPtr<ID2D1SolidColorBrush>  s_brushBgLight;   // desaturated light
static ComPtr<ID2D1SolidColorBrush>  s_brushBgDark;    // desaturated dark
static ComPtr<ID2D1SolidColorBrush>  s_brushButtonFill;
static ComPtr<ID2D1SolidColorBrush>  s_brushButtonEdge;
static ComPtr<ID2D1SolidColorBrush>  s_brushText;
static ComPtr<IDWriteFactory>        s_dwrite;
static ComPtr<IDWriteTextFormat>     s_tfTitle;
static ComPtr<IDWriteTextFormat>     s_tfButton;

static int s_width = 800;
static int s_height = 600;
static bool s_hasResume = false;

// button layout
static D2D1_RECT_F s_btnStart = D2D1::RectF();
static D2D1_RECT_F s_btnNewGame = D2D1::RectF();
static D2D1_RECT_F s_btnLoad  = D2D1::RectF();
static D2D1_RECT_F s_btnHelp  = D2D1::RectF();
static int s_hoverButton = -1; // index varies depending on s_hasResume

HRESULT Menu_CreateDeviceIndependentResources()
{
    HRESULT hr = S_OK;
    if (!s_factory)
    {
        D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
        options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
        ID2D1Factory* f = nullptr;
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options, reinterpret_cast<void**>(&f));
        if (SUCCEEDED(hr) && f)
        {
            s_factory.Attach(f);
        }
        else
        {
            if (f) f->Release();
            return hr;
        }
    }

    if (!s_dwrite)
    {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(s_dwrite.GetAddressOf()));
        if (FAILED(hr)) return hr;
    }

    return S_OK;
}

void Menu_Cleanup()
{
    s_rt.Reset();
    s_factory.Reset();
    s_brushBgLight.Reset();
    s_brushBgDark.Reset();
    s_brushButtonFill.Reset();
    s_brushButtonEdge.Reset();
    s_brushText.Reset();
    s_dwrite.Reset();
    s_tfTitle.Reset();
    s_tfButton.Reset();
}

void Menu_SetHasResume(bool hasResume)
{
    s_hasResume = hasResume;
}

bool Menu_HasResume()
{
    return s_hasResume;
}

static HRESULT EnsureDeviceResources(HWND hwnd)
{
    if (s_rt) return S_OK;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    s_width = (w > 1) ? w : 1;
    s_height = (h > 1) ? h : 1;

    D2D1_SIZE_U size = D2D1::SizeU((uint32_t)s_width, (uint32_t)s_height);
    HRESULT hr = s_factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, size), &s_rt);
    if (FAILED(hr)) return hr;

    // low-saturation board-like colors (muted browns / gray-brown)
    s_rt->CreateSolidColorBrush(D2D1::ColorF(0.86f, 0.82f, 0.78f), &s_brushBgLight); // very light desaturated
    s_rt->CreateSolidColorBrush(D2D1::ColorF(0.72f, 0.68f, 0.64f), &s_brushBgDark);  // darker desaturated

    // buttons and text
    s_rt->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.40f, 0.70f, 0.95f), &s_brushButtonFill);
    s_rt->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.06f, 0.06f, 0.9f), &s_brushButtonEdge);
    s_rt->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.98f, 0.98f), &s_brushText);

    // text formats (sizes will be reasonable defaults; resized later)
    FLOAT titleSize = std::max(20.0f, s_height * 0.08f);
    FLOAT btnSize = std::max(12.0f, s_height * 0.04f);

    s_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, titleSize, L"en-us", &s_tfTitle);
    s_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, btnSize, L"en-us", &s_tfButton);

    if (s_tfTitle)
    {
        s_tfTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        s_tfTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (s_tfButton)
    {
        s_tfButton->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        s_tfButton->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    return S_OK;
}

void Menu_OnResize(int width, int height)
{
    int w = width;
    int h = height;
    s_width = (w > 1) ? w : 1;
    s_height = (h > 1) ? h : 1;
    if (s_rt)
    {
        s_rt->Resize(D2D1::SizeU((uint32_t)s_width, (uint32_t)s_height));
        s_rt.Reset(); // will recreate with new size on next render
    }
}

static void LayoutButtons()
{
    // central column layout
    float w = (float)s_width;
    float h = (float)s_height;
    float buttonW = std::min(420.0f, w * 0.45f);
    float buttonH = std::max(40.0f, h * 0.08f);
    float centerX = w * 0.5f;
    float startY = h * 0.38f;

    float spacing = std::max(12.0f, buttonH * 0.35f);

    if (s_hasResume)
    {
        // Resume, New Game, Load, Help
        s_btnStart = D2D1::RectF(centerX - buttonW * 0.5f, startY, centerX + buttonW * 0.5f, startY + buttonH); // Resume
        s_btnNewGame = D2D1::RectF(centerX - buttonW * 0.5f, startY + (buttonH + spacing) * 1, centerX + buttonW * 0.5f, startY + (buttonH + spacing) * 1 + buttonH);
        s_btnLoad  = D2D1::RectF(centerX - buttonW * 0.5f, startY + (buttonH + spacing) * 2, centerX + buttonW * 0.5f, startY + (buttonH + spacing) * 2 + buttonH);
        s_btnHelp  = D2D1::RectF(centerX - buttonW * 0.5f, startY + (buttonH + spacing) * 3, centerX + buttonW * 0.5f, startY + (buttonH + spacing) * 3 + buttonH);
    }
    else
    {
        // Start, Load, Help
        s_btnStart = D2D1::RectF(centerX - buttonW * 0.5f, startY, centerX + buttonW * 0.5f, startY + buttonH);
        s_btnLoad  = D2D1::RectF(centerX - buttonW * 0.5f, startY + (buttonH + spacing) * 1, centerX + buttonW * 0.5f, startY + (buttonH + spacing) * 1 + buttonH);
        s_btnHelp  = D2D1::RectF(centerX - buttonW * 0.5f, startY + (buttonH + spacing) * 2, centerX + buttonW * 0.5f, startY + (buttonH + spacing) * 2 + buttonH);
        s_btnNewGame = D2D1::RectF();
    }
}

static bool PtInRect(const D2D1_RECT_F &r, float x, float y)
{
    return (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom);
}

void Menu_OnMouseMove(int x, int y)
{
    LayoutButtons();
    int hover = -1;
    if (PtInRect(s_btnStart, (float)x, (float)y)) hover = 0;
    else if (s_hasResume && PtInRect(s_btnNewGame, (float)x, (float)y)) hover = 1;
    else if (PtInRect(s_btnLoad, (float)x, (float)y)) hover = s_hasResume ? 2 : 1;
    else if (PtInRect(s_btnHelp, (float)x, (float)y)) hover = s_hasResume ? 3 : 2;
    if (hover != s_hoverButton)
    {
        s_hoverButton = hover;
        // caller should InvalidateRect to trigger redraw if integrating
    }
}

MenuAction Menu_OnLButtonDown(int x, int y)
{
    LayoutButtons();
    if (PtInRect(s_btnStart, (float)x, (float)y)) return MENU_ACTION_START; // start or resume
    if ( s_hasResume && PtInRect(s_btnNewGame, (float)x, (float)y)) return MENU_ACTION_NEWGAME;
    if (PtInRect(s_btnLoad,  (float)x, (float)y)) return MENU_ACTION_LOAD;
    if (PtInRect(s_btnHelp,  (float)x, (float)y)) return MENU_ACTION_HELP;
    return MENU_ACTION_NONE;
}

static void DrawButtonInternal(ID2D1RenderTarget* rt, const D2D1_RECT_F &r, ID2D1SolidColorBrush* edgeBrush, IDWriteTextFormat* tf, ID2D1SolidColorBrush* textBrush, bool hovered)
{
    float radius = std::min((r.right - r.left) * 0.06f, (r.bottom - r.top) * 0.25f);
    // fill slightly lighter when hovered
    if (hovered)
    {
        ComPtr<ID2D1SolidColorBrush> fill;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.5f, 0.9f, 1.0f), &fill);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), fill.Get());
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), edgeBrush, 2.0f);
    }
    else
    {
        ComPtr<ID2D1SolidColorBrush> fill;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.40f, 0.70f, 0.95f), &fill);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), fill.Get());
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), edgeBrush, 1.6f);
    }

    // draw text centered
    if (tf && textBrush)
    {
        // text will be drawn by caller passing appropriate string via DrawText
    }
}

void Menu_Render(HWND hwnd)
{
    if (FAILED(Menu_CreateDeviceIndependentResources())) return;
    if (FAILED(EnsureDeviceResources(hwnd))) return;

    s_rt->BeginDraw();

    // Draw low-saturation checkerboard background (10x10 like board but muted)
    const int N = 10;
    float left = 0.0f;
    float top = 0.0f;
    float bw = (float)s_width;
    float bh = (float)s_height;
    float tileW = bw / N;
    float tileH = bh / N;

    for (int r = 0; r < N; ++r)
    {
        for (int c = 0; c < N; ++c)
        {
            bool light = ((r + c) % 2 == 0);
            D2D1_RECT_F rc = D2D1::RectF(left + c * tileW, top + r * tileH, left + (c + 1) * tileW, top + (r + 1) * tileH);
            s_rt->FillRectangle(rc, light ? s_brushBgLight.Get() : s_brushBgDark.Get());
        }
    }

    // shadowed overlay to further desaturate and focus UI
    // subtle translucent dark overlay
    ComPtr<ID2D1SolidColorBrush> overlay;
    s_rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.08f), &overlay);
    s_rt->FillRectangle(D2D1::RectF(0, 0, (FLOAT)s_width, (FLOAT)s_height), overlay.Get());

    // Layout
    LayoutButtons();

    // Title
    if (s_tfTitle && s_brushText)
    {
        WCHAR title[] = L"Game of the Amazons";
        D2D1_RECT_F titleRect = D2D1::RectF(0.0f, s_height * 0.08f, (FLOAT)s_width, s_height * 0.25f);
        s_rt->DrawTextW(title, (UINT32)wcslen(title), s_tfTitle.Get(), titleRect, s_brushText.Get());
    }

    // Buttons: draw text directly after drawing shapes
    DrawButtonInternal(s_rt.Get(), s_btnStart, s_brushButtonEdge.Get(), s_tfButton.Get(), s_brushText.Get(), s_hoverButton == 0);
    s_rt->DrawTextW(s_hasResume ? L"Resume Game" : L"Start Game", (UINT32)wcslen(s_hasResume ? L"Resume Game" : L"Start Game"), s_tfButton.Get(), s_btnStart, s_brushText.Get());

    if (s_hasResume)
    {
        DrawButtonInternal(s_rt.Get(), s_btnNewGame, s_brushButtonEdge.Get(), s_tfButton.Get(), s_brushText.Get(), s_hoverButton == 1);
        s_rt->DrawTextW(L"New Game", (UINT32)wcslen(L"New Game"), s_tfButton.Get(), s_btnNewGame, s_brushText.Get());

        DrawButtonInternal(s_rt.Get(), s_btnLoad, s_brushButtonEdge.Get(), s_tfButton.Get(), s_brushText.Get(), s_hoverButton == 2);
        s_rt->DrawTextW(L"Load Game", (UINT32)wcslen(L"Load Game"), s_tfButton.Get(), s_btnLoad, s_brushText.Get());

        DrawButtonInternal(s_rt.Get(), s_btnHelp, s_brushButtonEdge.Get(), s_tfButton.Get(), s_brushText.Get(), s_hoverButton == 3);
        s_rt->DrawTextW(L"Help / Documentation", (UINT32)wcslen(L"Help / Documentation"), s_tfButton.Get(), s_btnHelp, s_brushText.Get());
    }
    else
    {
        DrawButtonInternal(s_rt.Get(), s_btnLoad, s_brushButtonEdge.Get(), s_tfButton.Get(), s_brushText.Get(), s_hoverButton == 1);
        s_rt->DrawTextW(L"Load Game", (UINT32)wcslen(L"Load Game"), s_tfButton.Get(), s_btnLoad, s_brushText.Get());

        DrawButtonInternal(s_rt.Get(), s_btnHelp, s_brushButtonEdge.Get(), s_tfButton.Get(), s_brushText.Get(), s_hoverButton == 2);
        s_rt->DrawTextW(L"Help / Documentation", (UINT32)wcslen(L"Help / Documentation"), s_tfButton.Get(), s_btnHelp, s_brushText.Get());
    }

    // End draw
    HRESULT hr = s_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        s_rt.Reset();
    }
}
