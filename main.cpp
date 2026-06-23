#include <windows.h>
#include <commctrl.h>

#include "src/ui/SettingsWindow.h"
#include "src/utils/ImageEncoder.h"
#include "resource.h"

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ PWSTR lpCmdLine,
    _In_ int nShowCmd)
{
    // Enable DPI awareness (must be first, before any window is created)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Initialise Common Controls (required for some visual styles)
    INITCOMMONCONTROLSEX icce{};
    icce.dwSize = sizeof(icce);
    icce.dwICC  = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icce);

    // Initialise GDI+ (required by ImageEncoder and OverlayWindow)
    ULONG_PTR gdiplusToken = 0;
    if (!InitGDIPlus(gdiplusToken))
    {
        MessageBoxW(nullptr,
            L"Failed to initialise GDI+.",
            L"Game Translation Overlay", MB_ICONERROR);
        return 1;
    }

    // Create and show the main settings window
    SettingsWindow settingsWnd;
    if (!settingsWnd.Create(hInstance))
    {
        MessageBoxW(nullptr,
            L"Failed to create the settings window.",
            L"Game Translation Overlay", MB_ICONERROR);
        ShutdownGDIPlus(gdiplusToken);
        return 1;
    }

    ShowWindow(settingsWnd.GetHWND(), nShowCmd);
    UpdateWindow(settingsWnd.GetHWND());

    // -- Message & Render loop -------------------------------------------------
    int result = settingsWnd.RunMessageLoop();

    // Cleanup
    ShutdownGDIPlus(gdiplusToken);

    return result;
}
