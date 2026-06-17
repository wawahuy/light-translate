#pragma once
#include <windows.h>
#include <functional>
#include "src/ui/RoiHelperWindow.h"

class CaptureHelperWindow
{
public:
    CaptureHelperWindow();
    ~CaptureHelperWindow();

    bool Create(HINSTANCE hInstance);
    void Destroy();

    void Show(bool show);
    bool IsVisible() const;

    RECT GetRect() const;
    void SetRect(const RECT& rc);

    void ShowRoi(bool show);
    RECT GetRoiRect() const;
    void SetRoiRect(const RECT& rc);
    void CenterRoi();

    std::function<void(const RECT&)> OnRectChanged;
    std::function<void(const RECT&)> OnRoiRectChanged;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    RoiHelperWindow m_roiHelper;
    static constexpr wchar_t CLASS_NAME[] = L"GameTranslate_CaptureHelper";
};
