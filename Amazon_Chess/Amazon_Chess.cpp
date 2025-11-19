// Amazon_Chess.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "Amazon_Chess.h"
#include <windowsx.h>
#include "board.h"
#include "menu.h"
#include "game.h"
#include "save_load.h"
#include <vector>
#include <commdlg.h>
#include <sstream>

#define MAX_LOADSTRING 100

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名

// Application mode: show menu first
enum AppMode { MODE_MENU = 0, MODE_GAME = 1 };
static AppMode g_appMode = MODE_MENU;

// callback to allow board to request returning to menu
static void ReturnToMenu()
{
    g_appMode = MODE_MENU;
    // tell menu to show Resume option
    Menu_SetHasResume(true);
}

// NewGame dialog result
struct NewGameOptions
{
    bool accepted;
    bool opponentIsAI; // true = AI (default), false = Human
    int boardSize;     // 8 or 10 (default 8)
    int difficulty;    // 0=Easy,1=Intermediate,2=Expert (shown but disabled)
    bool aiFirst;      // true if AI moves first (AI is black), default false (Player first)
};

// control IDs
enum {
    NG_ID_OK = 1001,
    NG_ID_CANCEL,
    NG_ID_OPP_AI,
    NG_ID_OPP_HUMAN,
    NG_ID_BS_8,
    NG_ID_BS_10,
    NG_ID_DIFF_EASY,
    NG_ID_DIFF_INTER,
    NG_ID_DIFF_EXPERT,
    NG_ID_AI_PLAYERFIRST,
    NG_ID_AI_AIFIRST
};

