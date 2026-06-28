#pragma once
#include <windows.h>
#include <vector>
#include <string>

enum class GraphicsAPI {
    DirectX11,
    OpenGL
};

const std::vector<std::string> g_Subtitles = {
    "Welcome to the Graphics Hooking Test Application.",
    "This program is designed to test in-game hooking mechanisms.",
    "Currently, we are rendering using the selected graphics API.",
    "Observe the subtitle text at the bottom of the screen.",
    "The translation tool should detect this English text and overlay the Vietnamese translation.",
    "Let's check if the OCR engine can accurately capture this dynamic text.",
    "Graphic hooking allows transparent rendering over third-party applications.",
    "DirectX 11 and OpenGL are the two major graphics APIs supported here.",
    "Ensure the frame rate remains stable during the hooking process.",
    "Press the buttons above to switch between different rendering backends.",
    "Subtitles change every four seconds to keep testing dynamic.",
    "Test App status: Running normally, rendering at target frame rate."
};

class IGraphicsRenderer {
public:
    virtual ~IGraphicsRenderer() = default;
    virtual bool Initialize(HWND hwnd) = 0;
    virtual void Shutdown() = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Resize(int width, int height) = 0;
};
