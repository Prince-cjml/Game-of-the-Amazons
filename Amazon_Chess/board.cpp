#include "board.h"
#include "game.h"
#include "save_load.h"
// #include "Mouse.h"  // custom mouse removed; use system cursor
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <utility>
#include <commdlg.h>
#include <shellapi.h>
#include <sstream>
#include <mmsystem.h>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "windowscodecs")
#pragma comment(lib, "comdlg32")
#pragma comment(lib, "shell32")
#pragma comment(lib, "winmm")

using Microsoft::WRL::ComPtr;

// Forward declarations for functions defined later but used earlier in this file
static void PlayWavByName(const wchar_t* filename);
static void OnGameHistoryChanged();
static void RedrawMainWindow();

static ComPtr<ID2D1Factory>            g_pD2DFactory;
static ComPtr<ID2D1HwndRenderTarget>  g_pRenderTarget;
static ComPtr<ID2D1SolidColorBrush>   g_pLightBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pDarkBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pLineBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pHoverBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pBeadBrush; // decorative beads
static ComPtr<ID2D1SolidColorBrush>   g_pYellowBrush; // legal move highlight
static ComPtr<ID2D1SolidColorBrush>   g_pRedBrush;    // arrow highlight
static ComPtr<ID2D1SolidColorBrush>   g_pArrowBrush;  // blue arrow fill
static ComPtr<ID2D1SolidColorBrush>   g_pDisabledBrush; // brush for disabled buttons
// piece brushes for UI
static ComPtr<ID2D1SolidColorBrush>   g_pWhitePieceBrush;
static ComPtr<ID2D1SolidColorBrush>   g_pBlackPieceBrush;
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
static D2D1_RECT_F g_btnHistoryRect = D2D1::RectF();
static D2D1_RECT_F g_btnUndoRect = D2D1::RectF(); // new undo button below history
static D2D1_RECT_F g_btnNextRect = D2D1::RectF(); // symmetric next button below undo

// history window globals (moved up so other code can reference them)
static HWND g_hHistoryWnd = nullptr;
static HWND g_hHistoryEdit = nullptr;
static ATOM g_historyClassAtom = 0;
static HFONT g_hHistoryFont = nullptr; // font used in history edit
static const int ID_HISTORY_LIST = 6001;

static ModeChangeCallback g_modeCb = nullptr;

void Board_SetModeChangeCallback(ModeChangeCallback cb) { g_modeCb = cb; }

// forward declaration for history window helper
static void CreateOrUpdateHistoryWindow();
static void UpdateHistoryWindowContents();

// Helper: find main application window by class name and request redraw
static HWND FindMainWindow()
{
    return FindWindowW(L"AmazonChessWindowClass", nullptr);
}
static void RedrawMainWindow()
{
    HWND h = FindMainWindow();
    if (h && IsWindow(h))
    {
        // Invalidate and update without erasing background to avoid white flash
        InvalidateRect(h, nullptr, FALSE);
        UpdateWindow(h);
    }
}

// Game configuration (make available before rendering code)
static int g_boardN = 10; // current board size (6/8/10)
static bool g_opponentIsAI = true;
static int  g_aiDifficulty = 1;

// selection state for human move flow
enum SelectState { SELECT_IDLE=0, SELECT_MOVE, SELECT_ARROW };
static SelectState g_selectState = SELECT_IDLE;
static int g_selFromR=-1, g_selFromC=-1;
static int g_selToR=-1, g_selToC=-1;
static std::vector<std::pair<int,int>> g_legalMoves;
static std::vector<std::pair<int,int>> g_legalArrows;

void Board_StartNewGame(int boardSize, bool opponentIsAI, int aiDifficulty)
{
    if (boardSize != 6 && boardSize != 8 && boardSize != 10) boardSize = 8;
    g_boardN = boardSize;
    g_opponentIsAI = opponentIsAI;
    g_aiDifficulty = (aiDifficulty >= 0) ? aiDifficulty : 1;
    g_hoverRow = -1; g_hoverCol = -1;
    g_widgetVisible = false;
    g_draggingSlider = false;
    // reset selection
    g_selectState = SELECT_IDLE;
    g_selFromR = g_selFromC = g_selToR = g_selToC = -1;
    g_legalMoves.clear(); g_legalArrows.clear();
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

    // register history-changed callback so UI can be notified when moves occur
    Game_SetHistoryChangedCallback(OnGameHistoryChanged);

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
    g_pYellowBrush.Reset(); g_pRedBrush.Reset(); g_pArrowBrush.Reset();
    g_pWhitePieceBrush.Reset(); g_pBlackPieceBrush.Reset();
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
    // highlights and arrow brush
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.92f, 0.3f, 0.45f), &g_pYellowBrush); // yellowish
    if (FAILED(hr)) return hr;
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.25f, 0.25f, 0.45f), &g_pRedBrush); // red
    if (FAILED(hr)) return hr;
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.45f, 0.94f), &g_pArrowBrush); // blue
    if (FAILED(hr)) return hr;

    // small brushes for piece UI
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &g_pWhitePieceBrush);
    if (FAILED(hr)) return hr;
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.08f), &g_pBlackPieceBrush);
    if (FAILED(hr)) return hr;
    // disabled button brush (greyed)
    hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.6f, 0.6f, 0.6f, 0.6f), &g_pDisabledBrush);
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

