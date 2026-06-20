#pragma once
#include <string>
#include <vector>

// Simple coordinate struct for OCR box vertices
struct Point2F { float x; float y; };

/// Interface for translation display/output windows.
class ITranslationOutput
{
public:
    virtual ~ITranslationOutput() = default;

    // Set the displayed text.
    virtual void SetText(const std::wstring& text) = 0;

    // Set text and detected text regions for in-place rendering.
    virtual void SetInPlaceText(const std::wstring& text, const std::vector<std::vector<Point2F>>& boxes) = 0;

    // Move the output display to (x, y) in screen coordinates.
    virtual void SetPosition(int x, int y) = 0;

    // Set the size of the output display.
    virtual void SetSize(int w, int h) = 0;

    // Show the output display.
    virtual void Show() = 0;

    // Hide the output display.
    virtual void Hide() = 0;

    // Check if the output display is visible.
    virtual bool IsVisible() const = 0;
};
