#include "board.h"
#include "game.h"
// #include "Mouse.h"  // custom mouse removed; use system cursor
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <commdlg.h>
#include <shellapi.h>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "windowscodecs")
#pragma comment(lib, "comdlg32")
#pragma comment(lib, "shell32")

using Microsoft::WRL::ComPtr;

static ComPtr<ID2D1Factory>            g_pD2DFactory;
static ComPtr<ID2D1HwndRenderTarget>  g_pRenderTarget;
static ComPtr<ID2D1SolidColorBrush>   g_pLightBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pDarkBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pLineBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pHoverBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pBeadBrush; // decorative beads
static ComPtr<IDWriteFactory>         g_pDWriteFactory;
static ComPtr<IDWriteTextFormat>      g_pTextFormatCenter; // for letters (centered)
static ComPtr<IDWriteTextFormat>      g_pTextFormatTrailing; // for numbers (right-aligned)
static ComPtr<IWICImagingFactory>     g_pWICFactory;
static ComPtr<ID2D1Bitmap>            g_pLightBitmap;
static ComPtr<ID2D1Bitmap>            g_pDarkBitmap;

// hover state
static int g_hoverRow = -1;
static int g_hoverCol = -1;
static float g_boardLeft = 0.0f;
static float g_boardTop = 0.0f;
static float g_tileSize = 0.0f;
static bool  g_mouseInside = false;

// in-game widget state
static bool g_widgetVisible = false;
static D2D1_RECT_F g_widgetRect = D2D1::RectF();
static D2D1_RECT_F g_btnMenuRect = D2D1::RectF();
static D2D1_RECT_F g_btnSaveRect = D2D1::RectF();
static D2D1_RECT_F g_sliderRect = D2D1::RectF();
static D2D1_RECT_F g_sliderTrackRect = D2D1::RectF(); // actual track used for interaction
static float g_bgmVolume = 0.6f; // 0.0 - 1.0
static bool g_draggingSlider = false;

// menu button (window top-right)
static D2D1_RECT_F g_menuButtonRectWindow = D2D1::RectF();

// callback to notify app to change mode
static ModeChangeCallback g_modeCb = nullptr;

void Board_SetModeChangeCallback(ModeChangeCallback cb) { g_modeCb = cb; }

// Game configuration (make available before rendering code)
static int g_boardN = 10; // current board size (6/8/10)
static bool g_opponentIsAI = true;
static int  g_aiDifficulty = 1;

void Board_StartNewGame(int boardSize, bool opponentIsAI, int aiDifficulty)
{
    if (boardSize != 6 && boardSize != 8 && boardSize != 10) boardSize = 8;
    g_boardN = boardSize;
    g_opponentIsAI = opponentIsAI;
    g_aiDifficulty = (aiDifficulty >= 0) ? aiDifficulty : 1;
    g_hoverRow = -1; g_hoverCol = -1;
    g_widgetVisible = false;
    g_draggingSlider = false;
}

// Create device-independent resources
HRESULT Board_CreateDeviceIndependentResources()
{
    HRESULT hr = S_OK;
    if (!g_pD2DFactory)
    {
        D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
        options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
        ID2D1Factory* pFactory = nullptr;
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options, reinterpret_cast<void**>(&pFactory));
        if (SUCCEEDED(hr) && pFactory)
        {
            g_pD2DFactory.Attach(pFactory);
        }
        else
        {
            if (pFactory) pFactory->Release();
            return hr;
        }
    }
    if (!g_pDWriteFactory)
    {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(g_pDWriteFactory.GetAddressOf()));
        if (FAILED(hr)) return hr;
    }
    if (!g_pWICFactory)
    {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pWICFactory));
        if (FAILED(hr)) return hr;
    }
    return hr;
}

void DiscardDeviceResources()
{
    g_pRenderTarget.Reset();
    g_pLightBrush.Reset();
    g_pDarkBrush.Reset();
    g_pLineBrush.Reset();
    g_pHoverBrush.Reset();
    g_pBeadBrush.Reset();
    g_pLightBitmap.Reset();
    g_pDarkBitmap.Reset();
    g_pTextFormatCenter.Reset();
    g_pTextFormatTrailing.Reset();
}

