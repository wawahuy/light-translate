#pragma once
#include <windows.h>
#include <functional>

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

    std::function<void(const RECT&)> OnRectChanged;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    static constexpr wchar_t CLASS_NAME[] = L"GameTranslate_CaptureHelper";
};
