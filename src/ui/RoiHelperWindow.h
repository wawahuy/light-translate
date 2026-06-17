#pragma once
#include <windows.h>
#include <functional>

class RoiHelperWindow
{
public:
    RoiHelperWindow();
    ~RoiHelperWindow();

    bool Create(HWND parentHwnd, HINSTANCE hInstance);
    void Destroy();

    void Show(bool show);
    bool IsVisible() const;

    RECT GetRect() const;
    void SetRect(const RECT& rc);

    void CenterAndReset();
    void EnsureWithinBounds();
    void OnParentResize(int newPW, int newPH);

    std::function<void(const RECT&)> OnRectChanged;

private:
    enum class DragType
    {
        NONE,
        MOVE,
        RESIZE_L,
        RESIZE_R,
        RESIZE_T,
        RESIZE_B,
        RESIZE_TL,
        RESIZE_TR,
        RESIZE_BL,
        RESIZE_BR
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND      m_hwnd = nullptr;
    HWND      m_parentHwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    DragType  m_dragType = DragType::NONE;
    POINT     m_startMouse = {};
    RECT      m_startRect = {};

    static constexpr wchar_t CLASS_NAME[] = L"GameTranslate_RoiHelper";
    int       m_parentW = 0;
    int       m_parentH = 0;
};