static LRESULT CALLBACK NewGameDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    NewGameOptions* popts = (NewGameOptions*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (msg == WM_CREATE)
    {
        // store pointer passed via CreateWindow lpParam
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        popts = (NewGameOptions*)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)popts);

        RECT rc; GetClientRect(hwnd, &rc);
        int dlgW = rc.right - rc.left;
        int dlgH = rc.bottom - rc.top;
        int left = 12, top = 12;

        // Opponent radio buttons
        CreateWindowW(L"STATIC", L"Opponent:", WS_CHILD | WS_VISIBLE, left, top, 80, 20, hwnd, nullptr, hInst, nullptr);
        CreateWindowW(L"BUTTON", L"AI", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP, left+90, top, 60, 20, hwnd, (HMENU)NG_ID_OPP_AI, hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Human", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, left+160, top, 80, 20, hwnd, (HMENU)NG_ID_OPP_HUMAN, hInst, nullptr);

        top += 30;
        // Board size radio buttons
        CreateWindowW(L"STATIC", L"Board size:", WS_CHILD | WS_VISIBLE, left, top, 80, 20, hwnd, nullptr, hInst, nullptr);
        CreateWindowW(L"BUTTON", L"8x8", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, left+90, top, 60, 20, hwnd, (HMENU)NG_ID_BS_8, hInst, nullptr);
        CreateWindowW(L"BUTTON", L"10x10", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, left+160, top, 80, 20, hwnd, (HMENU)NG_ID_BS_10, hInst, nullptr);

        top += 34;
        // Difficulty radio buttons (shown only when AI selected)
        CreateWindowW(L"STATIC", L"AI difficulty:", WS_CHILD | WS_VISIBLE, left, top, 100, 20, hwnd, nullptr, hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Easy", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, left+110, top, 80, 20, hwnd, (HMENU)NG_ID_DIFF_EASY, hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Intermediate", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP, left+200, top, 110, 20, hwnd, (HMENU)NG_ID_DIFF_INTER, hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Expert", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, left+110, top+24, 80, 20, hwnd, (HMENU)NG_ID_DIFF_EXPERT, hInst, nullptr);

        // AI Turn Order radio buttons
        CreateWindowW(L"STATIC", L"AI order:", WS_CHILD | WS_VISIBLE, left, top+54, 100, 20, hwnd, nullptr, hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Player first", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, left+110, top+54, 100, 20, hwnd, (HMENU)NG_ID_AI_PLAYERFIRST, hInst, nullptr);
        CreateWindowW(L"BUTTON", L"AI first", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, left+220, top+54, 80, 20, hwnd, (HMENU)NG_ID_AI_AIFIRST, hInst, nullptr);

        // OK / Cancel
        CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, dlgW-180, dlgH-40, 80, 28, hwnd, (HMENU)NG_ID_OK, hInst, nullptr);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, dlgW-90, dlgH-40, 80, 28, hwnd, (HMENU)NG_ID_CANCEL, hInst, nullptr);

        // set defaults: AI, 8x8, Intermediate, Player first
        CheckRadioButton(hwnd, NG_ID_OPP_AI, NG_ID_OPP_HUMAN, NG_ID_OPP_AI);
        CheckRadioButton(hwnd, NG_ID_BS_8, NG_ID_BS_10, NG_ID_BS_8);
        CheckRadioButton(hwnd, NG_ID_DIFF_EASY, NG_ID_DIFF_EXPERT, NG_ID_DIFF_INTER);
        CheckRadioButton(hwnd, NG_ID_AI_PLAYERFIRST, NG_ID_AI_AIFIRST, NG_ID_AI_PLAYERFIRST);
        // difficulty controls enabled because AI is default
        EnableWindow(GetDlgItem(hwnd, NG_ID_DIFF_EASY), TRUE);
        EnableWindow(GetDlgItem(hwnd, NG_ID_DIFF_INTER), TRUE);
        EnableWindow(GetDlgItem(hwnd, NG_ID_DIFF_EXPERT), TRUE);

        if (popts)
            *popts = { false, true, 8, 1, false };
        return 0;
    }

    if (msg == WM_COMMAND)
    {
        int id = LOWORD(wParam);
        // respond to opponent selection to enable/disable difficulty radios
        if (id == NG_ID_OPP_AI || id == NG_ID_OPP_HUMAN)
        {
            BOOL aiSelected = (IsDlgButtonChecked(hwnd, NG_ID_OPP_AI) == BST_CHECKED);
            EnableWindow(GetDlgItem(hwnd, NG_ID_DIFF_EASY), aiSelected);
            EnableWindow(GetDlgItem(hwnd, NG_ID_DIFF_INTER), aiSelected);
            EnableWindow(GetDlgItem(hwnd, NG_ID_DIFF_EXPERT), aiSelected);
            // enable/disable AI order radios as well
            EnableWindow(GetDlgItem(hwnd, NG_ID_AI_PLAYERFIRST), aiSelected);
            EnableWindow(GetDlgItem(hwnd, NG_ID_AI_AIFIRST), aiSelected);
            // if switched to AI and none of difficulty checked, default to Intermediate
            if (aiSelected && IsDlgButtonChecked(hwnd, NG_ID_DIFF_EASY) != BST_CHECKED && IsDlgButtonChecked(hwnd, NG_ID_DIFF_INTER) != BST_CHECKED && IsDlgButtonChecked(hwnd, NG_ID_DIFF_EXPERT) != BST_CHECKED)
            {
                CheckRadioButton(hwnd, NG_ID_DIFF_EASY, NG_ID_DIFF_EXPERT, NG_ID_DIFF_INTER);
            }
            return 0;
        }

        if (id == NG_ID_OK || id == NG_ID_CANCEL)
        {
            popts = (NewGameOptions*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            if (popts)
            {
                if (id == NG_ID_OK)
                {
                    popts->accepted = true;
                    popts->opponentIsAI = (IsDlgButtonChecked(hwnd, NG_ID_OPP_AI) == BST_CHECKED);
                    if (IsDlgButtonChecked(hwnd, NG_ID_BS_8) == BST_CHECKED) popts->boardSize = 8;
                    else popts->boardSize = 10;
                    if (popts->opponentIsAI)
                    {
                        if (IsDlgButtonChecked(hwnd, NG_ID_DIFF_EASY) == BST_CHECKED) popts->difficulty = 0;
                        else if (IsDlgButtonChecked(hwnd, NG_ID_DIFF_INTER) == BST_CHECKED) popts->difficulty = 1;
                        else popts->difficulty = 2;
                        // AI order
                        popts->aiFirst = (IsDlgButtonChecked(hwnd, NG_ID_AI_AIFIRST) == BST_CHECKED);
                    }
                    else
                    {
                        popts->difficulty = -1; // not applicable
                        popts->aiFirst = false;
                    }
                }
                else
                {
                    popts->accepted = false;
                }
            }
            DestroyWindow(hwnd);
            return 0;
        }
    }

    if (msg == WM_DESTROY)
    {
        // Do not post quit here; dialog destroy should just return control to caller
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static NewGameOptions ShowNewGameDialog(HWND parent)
{
    NewGameOptions opts = { false, true, 8, 1, false };

    WNDCLASS wc = {0};
    wc.lpfnWndProc = NewGameDlgProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"NewGameDlgClass";
    RegisterClass(&wc);

    int dlgW = 380, dlgH = 250;
    RECT pr; GetClientRect(parent, &pr);
    int px = pr.right/2 - dlgW/2;
    int py = pr.bottom/2 - dlgH/2;

    HWND hDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"New Game", WS_POPUPWINDOW | WS_CAPTION | WS_SYSMENU,
        px, py, dlgW, dlgH, parent, nullptr, hInst, &opts);
    if (!hDlg)
    {
        UnregisterClass(wc.lpszClassName, hInst);
        return opts;
    }

    ShowWindow(hDlg, SW_SHOW);

    // Modal loop: run until dialog destroyed
    MSG msg;
    while (IsWindow(hDlg) && GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterClass(wc.lpszClassName, hInst);
    return opts;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize COM for WIC
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 初始化全局字符串 - 使用固定标题和窗口类名 for clarity
    wcscpy_s(szTitle, MAX_LOADSTRING, L"Game of the Amazons: Conquer and Win!");
    wcscpy_s(szWindowClass, MAX_LOADSTRING, L"AmazonChessWindowClass");
    MyRegisterClass(hInstance);

    // Create device-independent board resources
    HRESULT hr = Board_CreateDeviceIndependentResources();
    if (FAILED(hr))
    {
        MessageBoxW(nullptr, L"Failed to initialize graphics libraries (Direct2D/DirectWrite/WIC). The application may not render correctly.", L"Initialization Error", MB_ICONERROR | MB_OK);
    }

    // Create menu device-independent resources (menu shown first)
    hr = Menu_CreateDeviceIndependentResources();
    if (FAILED(hr))
    {
        // non-fatal; menu will try to create resources when rendering
    }

    // 执行应用程序初始化:
    if (!InitInstance (hInstance, nCmdShow))
    {
        Board_Cleanup();
        Menu_Cleanup();
        CoUninitialize();
        return FALSE;
    }

    // register callback so board can return to menu
    Board_SetModeChangeCallback(ReturnToMenu);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_AMAZONCHESS));

    MSG msg;

    // 主消息循环:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    Board_Cleanup();
    Menu_Cleanup();
    CoUninitialize();
    return (int) msg.wParam;
}

