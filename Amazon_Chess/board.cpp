#include "board.h"
#include "Mouse.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "windowscodecs")

using Microsoft::WRL::ComPtr;

static ComPtr<ID2D1Factory>            g_pD2DFactory;
static ComPtr<ID2D1HwndRenderTarget>  g_pRenderTarget;
static ComPtr<ID2D1SolidColorBrush>   g_pLightBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pDarkBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pLineBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pHoverBrush;
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
            // provide factory to Mouse module
            Mouse_SetFactory(g_pD2DFactory.Get());
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

    // Draw board background
    if (g_pLightBrush) g_pRenderTarget->FillRectangle(boardRect, g_pLightBrush.Get());

    // attempt to load bitmaps once if available
    if (!g_pLightBitmap)
    {
        // user-supplied textures expected at resources\board_light.png and resources\board_dark.png
        wchar_t lightPath[MAX_PATH];
        wchar_t darkPath[MAX_PATH];
        GetModuleFileNameW(nullptr, lightPath, MAX_PATH);
        // strip exe name
        wchar_t* p = wcsrchr(lightPath, L'\\');
        if (p) *(p+1) = L'\0';
        wcscat_s(lightPath, MAX_PATH, L"resources\\board_light.png");
        wcscpy_s(darkPath, MAX_PATH, lightPath);
        // replace light with dark
        wchar_t* found = wcsstr(darkPath, L"board_light.png");
        if (found)
            wcscpy_s(found, MAX_PATH - (found - darkPath), L"board_dark.png");

        if (g_pRenderTarget && g_pWICFactory)
        {
            if (GetFileAttributesW(lightPath) != INVALID_FILE_ATTRIBUTES)
            {
                LoadBitmapFromFile(g_pRenderTarget.Get(), g_pWICFactory.Get(), lightPath, &g_pLightBitmap);
            }
            if (GetFileAttributesW(darkPath) != INVALID_FILE_ATTRIBUTES)
            {
                LoadBitmapFromFile(g_pRenderTarget.Get(), g_pWICFactory.Get(), darkPath, &g_pDarkBitmap);
            }
        }
    }

    // draw tiles
    const int N = 10;
    g_tileSize = (boardSize > 0) ? boardSize / N : 0;
    for (int r = 0; r < N; ++r)
    {
        for (int c = 0; c < N; ++c)
        {
            bool isLight = ((r + c) % 2 == 0);
            D2D1_RECT_F rct = D2D1::RectF(g_boardLeft + c * g_tileSize, g_boardTop + r * g_tileSize,
                                         g_boardLeft + (c + 1) * g_tileSize, g_boardTop + (r + 1) * g_tileSize);
            if (isLight && g_pLightBitmap)
            {
                g_pRenderTarget->DrawBitmap(g_pLightBitmap.Get(), rct, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            else if (!isLight && g_pDarkBitmap)
            {
                g_pRenderTarget->DrawBitmap(g_pDarkBitmap.Get(), rct, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            else
            {
                if (isLight) g_pRenderTarget->FillRectangle(rct, g_pLightBrush.Get());
                else g_pRenderTarget->FillRectangle(rct, g_pDarkBrush.Get());
            }
            g_pRenderTarget->DrawRectangle(rct, g_pLineBrush.Get(), 0.6f);
        }
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
        // letters A-J along bottom (centered in each tile)
        for (int c = 0; c < N; ++c)
        {
            wchar_t letter[3] = { (wchar_t)(L'A' + c), 0 };
            // center area within each tile
            D2D1_RECT_F tileRect = D2D1::RectF(g_boardLeft + c * g_tileSize, g_boardTop + boardSize + 2.0f,
                                               g_boardLeft + (c + 1) * g_tileSize, g_boardTop + boardSize + (g_tileSize * 0.5f));
            g_pRenderTarget->DrawTextW(letter, 1, g_pTextFormatCenter.Get(), tileRect, g_pLineBrush.Get());
        }
        // numbers 1-10 on left, center vertically in each tile and right-aligned within left margin
        for (int r = 0; r < N; ++r)
        {
            wchar_t num[8]; swprintf_s(num, L"%d", r + 1);
            D2D1_RECT_F numRect = D2D1::RectF(g_boardLeft - (g_tileSize * 0.6f), g_boardTop + r * g_tileSize,
                                              g_boardLeft - 4.0f, g_boardTop + (r + 1) * g_tileSize);
            g_pRenderTarget->DrawTextW(num, (UINT32)wcslen(num), g_pTextFormatTrailing.Get(), numRect, g_pLineBrush.Get());
        }
    }

    // draw cursor last
    if (g_mouseInside)
    {
        Mouse_Draw(g_pRenderTarget.Get());
    }

    g_pRenderTarget->DrawRectangle(boardRect, g_pLineBrush.Get(), 2.0f);

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
    if (col < 0 || col >= 10 || row < 0 || row >= 10) return false;
    outRow = row; outCol = col; return true;
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
    Mouse_OnMove((float)x, (float)y);
}

void Board_OnMouseLeave()
{
    g_mouseInside = false;
    g_hoverRow = -1; g_hoverCol = -1;
    Mouse_OnLeave();
}

void Board_OnLButtonDown(int x, int y)
{
    int row, col;
    if (ScreenToBoard(x, y, row, col))
    {
        // simple visual feedback: toggle highlight cell to dark brush temporarily (could be expanded)
        g_hoverRow = row; g_hoverCol = col;
        Mouse_SetParsing(true);
    }
}

void Board_Cleanup()
{
    DiscardDeviceResources();
    g_pDWriteFactory.Reset();
    g_pD2DFactory.Reset();
    Mouse_Cleanup();
}