HRESULT CreateDeviceResources(HWND hwnd)
{
    HRESULT hr = S_OK;
    if (g_pRenderTarget) return S_OK;

    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    hr = g_pD2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, size),
        &g_pRenderTarget);
    if (FAILED(hr)) return hr;

    // Create basic brushes
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.94f, 0.86f, 0.7f), &g_pLightBrush); // light wood
    if (FAILED(hr)) return hr;
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.68f, 0.50f, 0.30f), &g_pDarkBrush); // dark wood
    if (FAILED(hr)) return hr;
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.06f, 0.06f, 0.8f), &g_pLineBrush);
    if (FAILED(hr)) return hr;
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.6f, 0.9f, 0.35f), &g_pHoverBrush);
    if (FAILED(hr)) return hr;
    // decorative beads (goldenish)
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.75f, 0.58f, 0.20f), &g_pBeadBrush);
    if (FAILED(hr)) return hr;

    return hr;
}

HRESULT LoadBitmapFromFile(ID2D1RenderTarget* pRT, IWICImagingFactory* pIWICFactory, PCWSTR uri, ID2D1Bitmap** ppBitmap)
{
    if (!pRT || !pIWICFactory || !ppBitmap) return E_INVALIDARG;
    *ppBitmap = nullptr;
    HRESULT hr = S_OK;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = pIWICFactory->CreateDecoderFromFilename(uri, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return hr;

    ComPtr<IWICFormatConverter> converter;
    hr = pIWICFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return hr;

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return hr;

    hr = pRT->CreateBitmapFromWicBitmap(converter.Get(), nullptr, ppBitmap);
    return hr;
}

void Board_OnResize(int width, int height)
{
    if (g_pRenderTarget)
    {
        D2D1_SIZE_U size = D2D1::SizeU(width, height);
        g_pRenderTarget->Resize(size);
    }
}

static void FillTriangle(ID2D1RenderTarget* rt, D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c, ID2D1Brush* brush)
{
    ComPtr<ID2D1PathGeometry> pg;
    if (SUCCEEDED(g_pD2DFactory->CreatePathGeometry(&pg)))
    {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(pg->Open(&sink)))
        {
            sink->BeginFigure(a, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(b);
            sink->AddLine(c);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            rt->FillGeometry(pg.Get(), brush);
        }
    }
}

void Board_Render(HWND hwnd)
{
    if (FAILED(CreateDeviceResources(hwnd))) return;
    g_pRenderTarget->BeginDraw();

    D2D1_SIZE_F rtSize = g_pRenderTarget->GetSize();
    g_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::AntiqueWhite));

    // calculate sizes dynamically
    float leftMargin = max(48.0f, rtSize.width * 0.05f);
    float bottomMargin = leftMargin;
    float rightMargin = rtSize.width * 0.02f;
    float topMargin = rtSize.height * 0.02f;

    float availW = rtSize.width - leftMargin - rightMargin;
    float availH = rtSize.height - topMargin - bottomMargin;
    float boardSize = min(availW, availH);

    g_boardLeft = leftMargin + (availW - boardSize) / 2.0f;
    g_boardTop = topMargin + (availH - boardSize) / 2.0f;
    D2D1_RECT_F boardRect = D2D1::RectF(g_boardLeft, g_boardTop, g_boardLeft + boardSize, g_boardTop + boardSize);

    // compute tile size and outer border padding early so border fill doesn't overwrite tiles
    int N = Game_GetBoardSize();
    // keep local cache in sync
    if (N != g_boardN) g_boardN = N;
    g_tileSize = (boardSize > 0 && N > 0) ? boardSize / N : 0;
    float borderPad = max(6.0f, g_tileSize * 0.12f);
    D2D1_RECT_F outer = D2D1::RectF(boardRect.left - borderPad, boardRect.top - borderPad,
                                    boardRect.right + borderPad, boardRect.bottom + borderPad);

    // fill outer area (rounded) with dark wood so beads pop, but do it BEFORE drawing the board and tiles
    if (g_pDarkBrush)
    {
        g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(outer, 8.0f, 8.0f), g_pDarkBrush.Get());
    }

    // Draw board background (on top of outer fill)
    if (g_pLightBrush) g_pRenderTarget->FillRectangle(boardRect, g_pLightBrush.Get());

    // draw tiles
    for (int r = 0; r < N; ++r)
    {
        for (int c = 0; c < N; ++c)
        {
            bool isLight = ((r + c) % 2 == 0);
            D2D1_RECT_F rct = D2D1::RectF(g_boardLeft + c * g_tileSize, g_boardTop + r * g_tileSize,
                                         g_boardLeft + (c + 1) * g_tileSize, g_boardTop + (r + 1) * g_tileSize);
            // Procedural fill using brushes
            if (isLight)
            {
                if (g_pLightBrush)
                    g_pRenderTarget->FillRectangle(rct, g_pLightBrush.Get());
            }
            else
            {
                if (g_pDarkBrush)
                    g_pRenderTarget->FillRectangle(rct, g_pDarkBrush.Get());
            }

            g_pRenderTarget->DrawRectangle(rct, g_pLineBrush.Get(), 0.6f);
        }
    }

    // decorative outer border and beads
    if (g_pBeadBrush && g_pLineBrush)
    {
        // draw rounded outer frame outline (we already filled the outer area)
        g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(outer, 8.0f, 8.0f), g_pLineBrush.Get(), 2.2f);

        // beads: one per tile center along each side
        float beadRadius = max(3.0f, g_tileSize * 0.06f);
        // top and bottom
        for (int c = 0; c < N; ++c)
        {
            float cx = g_boardLeft + (c + 0.5f) * g_tileSize;
            float topY = boardRect.top - borderPad * 0.6f - beadRadius;
            float botY = boardRect.bottom + borderPad * 0.6f + beadRadius;
            D2D1_ELLIPSE topBead = D2D1::Ellipse(D2D1::Point2F(cx, topY), beadRadius, beadRadius);
            D2D1_ELLIPSE botBead = D2D1::Ellipse(D2D1::Point2F(cx, botY), beadRadius, beadRadius);
            g_pRenderTarget->FillEllipse(topBead, g_pBeadBrush.Get());
            g_pRenderTarget->DrawEllipse(topBead, g_pLineBrush.Get(), 1.0f);
            g_pRenderTarget->FillEllipse(botBead, g_pBeadBrush.Get());
            g_pRenderTarget->DrawEllipse(botBead, g_pLineBrush.Get(), 1.0f);
        }
        // left and right
        for (int r = 0; r < N; ++r)
        {
            float cy = g_boardTop + (r + 0.5f) * g_tileSize;
            float leftX = boardRect.left - borderPad * 0.6f - beadRadius;
            float rightX = boardRect.right + borderPad * 0.6f + beadRadius;
            D2D1_ELLIPSE leftBead = D2D1::Ellipse(D2D1::Point2F(leftX, cy), beadRadius, beadRadius);
            D2D1_ELLIPSE rightBead = D2D1::Ellipse(D2D1::Point2F(rightX, cy), beadRadius, beadRadius);
            g_pRenderTarget->FillEllipse(leftBead, g_pBeadBrush.Get());
            g_pRenderTarget->DrawEllipse(leftBead, g_pLineBrush.Get(), 1.0f);
            g_pRenderTarget->FillEllipse(rightBead, g_pBeadBrush.Get());
            g_pRenderTarget->DrawEllipse(rightBead, g_pLineBrush.Get(), 1.0f);
        }
    }

    // compute menu button rect anchored to board top-right so it's always visible in game session
    {
        float btnSize = max(36.0f, g_tileSize * 1.0f);
        float margin = 12.0f;
        // anchor to window top-right instead of board
        float left = rtSize.width - margin - btnSize;
        float top = margin;
        g_menuButtonRectWindow = D2D1::RectF(left, top, left + btnSize, top + btnSize);
    }

    // draw menu button top-right
    if (g_pBeadBrush && g_pLineBrush)
    {
        float radius = (g_menuButtonRectWindow.right - g_menuButtonRectWindow.left) * 0.28f;
        g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(g_menuButtonRectWindow, radius, radius), g_pBeadBrush.Get());
        g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(g_menuButtonRectWindow, radius, radius), g_pLineBrush.Get(), 1.4f);
        // draw hamburger icon (three lines)
        float lx = g_menuButtonRectWindow.left + (g_menuButtonRectWindow.right - g_menuButtonRectWindow.left) * 0.22f;
        float rx = g_menuButtonRectWindow.right - (g_menuButtonRectWindow.right - g_menuButtonRectWindow.left) * 0.22f;
        float hy = (g_menuButtonRectWindow.top + g_menuButtonRectWindow.bottom) * 0.5f;
        float spacing = (g_menuButtonRectWindow.bottom - g_menuButtonRectWindow.top) * 0.18f;
        g_pRenderTarget->DrawLine(D2D1::Point2F(lx, hy - spacing), D2D1::Point2F(rx, hy - spacing), g_pLineBrush.Get(), 2.0f);
        g_pRenderTarget->DrawLine(D2D1::Point2F(lx, hy), D2D1::Point2F(rx, hy), g_pLineBrush.Get(), 2.0f);
        g_pRenderTarget->DrawLine(D2D1::Point2F(lx, hy + spacing), D2D1::Point2F(rx, hy + spacing), g_pLineBrush.Get(), 2.0f);
    }

    // hover highlight
    if (g_mouseInside && g_hoverRow >=0 && g_hoverCol >=0)
    {
        D2D1_RECT_F hr = D2D1::RectF(g_boardLeft + g_hoverCol * g_tileSize, g_boardTop + g_hoverRow * g_tileSize,
                                     g_boardLeft + (g_hoverCol + 1) * g_tileSize, g_boardTop + (g_hoverRow + 1) * g_tileSize);
        g_pRenderTarget->FillRectangle(hr, g_pHoverBrush.Get());
        // draw a subtle highlight border
        g_pRenderTarget->DrawRectangle(hr, g_pLineBrush.Get(), 2.0f);
    }

    // prepare text formats: size scales with tile size, clamp min/max
    float fontSize = g_tileSize * 0.45f;
    if (fontSize < 10.0f) fontSize = 10.0f;
    if (fontSize > 48.0f) fontSize = 48.0f;

    // recreate formats
    g_pTextFormatCenter.Reset();
    g_pTextFormatTrailing.Reset();
    if (g_pDWriteFactory)
    {
        g_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"en-us", &g_pTextFormatCenter);
        if (g_pTextFormatCenter)
        {
            g_pTextFormatCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_pTextFormatCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        g_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"en-us", &g_pTextFormatTrailing);
        if (g_pTextFormatTrailing)
        {
            g_pTextFormatTrailing->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            g_pTextFormatTrailing->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // draw coordinates
    if (g_pTextFormatCenter && g_pTextFormatTrailing && g_pLineBrush)
    {
        // letters A-J along bottom (centered in each tile) - position below the outer border so they don't overlap
        for (int c = 0; c < N; ++c)
        {
            wchar_t letter[3] = { (wchar_t)(L'A' + c), 0 };
            D2D1_RECT_F tileRect = D2D1::RectF(g_boardLeft + c * g_tileSize, outer.bottom + 4.0f,
                                               g_boardLeft + (c + 1) * g_tileSize, outer.bottom + 4.0f + (g_tileSize * 0.5f));
            g_pRenderTarget->DrawTextW(letter, 1, g_pTextFormatCenter.Get(), tileRect, g_pLineBrush.Get());
        }
        // numbers 1-N on left
        for (int r = 0; r < N; ++r)
        {
            wchar_t num[8]; swprintf_s(num, L"%d", r + 1);
            D2D1_RECT_F numRect = D2D1::RectF(outer.left - (g_tileSize * 0.6f), g_boardTop + r * g_tileSize,
                                              outer.left - 4.0f, g_boardTop + (r + 1) * g_tileSize);
            g_pRenderTarget->DrawTextW(num, (UINT32)wcslen(num), g_pTextFormatTrailing.Get(), numRect, g_pLineBrush.Get());
        }
    }

    // render in-game widget: saved since we need to redraw it if resized
    if (g_widgetVisible)
    {
        // compute widget rect near top-right of board
        float w = max(220.0f, g_tileSize * 5.0f);
        float h = max(120.0f, g_tileSize * 2.2f);
        float left = g_menuButtonRectWindow.right - w; // align widget right edge with window button
        float top = g_menuButtonRectWindow.bottom + 8.0f;
        g_widgetRect = D2D1::RectF(left, top, left + w, top + h);

        // widget background (rounded rectangle)
        if (g_pDarkBrush)
        {
            g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(g_widgetRect, 12.0f, 12.0f), g_pDarkBrush.Get());
        }
        // widget borders
        if (g_pLineBrush)
        {
            g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(g_widgetRect, 12.0f, 12.0f), g_pLineBrush.Get(), 1.5f);
        }

        const float btnH = 36.0f;
        const float btnPad = 8.0f;
        const float lineW = 2.0f;

        // layout controls inside widget
        float cx = g_widgetRect.left + btnPad;
        float cy = g_widgetRect.top + btnPad;
        // menu button (left)
        g_btnMenuRect = D2D1::RectF(g_widgetRect.left + btnPad, g_widgetRect.top + btnPad, g_widgetRect.left + btnPad + (w - btnPad*2) * 0.28f, g_widgetRect.top + btnPad + btnH);
        // save button (right)
        g_btnSaveRect = D2D1::RectF(g_widgetRect.left + btnPad + (w - btnPad*2) * 0.32f, g_widgetRect.top + btnPad, g_widgetRect.left + btnPad + (w - btnPad*2) * 0.32f + (w - btnPad*2) * 0.36f, g_widgetRect.top + btnPad + btnH);
        // slider area below
        g_sliderRect = D2D1::RectF(g_widgetRect.left + btnPad, g_widgetRect.top + btnPad + btnH + 12.0f, g_widgetRect.right - btnPad, g_widgetRect.top + btnPad + btnH + 28.0f);

        // draw menu
        // draw widget controls (rounded buttons, volume label, solid knob)
        // draw menu (rounded)
        float corner = 12.0f; // smoother corners
        g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(g_btnMenuRect, corner, corner), g_pHoverBrush.Get());
        g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(g_btnMenuRect, corner, corner), g_pLineBrush.Get(), 1.0f);
        if (g_pTextFormatCenter)
            g_pRenderTarget->DrawTextW(L"Menu", 4, g_pTextFormatCenter.Get(), g_btnMenuRect, g_pLineBrush.Get());

        // draw save (rounded)
        g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(g_btnSaveRect, corner, corner), g_pHoverBrush.Get());
        g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(g_btnSaveRect, corner, corner), g_pLineBrush.Get(), 1.0f);
        if (g_pTextFormatCenter)
            g_pRenderTarget->DrawTextW(L"Save", 4, g_pTextFormatCenter.Get(), g_btnSaveRect, g_pLineBrush.Get());

        // allocate space for Volume label on left
        float labelW = 28.0f;
        D2D1_RECT_F labelRect = D2D1::RectF(g_widgetRect.left + btnPad, g_sliderRect.top, g_widgetRect.left + btnPad + labelW, g_sliderRect.bottom);
        // create a small text format so 'Vol' fits on one line
        if (g_pDWriteFactory)
        {
            ComPtr<IDWriteTextFormat> tfLabel;
            // use a slightly larger fixed font for the Vol label so it's readable
            float labelFontSize = 12.0f;
            if (SUCCEEDED(g_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, labelFontSize, L"en-us", &tfLabel)))
            {
                tfLabel->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                tfLabel->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                tfLabel->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                g_pRenderTarget->DrawTextW(L"Vol", 3, tfLabel.Get(), labelRect, g_pLineBrush.Get());
            }
            else
            {
                if (g_pTextFormatTrailing)
                    g_pRenderTarget->DrawTextW(L"Vol", 3, g_pTextFormatTrailing.Get(), labelRect, g_pLineBrush.Get());
            }
        }
        else
        {
            if (g_pTextFormatTrailing)
                g_pRenderTarget->DrawTextW(L"Vol", 3, g_pTextFormatTrailing.Get(), labelRect, g_pLineBrush.Get());
        }

        // draw slider track (adjusted to leave label area)
        D2D1_RECT_F trackRect = g_sliderRect;
        trackRect.left = labelRect.right + 8.0f;
        g_sliderTrackRect = trackRect; // store for interaction
        if (g_pDarkBrush)
            g_pRenderTarget->FillRectangle(trackRect, g_pDarkBrush.Get());
        // value fill
        D2D1_RECT_F valueRect = trackRect;
        valueRect.right = valueRect.left + (valueRect.right - valueRect.left) * g_bgmVolume;
        if (g_pLightBrush)
            g_pRenderTarget->FillRectangle(valueRect, g_pLightBrush.Get());
        // solid knob (larger)
        float knobR = 12.0f;
        float knobX = valueRect.right;
        float knobY = (trackRect.top + trackRect.bottom) * 0.5f;
        if (g_pBeadBrush)
            g_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, knobY), knobR, knobR), g_pBeadBrush.Get());
        if (g_pLineBrush)
            g_pRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, knobY), knobR, knobR), g_pLineBrush.Get(), 1.0f);
    }

    // draw pieces
    // prepare simple brushes for piece fill/stroke
    ComPtr<ID2D1SolidColorBrush> whitePieceBrush;
    ComPtr<ID2D1SolidColorBrush> blackPieceBrush;
    g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &whitePieceBrush);
    g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.08f), &blackPieceBrush);
    Game_DrawPieces(g_pRenderTarget.Get(), whitePieceBrush.Get(), blackPieceBrush.Get(), g_boardLeft, g_boardTop, g_tileSize);

    HRESULT hrEnd = g_pRenderTarget->EndDraw();
    if (hrEnd == D2DERR_RECREATE_TARGET)
    {
        DiscardDeviceResources();
    }
}