//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_AMAZONCHESS));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_AMAZONCHESS);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 将实例句柄存储在全局变量中

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, SW_SHOWMAXIMIZED);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
//  WM_COMMAND  - 处理应用程序菜单
//  WM_PAINT    - 绘制主窗口
//  WM_DESTROY  - 发送退出消息并返回
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // 分析菜单选择:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_SIZE:
        {
            Board_OnResize(LOWORD(lParam), HIWORD(lParam));
            Menu_OnResize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        // Suppress default background erase to prevent flicker/white flash.
        return 1;
    case WM_MOUSELEAVE:
        {
            if (g_appMode == MODE_GAME)
            {
                Board_OnMouseLeave();
            }
            // restore system cursor when leaving
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
        break;
    case WM_MOUSEMOVE:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (g_appMode == MODE_MENU)
            {
                Menu_OnMouseMove(x, y);
            }
            else
            {
                Board_OnMouseMove(x, y);
            }
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    case WM_LBUTTONDOWN:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (g_appMode == MODE_MENU)
            {
                MenuAction act = Menu_OnLButtonDown(x, y);
                if (act == MENU_ACTION_START)
                {
                    // If menu shows Resume, treat Start as resume; otherwise open New Game dialog
                    if (Menu_HasResume())
                    {
                        g_appMode = MODE_GAME; // resume
                    }
                    else
                    {
                        NewGameOptions opts = ShowNewGameDialog(hWnd);
                        if (opts.accepted)
                        {
                            // apply options to game logic
                            Game_Init(opts.boardSize, opts.opponentIsAI, opts.difficulty, opts.aiFirst);
                            g_appMode = MODE_GAME;
                            // starting a new game should hide "Resume"
                            Menu_SetHasResume(false);
                        }
                    }
                }
                else if (act == MENU_ACTION_NEWGAME)
                {
                    NewGameOptions opts = ShowNewGameDialog(hWnd);
                    if (opts.accepted)
                    {
                        Game_Init(opts.boardSize, opts.opponentIsAI, opts.difficulty, opts.aiFirst);
                        g_appMode = MODE_GAME;
                        Menu_SetHasResume(false);
                    }
                }
                else if (act == MENU_ACTION_HELP)
                {
                    // TODO: open documentation (e.g., ShellExecute)
                }
                else if (act == MENU_ACTION_LOAD)
                {
                    // show Open File dialog for .pbn files
                    WCHAR szFile[MAX_PATH] = {};
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFilter = L"Amazons Chess Game Record (*.pbn)\0*.pbn\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    ofn.lpstrDefExt = L"pbn";
                    if (GetOpenFileNameW(&ofn))
                    {
                        std::vector<std::wstring> lines;
                        int fileBoardSize = 8; bool fileOppIsAI = true; bool fileAIFirst = false;
                        if (LoadHistoryFromFile(szFile, lines, fileBoardSize, fileOppIsAI, fileAIFirst))
                        {
                            // reinit game using header values
                            Game_Init(fileBoardSize, fileOppIsAI, 1, fileAIFirst);
                            // replay moves
                            for (const auto &entry : lines)
                            {
                                if (entry.empty()) continue;
                                size_t pos = entry.find(L"] ");
                                if (pos == std::wstring::npos) continue;
                                std::wstring rest = entry.substr(pos + 2);
                                std::wstringstream ss(rest);
                                std::wstring a,b,c;
                                if (!(ss >> a >> b >> c)) continue;
                                if (a.size() < 2 || b.size() < 2 || c.size() < 2) continue;
                                int fromC = a[0] - L'A';
                                int fromR = _wtoi(a.substr(1).c_str()) - 1;
                                int toC = b[0] - L'A';
                                int toR = _wtoi(b.substr(1).c_str()) - 1;
                                int arrowC = c[0] - L'A';
                                int arrowR = _wtoi(c.substr(1).c_str()) - 1;
                                Game_MakeMove(fromR, fromC, toR, toC, arrowR, arrowC);
                            }
                            // switch to game interface
                            g_appMode = MODE_GAME;
                            Menu_SetHasResume(false);
                        }
                        else
                        {
                            MessageBoxW(hWnd, L"Failed to load file.", L"Load Error", MB_OK | MB_ICONERROR);
                        }
                    }
                }
            }
            else
            {
                Board_OnLButtonDown(x, y);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    case WM_PAINT:
        {
            if (g_appMode == MODE_MENU)
            {
                Menu_Render(hWnd);
            }
            else
            {
                Board_Render(hWnd);
            }
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        Board_Cleanup();
        Menu_Cleanup();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// “关于”框的消息处理程序。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