// simple helper to check if a coordinate pair exists in a list
static bool PointInList(const std::vector<std::pair<int,int>>& list, int r, int c)
{
    for (const auto &p : list) if (p.first==r && p.second==c) return true;
    return false;
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

    // highlight legal moves (yellow) and arrows (red)
    if (g_selectState == SELECT_MOVE)
    {
        for (auto &p : g_legalMoves)
        {
            D2D1_RECT_F rct = D2D1::RectF(g_boardLeft + p.second * g_tileSize, g_boardTop + p.first * g_tileSize,
                                         g_boardLeft + (p.second+1) * g_tileSize, g_boardTop + (p.first+1) * g_tileSize);
            if (g_pYellowBrush) g_pRenderTarget->FillRectangle(rct, g_pYellowBrush.Get());
        }
    }
    else if (g_selectState == SELECT_ARROW)
    {
        for (auto &p : g_legalArrows)
        {
            D2D1_RECT_F rct = D2D1::RectF(g_boardLeft + p.second * g_tileSize, g_boardTop + p.first * g_tileSize,
                                         g_boardLeft + (p.second+1) * g_tileSize, g_boardTop + (p.first+1) * g_tileSize);
            if (g_pRedBrush) g_pRenderTarget->FillRectangle(rct, g_pRedBrush.Get());
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
        // history button below
        float hy = top + btnSize + 8.0f;
        g_btnHistoryRect = D2D1::RectF(left, hy, left + btnSize, hy + btnSize);
        // undo button below history
        float uy = hy + btnSize + 6.0f;
        g_btnUndoRect = D2D1::RectF(left, uy, left + btnSize, uy + btnSize);
        // next button below undo
        float ny = uy + btnSize + 6.0f;
        g_btnNextRect = D2D1::RectF(left, ny, left + btnSize, ny + btnSize);
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

    // draw history button
    if (g_pHoverBrush && g_pLineBrush)
    {
        float corner = 6.0f;
        g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(g_btnHistoryRect, corner, corner), g_pHoverBrush.Get());
        g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(g_btnHistoryRect, corner, corner), g_pLineBrush.Get(), 1.0f);
        if (g_pTextFormatCenter)
        {
            g_pRenderTarget->DrawTextW(L"Hist", 4, g_pTextFormatCenter.Get(), g_btnHistoryRect, g_pLineBrush.Get());
        }
    }

    // draw undo button (arrow) below history
    bool canUndo = (Game_GetCurrentMoveIndex() > 0);
    bool canRedo = Game_CanStepForward();
    if (g_pHoverBrush && g_pLineBrush)
    {
        float corner = 6.0f;
        // choose brush based on enabled state
        ID2D1Brush* fillBrush = (canUndo ? g_pHoverBrush.Get() : g_pDisabledBrush.Get());
        g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(g_btnUndoRect, corner, corner), fillBrush);
        g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(g_btnUndoRect, corner, corner), g_pLineBrush.Get(), 1.0f);
        // draw simple undo arrow: left-pointing triangle plus tail
        float cx = (g_btnUndoRect.left + g_btnUndoRect.right) * 0.5f;
        float cy = (g_btnUndoRect.top + g_btnUndoRect.bottom) * 0.5f;
        float s = (g_btnUndoRect.right - g_btnUndoRect.left) * 0.18f;
        // arrowhead
        D2D1_POINT_2F a = D2D1::Point2F(cx - s, cy);
        D2D1_POINT_2F b = D2D1::Point2F(cx + s, cy - s);
        D2D1_POINT_2F cpt = D2D1::Point2F(cx + s, cy + s);
        // use line brush for arrowhead but adjust opacity if disabled
        ComPtr<ID2D1SolidColorBrush> arrowBrush;
        if (canUndo)
            arrowBrush = g_pLineBrush;
        else
            g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.35f,0.35f,0.35f,0.9f), &arrowBrush);
        FillTriangle(g_pRenderTarget.Get(), a, b, cpt, arrowBrush.Get());
        // tail: short curved-ish line approximated by two lines
        g_pRenderTarget->DrawLine(D2D1::Point2F(cx + s, cy), D2D1::Point2F(cx + s + (s*0.9f), cy - (s*0.6f)), arrowBrush.Get(), 1.4f);
        g_pRenderTarget->DrawLine(D2D1::Point2F(cx + s, cy), D2D1::Point2F(cx + s + (s*0.9f), cy + (s*0.6f)), arrowBrush.Get(), 1.4f);
    }

    // draw next button (symmetric) below undo
    if (g_pHoverBrush && g_pLineBrush)
    {
        float corner = 6.0f;
        ID2D1Brush* fillBrushNext = (canRedo ? g_pHoverBrush.Get() : g_pDisabledBrush.Get());
        g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(g_btnNextRect, corner, corner), fillBrushNext);
        g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(g_btnNextRect, corner, corner), g_pLineBrush.Get(), 1.0f);
        // draw simple next arrow: right-pointing triangle plus tail
        float cx = (g_btnNextRect.left + g_btnNextRect.right) * 0.5f;
        float cy = (g_btnNextRect.top + g_btnNextRect.bottom) * 0.5f;
        float s = (g_btnNextRect.right - g_btnNextRect.left) * 0.18f;
        // arrowhead (right-pointing)
        D2D1_POINT_2F a2 = D2D1::Point2F(cx + s, cy);
        D2D1_POINT_2F b2 = D2D1::Point2F(cx - s, cy - s);
        D2D1_POINT_2F cpt2 = D2D1::Point2F(cx - s, cy + s);
        ComPtr<ID2D1SolidColorBrush> arrowBrush2;
        if (canRedo)
            arrowBrush2 = g_pLineBrush;
        else
            g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.35f,0.35f,0.35f,0.9f), &arrowBrush2);
        FillTriangle(g_pRenderTarget.Get(), a2, b2, cpt2, arrowBrush2.Get());
        // tail
        g_pRenderTarget->DrawLine(D2D1::Point2F(cx - s, cy), D2D1::Point2F(cx - s - (s*0.9f), cy - (s*0.6f)), arrowBrush2.Get(), 1.4f);
        g_pRenderTarget->DrawLine(D2D1::Point2F(cx - s, cy), D2D1::Point2F(cx - s - (s*0.9f), cy + (s*0.6f)), arrowBrush2.Get(), 1.4f);
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
        // shift panel slightly left from the menu button so the bottom-right indicator doesn't overlap
        float left = g_menuButtonRectWindow.right - w - 8.0f; // small offset to avoid overlap
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

        // make button height scale with panel font size so text fits
        float panelFontSize = max(12.0f, (g_widgetRect.bottom - g_widgetRect.top) * 0.18f);
        float btnH = max(36.0f, panelFontSize * 2.0f);
        const float btnPad = 8.0f;
        const float lineW = 2.0f;

        // prepare a button text format so labels fit comfortably
        ComPtr<IDWriteTextFormat> tfBtn;
        if (g_pDWriteFactory)
        {
            float btnFont = max(12.0f, panelFontSize * 0.9f);
            if (SUCCEEDED(g_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, btnFont, L"en-us", &tfBtn)))
            {
                tfBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                tfBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }

        // layout controls inside widget
        float cx = g_widgetRect.left + btnPad;
        float cy = g_widgetRect.top + btnPad;
        float innerW = (g_widgetRect.right - g_widgetRect.left) - (btnPad * 2.0f);
        // allocate larger portions for the buttons so their text fits
        float leftBtnW = innerW * 0.40f;
        float rightBtnW = innerW * 0.40f;
        // menu button (left)
        g_btnMenuRect = D2D1::RectF(g_widgetRect.left + btnPad, g_widgetRect.top + btnPad, g_widgetRect.left + btnPad + leftBtnW, g_widgetRect.top + btnPad + btnH);
        // save button (right)
        g_btnSaveRect = D2D1::RectF(g_widgetRect.left + btnPad + leftBtnW + (innerW * 0.05f), g_widgetRect.top + btnPad, g_widgetRect.left + btnPad + leftBtnW + (innerW * 0.05f) + rightBtnW, g_widgetRect.top + btnPad + btnH);
        // slider area below
        g_sliderRect = D2D1::RectF(g_widgetRect.left + btnPad, g_widgetRect.top + btnPad + btnH + 12.0f, g_widgetRect.right - btnPad, g_widgetRect.top + btnPad + btnH + 28.0f);

        // draw menu (rounded)
        float corner = 12.0f; // smoother corners
        g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(g_btnMenuRect, corner, corner), g_pHoverBrush.Get());
        g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(g_btnMenuRect, corner, corner), g_pLineBrush.Get(), 1.0f);
        if (tfBtn)
            g_pRenderTarget->DrawTextW(L"Menu", 4, tfBtn.Get(), g_btnMenuRect, g_pLineBrush.Get());
        else if (g_pTextFormatCenter)
            g_pRenderTarget->DrawTextW(L"Menu", 4, g_pTextFormatCenter.Get(), g_btnMenuRect, g_pLineBrush.Get());

        // draw save (rounded)
        g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(g_btnSaveRect, corner, corner), g_pHoverBrush.Get());
        g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(g_btnSaveRect, corner, corner), g_pLineBrush.Get(), 1.0f);
        if (tfBtn)
            g_pRenderTarget->DrawTextW(L"Save", 4, tfBtn.Get(), g_btnSaveRect, g_pLineBrush.Get());
        else if (g_pTextFormatCenter)
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

    // when a piece is selected, exclude it from the regular draw so we can render a translucent ghost separately
    int excludeR = -1, excludeC = -1;
    if (g_selectState == SELECT_MOVE || g_selectState == SELECT_ARROW)
    {
        excludeR = g_selFromR;
        excludeC = g_selFromC;
    }
    Game_DrawPieces(g_pRenderTarget.Get(), whitePieceBrush.Get(), blackPieceBrush.Get(), g_boardLeft, g_boardTop, g_tileSize, excludeR, excludeC);

    // create ghost brushes (50% opacity)
    ComPtr<ID2D1SolidColorBrush> ghostWhite;
    ComPtr<ID2D1SolidColorBrush> ghostBlack;
    g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 0.5f), &ghostWhite);
    g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.08f, 0.5f), &ghostBlack);

    // draw translucent ghosts depending on selection state
    auto drawGhostAt = [&](int rr, int cc, bool isWhite)
    {
        if (rr < 0 || cc < 0) return;
        float cx = g_boardLeft + (cc + 0.5f) * g_tileSize;
        float cy = g_boardTop + (rr + 0.5f) * g_tileSize;
        float radius = max(6.0f, g_tileSize * 0.28f);
        D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
        if (isWhite)
        {
            if (ghostWhite) g_pRenderTarget->FillEllipse(e, ghostWhite.Get());
            if (g_pLineBrush) g_pRenderTarget->DrawEllipse(e, g_pLineBrush.Get(), 1.0f);
        }
        else
        {
            if (ghostBlack) g_pRenderTarget->FillEllipse(e, ghostBlack.Get());
            if (g_pLineBrush) g_pRenderTarget->DrawEllipse(e, g_pLineBrush.Get(), 1.0f);
        }
    };

    if (g_selectState == SELECT_MOVE && g_selFromR >= 0)
    {
        // original piece translucent at source
        const GamePiece* orig = Game_GetPieceAt(g_selFromR, g_selFromC);
        if (orig) drawGhostAt(g_selFromR, g_selFromC, orig->isWhite);
        // preview translucent piece at hovered legal move
        if (g_hoverRow >= 0 && g_hoverCol >= 0 && PointInList(g_legalMoves, g_hoverRow, g_hoverCol))
        {
            drawGhostAt(g_hoverRow, g_hoverCol, orig ? orig->isWhite : true);
        }
    }
    else if (g_selectState == SELECT_ARROW && g_selToR >= 0)
    {
        // show fixed translucent piece at destination
        const GamePiece* orig = Game_GetPieceAt(g_selFromR, g_selFromC);
        // determine color (orig may still be retrievable)
        bool isWhite = true;
        if (orig) isWhite = orig->isWhite;
        drawGhostAt(g_selToR, g_selToC, isWhite);
        // preview translucent arrow placement at hover if valid
        if (g_hoverRow >= 0 && g_hoverCol >= 0 && PointInList(g_legalArrows, g_hoverRow, g_hoverCol))
        {
            // draw translucent blue triangle to match final arrow sprite
            float cx = g_boardLeft + (g_hoverCol + 0.5f) * g_tileSize;
            float cy = g_boardTop + (g_hoverRow + 0.5f) * g_tileSize;
            float s = g_tileSize * 0.35f;
            D2D1_POINT_2F a = D2D1::Point2F(cx, cy - s);
            D2D1_POINT_2F b = D2D1::Point2F(cx - s, cy + s);
            D2D1_POINT_2F cpt = D2D1::Point2F(cx + s, cy + s);
            ComPtr<ID2D1SolidColorBrush> triBrush;
            if (SUCCEEDED(g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.45f, 0.94f, 0.45f), &triBrush)))
            {
                FillTriangle(g_pRenderTarget.Get(), a, b, cpt, triBrush.Get());
                if (g_pLineBrush) g_pRenderTarget->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), g_pLineBrush.Get(), 1.0f);
                if (g_pLineBrush) g_pRenderTarget->DrawLine(D2D1::Point2F(b.x, b.y), D2D1::Point2F(cpt.x, cpt.y), g_pLineBrush.Get(), 1.0f);
                if (g_pLineBrush) g_pRenderTarget->DrawLine(D2D1::Point2F(cpt.x, cpt.y), D2D1::Point2F(a.x, a.y), g_pLineBrush.Get(), 1.0f);
            }
        }
    }

    // draw arrows (blue triangles) for grid cells occupied by arrows
    for (int r = 0; r < N; ++r)
    {
        for (int c = 0; c < N; ++c)
        {
            if (Game_IsOccupied(r,c) && !Game_GetPieceAt(r,c))
            {
                // occupied but not a piece -> arrow (Game uses grid value 2)
                // draw equilateral triangle pointing up centered in cell
                float cx = g_boardLeft + (c + 0.5f) * g_tileSize;
                float cy = g_boardTop + (r + 0.5f) * g_tileSize;
                float s = g_tileSize * 0.35f;
                D2D1_POINT_2F a = D2D1::Point2F(cx, cy - s);
                D2D1_POINT_2F b = D2D1::Point2F(cx - s, cy + s);
                D2D1_POINT_2F cpt = D2D1::Point2F(cx + s, cy + s);
                FillTriangle(g_pRenderTarget.Get(), a, b, cpt, g_pArrowBrush.Get());
            }
        }
    }

    // draw side-to-move and AI side to the left of board
    {
        // compute panel width dynamically based on window size and tile size, clamp min/max
        float panelW = rtSize.width * 0.14f;
        float panelMinW = max(140.0f, g_tileSize * 3.0f);
        float panelMaxW = rtSize.width * 0.28f;
        if (panelW < panelMinW) panelW = panelMinW;
        if (panelW > panelMaxW) panelW = panelMaxW;
        float panelH = max(80.0f, g_tileSize * 3.0f);

        // Determine safe left position so panel does not overlap the coordinate gutter on the left.
        // The coordinate numbers are drawn left of 'outer.left' by roughly (g_tileSize * 0.6f).
        float gutterLeft = outer.left - (g_tileSize * 0.6f) - 8.0f; // extra padding
        float panelRightPreferred = gutterLeft; // ensure panel's right edge is left of the coords
        float panelLeft = panelRightPreferred - panelW - 12.0f; // small spacing between panel and gutter

        // If not enough room on the left, try placing panel to the right of the board instead.
        if (panelLeft < 8.0f)
        {
            panelLeft = boardRect.right + 18.0f;
            // ensure panel stays inside the window; if not, shrink it
            if (panelLeft + panelW > rtSize.width - 8.0f)
            {
                panelW = rtSize.width - panelLeft - 8.0f;
                if (panelW < panelMinW) panelW = panelMinW; // keep a sensible minimum
            }
        }

        float panelTop = g_boardTop;
        D2D1_RECT_F panel = D2D1::RectF(panelLeft, panelTop, panelLeft + panelW, panelTop + panelH);
        if (g_pDarkBrush) g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(panel, 8, 8), g_pDarkBrush.Get());
        if (g_pLineBrush) g_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(panel, 8, 8), g_pLineBrush.Get(), 1.4f);

        // create text formats for the panel: label and value
        ComPtr<IDWriteTextFormat> tfLabel;
        ComPtr<IDWriteTextFormat> tfValue;
        float panelFontSize = max(12.0f, panelH * 0.18f);
        if (g_pDWriteFactory)
        {
            // label (smaller)
            g_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, panelFontSize, L"en-us", &tfLabel);
            if (tfLabel)
            {
                tfLabel->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                tfLabel->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                tfLabel->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
            // value (slightly larger for emphasis)
            float valFontSize = max(panelFontSize * 1.2f, panelFontSize + 2.0f);
            g_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, valFontSize, L"en-us", &tfValue);
            if (tfValue)
            {
                tfValue->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                tfValue->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                tfValue->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
        }

        // To move: original position near the top-left inside the panel
        std::wstring mover = Game_IsBlackToMove() ? L"Black" : L"White";
        std::wstring labelText = L"To move:";

        float pad = 12.0f;
        float labelTop = panelTop + 10.0f;
        float labelH = panelFontSize * 1.2f;
        D2D1_RECT_F labelRect = D2D1::RectF(panelLeft + pad, labelTop, panelLeft + panelW - pad, labelTop + labelH);
        if (tfLabel)
            g_pRenderTarget->DrawTextW(labelText.c_str(), (UINT32)labelText.size(), tfLabel.Get(), labelRect, g_pLineBrush.Get());
        else if (g_pTextFormatCenter)
            g_pRenderTarget->DrawTextW(labelText.c_str(), (UINT32)labelText.size(), g_pTextFormatCenter.Get(), labelRect, g_pLineBrush.Get());

        // mover value on second line directly under the label
        float valTop = labelRect.bottom + 4.0f;
        float valH = (tfValue) ? (panelFontSize * 1.2f * 1.2f) : labelH;
        D2D1_RECT_F valRect = D2D1::RectF(panelLeft + pad, valTop, panelLeft + panelW - pad, valTop + valH);
        if (tfValue)
            g_pRenderTarget->DrawTextW(mover.c_str(), (UINT32)mover.size(), tfValue.Get(), valRect, g_pLineBrush.Get());
        else if (g_pTextFormatCenter)
            g_pRenderTarget->DrawTextW(mover.c_str(), (UINT32)mover.size(), g_pTextFormatCenter.Get(), valRect, g_pLineBrush.Get());

        // draw a circle indicating side color, KEEP at bottom-right inside the panel with padding (user likes this)
        float circR = max(10.0f, g_tileSize * 0.22f);
        float bottomPad = pad;
        float bottomTextH = (tfValue) ? valH : labelH;
        float bottomTextTop = panelTop + panelH - bottomPad - bottomTextH; // used for vertical placement reference
        float circX = panelLeft + panelW - bottomPad - circR;
        float circY = bottomTextTop + bottomTextH * 0.5f;
        D2D1_ELLIPSE circ = D2D1::Ellipse(D2D1::Point2F(circX, circY), circR, circR);
        if (Game_IsBlackToMove())
        {
            if (g_pBlackPieceBrush) g_pRenderTarget->FillEllipse(circ, g_pBlackPieceBrush.Get());
            if (g_pLineBrush) g_pRenderTarget->DrawEllipse(circ, g_pLineBrush.Get(), 1.0f);
        }
        else
        {
            if (g_pWhitePieceBrush) g_pRenderTarget->FillEllipse(circ, g_pWhitePieceBrush.Get());
            if (g_pLineBrush) g_pRenderTarget->DrawEllipse(circ, g_pLineBrush.Get(), 1.0f);
        }

        // AI side only when opponent is AI: place it below the mover value if space allows
        if (Game_IsOpponentAI())
        {
            std::wstring aiText = L"AI: ";
            aiText += (Game_IsAIBlack() ? L"Black" : L"White");
            float aiTop = valRect.bottom + 8.0f;
            float aiH = panelFontSize * 1.0f;
            D2D1_RECT_F trect2 = D2D1::RectF(panelLeft + pad, aiTop, panelLeft + panelW - pad, aiTop + aiH);
            if (tfLabel)
                g_pRenderTarget->DrawTextW(aiText.c_str(), (UINT32)aiText.size(), tfLabel.Get(), trect2, g_pLineBrush.Get());
            else if (g_pTextFormatCenter)
                g_pRenderTarget->DrawTextW(aiText.c_str(), (UINT32)aiText.size(), g_pTextFormatCenter.Get(), trect2, g_pLineBrush.Get());
        }
    }

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

    // change cursor to hand when hovering over interactive UI buttons, otherwise arrow
    bool overButton = false;
    bool overHistory = PtInRectF(g_btnHistoryRect, x, y);
    bool overUndo = PtInRectF(g_btnUndoRect, x, y);
    bool overNext = PtInRectF(g_btnNextRect, x, y);
    if (PtInRectF(g_menuButtonRectWindow, x, y) || overHistory) overButton = true;

    // if over undo/next and disabled, show forbidden cursor
    if (overUndo && ! (Game_GetCurrentMoveIndex() > 0))
    {
        SetCursor(LoadCursor(NULL, IDC_NO));
        return;
    }
    if (overNext && !Game_CanStepForward())
    {
        SetCursor(LoadCursor(NULL, IDC_NO));
        return;
    }

    if (overButton || overUndo || overNext)
    {
        SetCursor(LoadCursor(NULL, IDC_HAND));
    }
    else
    {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    }
}

