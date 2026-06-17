#include "src/ocr/WindowsOcrEngine.h"
#include "src/utils/StringUtils.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace Windows::Storage::Streams;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Foundation;

WindowsOcrEngine::WindowsOcrEngine() = default;

WindowsOcrEngine::~WindowsOcrEngine()
{
    Reset();
}

bool WindowsOcrEngine::Initialize()
{
    if (m_initialized) return true;

    try
    {
        // Initialise COM thread apartment
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        
        m_ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        if (!m_ocrEngine)
        {
            throw std::runtime_error("Failed to create Windows OcrEngine (UserProfileLanguages check failed or no language packs installed).");
        }
        m_initialized = true;
    }
    catch (const winrt::hresult_error& ex)
    {
        m_initialized = false;
        std::stringstream ss;
        ss << "Windows OCR initialization WinRT error: HRESULT 0x" << std::hex << ex.to_abi().value;
        throw std::runtime_error(ss.str());
    }
    catch (const std::exception&)
    {
        m_initialized = false;
        throw;
    }

    return m_initialized;
}

void WindowsOcrEngine::Reset()
{
    m_ocrEngine = nullptr;
    m_initialized = false;
}

cv::Mat WindowsOcrEngine::PrepareFrame(const cv::Mat& bgraFrame)
{
    return bgraFrame; // Zero-copy, returns a reference of the matrix
}

::OcrResult WindowsOcrEngine::Recognize(const cv::Mat& bgraFrame)
{
    ::OcrResult result;
    if (!m_initialized || !m_ocrEngine)
    {
        return result;
    }

    try
    {
        // Construct a BGRA SoftwareBitmap
        SoftwareBitmap softwareBitmap(
            BitmapPixelFormat::Bgra8,
            bgraFrame.cols,
            bgraFrame.rows,
            BitmapAlphaMode::Premultiplied
        );

        // Copy raw pixel bytes to bitmap buffer
        DataWriter writer;
        writer.WriteBytes({ bgraFrame.data, bgraFrame.data + (bgraFrame.total() * bgraFrame.elemSize()) });
        softwareBitmap.CopyFromBuffer(writer.DetachBuffer());

        // Process OCR synchronously
        winrt::Windows::Media::Ocr::OcrResult winrtOcrResult = m_ocrEngine.RecognizeAsync(softwareBitmap).get();

        // Extract lines, texts, and box coordinates
        for (auto const& line : winrtOcrResult.Lines())
        {
            // Convert wide string to UTF-8
            std::wstring wideLine = line.Text().c_str();
            result.texts.push_back(WideToUtf8(wideLine));

            // Compute the union bounding box of all words in the line
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            bool hasWords = false;
            for (auto const& word : line.Words())
            {
                Rect rect = word.BoundingRect();
                if (rect.X < minX) minX = rect.X;
                if (rect.Y < minY) minY = rect.Y;
                if (rect.X + rect.Width > maxX) maxX = rect.X + rect.Width;
                if (rect.Y + rect.Height > maxY) maxY = rect.Y + rect.Height;
                hasWords = true;
            }

            // Construct 4-point polygon box
            std::vector<cv::Point2f> box(4);
            if (hasWords)
            {
                box[0] = cv::Point2f(minX, minY);
                box[1] = cv::Point2f(maxX, minY);
                box[2] = cv::Point2f(maxX, maxY);
                box[3] = cv::Point2f(minX, maxY);
            }
            else
            {
                box[0] = cv::Point2f(0.0f, 0.0f);
                box[1] = cv::Point2f(0.0f, 0.0f);
                box[2] = cv::Point2f(0.0f, 0.0f);
                box[3] = cv::Point2f(0.0f, 0.0f);
            }

            result.boxes.push_back(std::move(box));
        }
    }
    catch (const winrt::hresult_error& ex)
    {
        std::stringstream ss;
        ss << "Windows OCR processing WinRT error: HRESULT 0x" << std::hex << ex.to_abi().value;
        throw std::runtime_error(ss.str());
    }
    catch (const std::exception&)
    {
        throw;
    }

    return result;
}