// convert screen coords to board indices
static bool ScreenToBoard(int x, int y, int &outRow, int &outCol)
{
    if (g_tileSize <= 0.0f) return false;
    if (x < (int)g_boardLeft || y < (int)g_boardTop) return false;
    float fx = (float)x - g_boardLeft;
    float fy = (float)y - g_boardTop;
    if (fx < 0 || fy < 0) return false;
    int col = (int)(fx / g_tileSize);
    int row = (int)(fy / g_tileSize);
    if (col < 0 || col >= g_boardN || row < 0 || row >= g_boardN) return false;
    outRow = row; outCol = col; return true;
}

static bool PtInRectF(const D2D1_RECT_F &r, int x, int y)
{
    return (x >= (int)r.left && x <= (int)r.right && y >= (int)r.top && y <= (int)r.bottom);
}

void Board_OnMouseMove(int x, int y)
{
    g_mouseInside = true;
    int row, col;
    bool inside = ScreenToBoard(x, y, row, col);
    if (inside)
    {
        if (row != g_hoverRow || col != g_hoverCol)
        {
            g_hoverRow = row; g_hoverCol = col;
        }
    }
    else
    {
        g_hoverRow = -1; g_hoverCol = -1;
    }

    // if dragging slider, update value
    if (g_draggingSlider)
    {
        float pos = 0.0f;
        if (g_sliderTrackRect.right > g_sliderTrackRect.left)
        {
            pos = (float)(x - g_sliderTrackRect.left) / (g_sliderTrackRect.right - g_sliderTrackRect.left);
        }
        if (pos < 0.0f) pos = 0.0f;
        if (pos > 1.0f) pos = 1.0f;
        g_bgmVolume = pos;
    }
}