void CancelSelection()
{
    g_selectState = SELECT_IDLE;
    g_selFromR = g_selFromC = g_selToR = g_selToC = -1;
    g_legalMoves.clear(); g_legalArrows.clear();
}

void Board_OnLButtonDown(int x, int y)
{
    // check menu button first
    if (PtInRectF(g_menuButtonRectWindow, x, y))
    {
        g_widgetVisible = !g_widgetVisible;
        return;
    }

    // history button
    if (PtInRectF(g_btnHistoryRect, x, y))
    {
        CreateOrUpdateHistoryWindow();
        return;
    }

    // undo button
    if (PtInRectF(g_btnUndoRect, x, y))
    {
        Game_RewindOneStep();
        // update history window if open
        UpdateHistoryWindowContents();
        // force immediate redraw so board updates right away
        RedrawMainWindow();
        return;
    }

    // next button
    if (PtInRectF(g_btnNextRect, x, y))
    {
        if (Game_CanStepForward()) Game_StepForward();
        UpdateHistoryWindowContents();
        RedrawMainWindow();
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
            PlayWavByName(L"click.wav");
            return;
        }
        if (PtInRectF(g_btnSaveRect, x, y))
        {
            PlayWavByName(L"click.wav");
            // show Save File dialog
            WCHAR szFile[MAX_PATH] = {};
            // default file name
            wcscpy_s(szFile, L"NewGame.pbn");
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = FindMainWindow();
            ofn.lpstrFilter = L"Amazons Chess Game Record (*.pbn)\0*.pbn\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            ofn.lpstrDefExt = L"pbn";
            if (GetSaveFileNameW(&ofn))
            {
                bool ok = SaveHistoryToFile(szFile);
                if (!ok)
                {
                    MessageBoxW(ofn.hwndOwner, L"Failed to save file.", L"Save Error", MB_OK | MB_ICONERROR);
                }
                else
                {
                    MessageBoxW(ofn.hwndOwner, L"Saved.", L"Save", MB_OK | MB_ICONINFORMATION);
                }
            }
            return;
        }
        // slider click: if inside slider track, start dragging and set value
        if (PtInRectF(g_sliderTrackRect, x, y))
        {
            PlayWavByName(L"click.wav");
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
    bool inside = ScreenToBoard(x, y, row, col);
    if (!inside)
    {
        // clicking outside board cancels selection per requirements
        CancelSelection();
        return;
    }

    // selection state machine for human play
    if (g_selectState == SELECT_IDLE)
    {
        const GamePiece* p = Game_GetPieceAt(row,col);
        if (!p) return; // clicked empty
        bool blackToMove = Game_IsBlackToMove();
        if ((p->isWhite && blackToMove) || (!p->isWhite && !blackToMove))
        {
            // not current player's piece
            return;
        }
        // start selection
        g_selFromR = row; g_selFromC = col;
        g_legalMoves = Game_GetLegalMoves(row,col);
        if (!g_legalMoves.empty()) { g_selectState = SELECT_MOVE; /* click sound only on actual move */ } else CancelSelection();
        return;
    }
    else if (g_selectState == SELECT_MOVE)
    {
        // check if clicked a legal destination
        bool found = false;
        for (auto &p : g_legalMoves) if (p.first==row && p.second==col) { found=true; break; }
        if (!found)
        {
            // invalid -> cancel
            CancelSelection();
            return;
        }
        // valid destination
        g_selToR = row; g_selToC = col;
        g_legalArrows = Game_GetLegalArrows(g_selFromR, g_selFromC, g_selToR, g_selToC);
        if (!g_legalArrows.empty()) { g_selectState = SELECT_ARROW; /* click sound only on actual move */ } else CancelSelection();
        return;
    }
    else if (g_selectState == SELECT_ARROW)
    {
        // check arrow validity
        bool found = false;
        for (auto &p : g_legalArrows) if (p.first==row && p.second==col) { found=true; break; }
        if (!found)
        {
            CancelSelection();
            return;
        }
        // make move
        Game_MakeMove(g_selFromR, g_selFromC, g_selToR, g_selToC, row, col);
        // play click sound only when a move has been made
        PlayWavByName(L"click.wav");

        // after move, check for end-game
        int winner = Game_CheckForWinner(); // 0 none, 1 white wins, 2 black wins
        if (winner != 0)
        {
            // display winner
            std::wstring msg = (winner == 1) ? L"White wins!" : L"Black wins!";
            // choose sound: if human vs human -> chimes; if vs AI -> chimes if human wins else explode
            if (!Game_IsOpponentAI())
            {
                PlayWavByName(L"chimes.wav");
            }
            else
            {
                // determine human color: if AI is black, human is white
                bool aiIsBlack = Game_IsAIBlack();
                bool humanWon = (winner == 1 && !aiIsBlack) || (winner == 2 && aiIsBlack) ? false : false; // placeholder
                // compute correctly: humanIsWhite = !aiIsBlack
                bool humanIsWhite = !aiIsBlack;
                if ((winner == 1 && humanIsWhite) || (winner == 2 && !humanIsWhite))
                {
                    // human won
                    PlayWavByName(L"chimes.wav");
                }
                else
                {
                    // AI won
                    PlayWavByName(L"explode.wav");
                }
            }
            MessageBoxW(nullptr, msg.c_str(), L"Game Over", MB_OK | MB_ICONINFORMATION);
        }

        // update history window if open
        UpdateHistoryWindowContents();

        CancelSelection();
        return;
    }

    // no default click here
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

void Board_OnMouseLeave()
{
    // restore hover state when mouse leaves board area
    g_mouseInside = false;
    g_hoverRow = -1;
    g_hoverCol = -1;
    // restore cursor
    SetCursor(LoadCursor(NULL, IDC_ARROW));
}

// forward declarations for history window helper (defined later)
static void CreateOrUpdateHistoryWindow();

LRESULT CALLBACK HistoryWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == ID_HISTORY_LIST && code == LBN_SELCHANGE)
        {
            // user selected an item -> rewind to that move
            int idx = (int)SendMessageW(g_hHistoryEdit, LB_GETCURSEL, 0, 0);
            if (idx >= 0)
            {
                // list contains "Game Start" at index 0 which corresponds to 0 applied moves
                // so keepMoves = idx
                Game_RewindToMoveCount(idx);
                // refresh list contents (UpdateHistoryWindowContents will be called via callback too)
                UpdateHistoryWindowContents();
                // immediately update UI so the main board reflects the rewound state
                RedrawMainWindow();
            }
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        if (g_hHistoryEdit)
        {
            DestroyWindow(g_hHistoryEdit);
            g_hHistoryEdit = nullptr;
        }
        g_hHistoryWnd = nullptr;
        // Keep the window class registered so the history window can be recreated later.
        // Free the history font if we created one
        if (g_hHistoryFont)
        {
            DeleteObject(g_hHistoryFont);
            g_hHistoryFont = nullptr;
        }
        // unregister callback so we don't attempt to update a closed window
        Game_SetHistoryChangedCallback(nullptr);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

static void CreateOrUpdateHistoryWindow()
{
    const auto &hist = Game_GetHistory();
    if (g_hHistoryWnd && IsWindow(g_hHistoryWnd))
    {
        // refresh contents
        UpdateHistoryWindowContents();
        SetForegroundWindow(g_hHistoryWnd);
        return;
    }

    // register class
    if (!g_historyClassAtom)
    {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HistoryWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"AmazonChessHistoryWndClass";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        g_historyClassAtom = RegisterClassExW(&wc);
    }
    if (!g_historyClassAtom) return;

    g_hHistoryWnd = CreateWindowExW(0, MAKEINTATOM(g_historyClassAtom), L"Game History", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 600, FindMainWindow(), nullptr, GetModuleHandle(NULL), nullptr);
    if (!g_hHistoryWnd) return;

    // ensure window is shown without taking activation and kept above the main window
    ShowWindow(g_hHistoryWnd, SW_SHOWNOACTIVATE);
    SetWindowPos(g_hHistoryWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // create a listbox instead of edit so clicks are easy to detect
    g_hHistoryEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
        8, 8, 400, 520, g_hHistoryWnd, (HMENU)ID_HISTORY_LIST, GetModuleHandle(NULL), nullptr);
    if (!g_hHistoryEdit) return;

    // populate
    UpdateHistoryWindowContents();

    // set a larger monospace font for clarity
    if (g_hHistoryFont)
    {
        DeleteObject(g_hHistoryFont);
        g_hHistoryFont = nullptr;
    }
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    // create a 14pt Consolas font (scaled for DPI)
    g_hHistoryFont = CreateFontW(-MulDiv(14, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
    if (g_hHistoryFont)
        SendMessageW(g_hHistoryEdit, WM_SETFONT, (WPARAM)g_hHistoryFont, TRUE);

    // register history-changed callback so the window updates in real-time
    Game_SetHistoryChangedCallback(OnGameHistoryChanged);

    // clear hover so UI in main window doesn't think mouse is still over board
    g_mouseInside = false;
    g_hoverRow = -1;
    g_hoverCol = -1;
}

static void UpdateHistoryWindowContents()
{
    if (!g_hHistoryEdit || !IsWindow(g_hHistoryEdit)) return;
    SendMessageW(g_hHistoryEdit, LB_RESETCONTENT, 0, 0);
    // first item is Game Start (0 moves applied)
    SendMessageW(g_hHistoryEdit, LB_ADDSTRING, 0, (LPARAM)L"Game Start");
    const auto &hist = Game_GetHistory();
    for (size_t i = 0; i < hist.size(); ++i)
    {
        SendMessageW(g_hHistoryEdit, LB_ADDSTRING, 0, (LPARAM)hist[i].c_str());
    }
    // set selection to current move index (number of moves applied)
    int sel = Game_GetCurrentMoveIndex();
    if (sel < 0) sel = 0;
    if (sel > Game_GetTotalMoves()) sel = Game_GetTotalMoves();
    SendMessageW(g_hHistoryEdit, LB_SETCURSEL, (WPARAM)sel, 0);

    // keep history window topmost above main window without activating it
    if (g_hHistoryWnd && IsWindow(g_hHistoryWnd))
    {
        SetWindowPos(g_hHistoryWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// callback invoked by game logic when history changes
static void OnGameHistoryChanged()
{
    // If history window is open, refresh contents but do not change activation
    if (g_hHistoryWnd && IsWindow(g_hHistoryWnd))
    {
        UpdateHistoryWindowContents();
        // do not call SetForegroundWindow or otherwise steal focus
    }
    // request main window redraw so board reflects new/rewound state (targeted, non-activating)
    RedrawMainWindow();
}

static void PlayWavByName(const wchar_t* filename)
{
    if (!filename) return;
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
    {
        path[0] = 0;
    }
    else
    {
        for (int i = (int)len - 1; i >= 0; --i) { if (path[i] == L'\\' || path[i] == L'/') { path[i+1] = 0; break; } }
    }

    wchar_t fullPath[MAX_PATH] = {};
    wcscpy_s(fullPath, MAX_PATH, path);
    wcscat_s(fullPath, MAX_PATH, L"resources\\");
    wcscat_s(fullPath, MAX_PATH, filename);

    if (GetFileAttributesW(fullPath) != INVALID_FILE_ATTRIBUTES)
    {
        PlaySoundW(fullPath, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    }
    else if (GetFileAttributesW(filename) != INVALID_FILE_ATTRIBUTES)
    {
        PlaySoundW(filename, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    }
}
