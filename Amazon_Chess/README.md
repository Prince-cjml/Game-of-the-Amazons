# Game of the Amazons - Project Overview

This project aims to create a GUI Windows desktop game implementing the "Game of the Amazons". The repository currently contains a skeleton Win32 application created by Visual Studio.

What I changed
- Set the application window title to `Game of the Amazons: Conquer and Win!`.
- Made the program create a borderless fullscreen window by default.

Project structure (typical for a simple Win32 project)
- `Amazon_Chess.cpp` - Main application file. Contains `wWinMain`, window registration, creation, and message loop.
- Resource files (icons, dialogs, menus) - Usually in `.rc` and resource header `resource.h`.
- `framework.h`, `Amazon_Chess.h` - Project headers for precompiled headers and resource IDs.

How the application starts
1. `wWinMain` is the entry point. It prepares global strings for the window class and title, registers the window class, and calls `InitInstance`.
2. `InitInstance` creates the main window and shows it. In this project, it creates a borderless window sized to the primary display resolution.
3. The message loop in `wWinMain` runs until a quit message is posted.

Next steps to build the game GUI
- Decide on rendering approach:
  - GDI/GDI+ (simple, built-in) for 2D rendering.
  - Direct2D/DirectWrite for better 2D and text rendering.
  - Direct3D if you want GPU-accelerated rendering (more complex).
- Input handling: use `WM_LBUTTONDOWN`, `WM_MOUSEMOVE`, `WM_LBUTTONUP`, and keyboard messages for interaction.
- Game state & logic: keep game board, moves, and rules in plain C++ classes. Use the GUI only to present and interact.
- Double-buffering: avoid flicker when drawing the board. You can use an off-screen compatible bitmap and `BitBlt`.

If you prefer a higher-level UI framework (recommended for faster development):
- Use WinUI/Win32 with XAML (modern, but more setup).
- Use a cross-platform GUI library like SDL, SFML, or Qt. Qt provides designer tools and is feature-rich.

How I can help next
- Implement a simple board renderer in GDI and handle mouse clicks.
- Add game logic and move validation.
- Add menus or a simple toolbar for game controls (new game, undo, exit).

If you want, tell me which rendering/input approach you prefer and I will implement the next step.