void Board_OnMouseLeave()
{
    g_mouseInside = false;
    g_hoverRow = -1; g_hoverCol = -1;
}

void Board_OnLButtonDown(int x, int y)
{
    // check menu button first
    if (PtInRectF(g_menuButtonRectWindow, x, y))
    {
        g_widgetVisible = !g_widgetVisible;
        return;
    }

    // if widget visible, check its controls
    if (g_widgetVisible)
    {
        if (PtInRectF(g_btnMenuRect, x, y))
        {
            // menu -> show/hide widget
            // request app to return to menu
            if (g_modeCb) g_modeCb();
            // hide widget
            g_widgetVisible = false;
            return;
        }
        if (PtInRectF(g_btnSaveRect, x, y))
        {
            // show Save File dialog
            WCHAR szFile[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = nullptr;
            ofn.lpstrFilter = L"Amazon Save Files\0*.amaz\0All Files\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            ofn.lpstrDefExt = L"amaz";
            if (GetSaveFileNameW(&ofn))
            {
                // write a tiny placeholder save
                HANDLE h = CreateFileW(szFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE)
                {
                    const char *txt = "Game saved (placeholder).";
                    DWORD written = 0;
                    WriteFile(h, txt, (DWORD)strlen(txt), &written, nullptr);
                    CloseHandle(h);
                }
            }
            return;
        }
        // slider click: if inside slider track, start dragging and set value
        if (PtInRectF(g_sliderTrackRect, x, y))
        {
            float pos = 0.0f;
            if (g_sliderTrackRect.right > g_sliderTrackRect.left)
                pos = (float)(x - g_sliderTrackRect.left) / (g_sliderTrackRect.right - g_sliderTrackRect.left);
            if (pos < 0.0f) pos = 0.0f;
            if (pos > 1.0f) pos = 1.0f;
            g_bgmVolume = pos;
            g_draggingSlider = true;
            return;
        }
    }

    int row, col;
    if (ScreenToBoard(x, y, row, col))
    {
        // simple visual feedback: toggle highlight cell to dark brush temporarily (could be expanded)
        g_hoverRow = row; g_hoverCol = col;
    }
}

void Board_OnLButtonUp(int x, int y)
{
    if (g_draggingSlider)
    {
        g_draggingSlider = false;
        return;
    }
}

void Board_OnMouseWheel(int delta)
{
    // ignore for now
}

void Board_Cleanup()
{
    DiscardDeviceResources();
    g_pDWriteFactory.Reset();
    g_pD2DFactory.Reset();
}
