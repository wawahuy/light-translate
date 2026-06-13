#include "src/utils/ImageEncoder.h"
#include <objbase.h>   // MinGW-w64: must include before gdiplus to have PROPID
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// -- GDI+ lifecycle ------------------------------------------------------------

bool InitGDIPlus(ULONG_PTR& token)
{
    Gdiplus::GdiplusStartupInput input;
    return Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
}

void ShutdownGDIPlus(ULONG_PTR token)
{
    Gdiplus::GdiplusShutdown(token);
}

// -- Helpers -------------------------------------------------------------------

static bool GetEncoderClsid(const wchar_t* mimeType, CLSID& clsid)
{
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;

    auto* codecs = static_cast<Gdiplus::ImageCodecInfo*>(malloc(size));
    if (!codecs) return false;

    Gdiplus::GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(codecs[i].MimeType, mimeType) == 0)
        {
            clsid = codecs[i].Clsid;
            free(codecs);
            return true;
        }
    }
    free(codecs);
    return false;
}

// -- EncodeBGRAtoJPEG ----------------------------------------------------------

bool EncodeBGRAtoJPEG(const uint8_t* bgraData, int width, int height,
                      int quality, std::vector<uint8_t>& outJpeg)
{
    if (!bgraData || width <= 0 || height <= 0) return false;

    // GDI+ PixelFormat32bppARGB stores bytes as B,G,R,A in memory (little-endian)
    // which matches DXGI BGRA output exactly.
    Gdiplus::Bitmap bmp(
        width, height,
        width * 4,
        PixelFormat32bppARGB,
        const_cast<uint8_t*>(bgraData)
    );
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;

    CLSID jpegClsid;
    if (!GetEncoderClsid(L"image/jpeg", jpegClsid)) return false;

    // Set quality
    Gdiplus::EncoderParameters ep;
    ep.Count = 1;
    ep.Parameter[0].Guid              = Gdiplus::EncoderQuality;
    ep.Parameter[0].Type              = Gdiplus::EncoderParameterValueTypeLong;
    ep.Parameter[0].NumberOfValues    = 1;
    ULONG qual = static_cast<ULONG>(quality);
    ep.Parameter[0].Value             = &qual;

    // Encode to in-memory IStream
    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &pStream)))
        return false;

    if (bmp.Save(pStream, &jpegClsid, &ep) != Gdiplus::Ok)
    {
        pStream->Release();
        return false;
    }

    STATSTG stat{};
    pStream->Stat(&stat, STATFLAG_NONAME);
    ULONG sz = static_cast<ULONG>(stat.cbSize.QuadPart);
    outJpeg.resize(sz);

    LARGE_INTEGER li{};
    pStream->Seek(li, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    pStream->Read(outJpeg.data(), sz, &read);
    pStream->Release();

    outJpeg.resize(read);
    return read > 0;
}
