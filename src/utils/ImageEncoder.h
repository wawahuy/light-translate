#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <cstdint>

/// Encode a BGRA frame to JPEG using GDI+.
///
/// @param bgraData  Pointer to raw BGRA pixel data (4 bytes per pixel, top-down)
/// @param width     Image width in pixels
/// @param height    Image height in pixels
/// @param quality   JPEG quality 0..100  (75 is a good default)
/// @param outJpeg   Receives the encoded JPEG bytes
/// @returns         true on success
bool EncodeBGRAtoJPEG(const uint8_t* bgraData, int width, int height,
                      int quality, std::vector<uint8_t>& outJpeg);

/// Must be called once at process startup before any EncodeBGRAtoJPEG call.
/// @param token  Receives the GDI+ token (pass to ShutdownGDIPlus at exit)
bool InitGDIPlus(ULONG_PTR& token);

/// Call at process exit with the token returned by InitGDIPlus.
void ShutdownGDIPlus(ULONG_PTR token);
