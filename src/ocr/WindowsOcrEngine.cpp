#include "src/ocr/WindowsOcrEngine.h"
#include "src/utils/StringUtils.h"
#include <opencv2/imgproc.hpp>

// Windows SDK / WinRT headers
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Globalization.h>
#include <unknwn.h>
#include <inspectable.h>

// IMemoryBufferByteAccess definition for raw access to SoftwareBitmap buffer.
struct __declspec(uuid("5b0d3235-4dbd-4d14-bb78-cd3d15a8dd6f"))
IMemoryBufferByteAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetBuffer(uint8_t** value, uint32_t* capacity) = 0;
};

// COM Initializer helper to ensure COM MTA is initialized on standard background threads.
struct COMInitializer
{
    COMInitializer()
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        catch (...)
        {
            // COM might already be initialized (e.g. on main thread), which is fine.
        }
    }
};

static void EnsureCOMInitializedForCurrentThread()
{
    thread_local COMInitializer initializer;
}

struct WindowsOcrEngine::Impl
{
    winrt::Windows::Media::Ocr::OcrEngine ocrEngine{ nullptr };
};

WindowsOcrEngine::WindowsOcrEngine()
    : m_impl(std::make_unique<Impl>())
{
}

WindowsOcrEngine::~WindowsOcrEngine() = default;

bool WindowsOcrEngine::Initialize()
{
    EnsureCOMInitializedForCurrentThread();

    if (m_initialized) return true;

    try
    {
        // Try creating from user profile languages
        m_impl->ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();

        if (!m_impl->ocrEngine)
        {
            // Fallback to the first available OCR-capable language on the system
            auto languages = winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
            if (languages.Size() > 0)
            {
                m_impl->ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(languages.GetAt(0));
            }
        }

        m_initialized = (m_impl->ocrEngine != nullptr);
    }
    catch (...)
    {
        m_initialized = false;
    }

    return m_initialized;
}

bool WindowsOcrEngine::IsInitialized() const
{
    return m_initialized;
}

OcrResult WindowsOcrEngine::Recognize(const cv::Mat& bgrFrame)
{
    OcrResult result;
    if (!m_initialized) return result;

    EnsureCOMInitializedForCurrentThread();

    try
    {
        // Windows OCR requires BGRA format
        cv::Mat bgraMat;
        if (bgrFrame.channels() == 3)
        {
            cv::cvtColor(bgrFrame, bgraMat, cv::COLOR_BGR2BGRA);
        }
        else if (bgrFrame.channels() == 4)
        {
            bgraMat = bgrFrame.clone();
        }
        else
        {
            // Fallback for single-channel grayscale or other unexpected formats
            cv::cvtColor(bgrFrame, bgraMat, cv::COLOR_GRAY2BGRA);
        }

        int w = bgraMat.cols;
        int h = bgraMat.rows;

        // Create SoftwareBitmap and copy pixels
        winrt::Windows::Graphics::Imaging::SoftwareBitmap softwareBitmap(
            winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8, w, h,
            winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied);

        {
            winrt::Windows::Graphics::Imaging::BitmapBuffer buffer = softwareBitmap.LockBuffer(
                winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode::Write);
            winrt::Windows::Foundation::IMemoryBufferReference reference = buffer.CreateReference();
            auto byteAccess = reference.as<IMemoryBufferByteAccess>();
            uint8_t* pData = nullptr;
            uint32_t capacity = 0;

            if (SUCCEEDED(byteAccess->GetBuffer(&pData, &capacity)))
            {
                // Copy row-by-row to handle potential stride alignment
                for (int y = 0; y < h; ++y)
                {
                    std::memcpy(pData + y * w * 4, bgraMat.ptr<uint8_t>(y), w * 4);
                }
            }
        }

        // Run Windows OCR synchronously (blocking .get() call is safe on background worker thread)
        auto winrtOcrResult = m_impl->ocrEngine.RecognizeAsync(softwareBitmap).get();

        if (winrtOcrResult)
        {
            for (auto line : winrtOcrResult.Lines())
            {
                result.texts.push_back(WideToUtf8(line.Text().c_str()));
            }
        }
    }
    catch (...)
    {
        // Return empty result in case of runtime exceptions
    }

    return result;
}

void WindowsOcrEngine::Reset()
{
    EnsureCOMInitializedForCurrentThread();
    m_impl->ocrEngine = nullptr;
    m_initialized = false;
}
