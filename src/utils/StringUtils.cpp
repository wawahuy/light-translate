#include "src/utils/StringUtils.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

std::string WideToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string strTo(sizeNeeded - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &strTo[0], sizeNeeded, nullptr, nullptr);
    return strTo;
}

std::wstring Utf8ToWide(const std::string& str)
{
    if (str.empty()) return {};
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (sizeNeeded <= 0) return {};
    std::wstring wstrTo(sizeNeeded - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstrTo[0], sizeNeeded);
    return wstrTo;
}

bool CheckOcrModelExists(const std::wstring& dir)
{
    std::wstring modelPath  = dir + L"/inference.json";
    std::wstring paramsPath = dir + L"/inference.pdiparams";
    DWORD dwAttrib1 = GetFileAttributesW(modelPath.c_str());
    DWORD dwAttrib2 = GetFileAttributesW(paramsPath.c_str());
    return (dwAttrib1 != INVALID_FILE_ATTRIBUTES && !(dwAttrib1 & FILE_ATTRIBUTE_DIRECTORY)) &&
           (dwAttrib2 != INVALID_FILE_ATTRIBUTES && !(dwAttrib2 & FILE_ATTRIBUTE_DIRECTORY));
}
