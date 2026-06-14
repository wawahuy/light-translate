#pragma once
#include <string>

/// Convert a wide (UTF-16) string to a UTF-8 std::string.
[[nodiscard]] std::string  WideToUtf8(const std::wstring& wstr);

/// Convert a UTF-8 std::string to a wide (UTF-16) std::wstring.
[[nodiscard]] std::wstring Utf8ToWide(const std::string& str);

/// Check whether OCR model files (inference.json + inference.pdiparams) exist
/// in the given directory.
[[nodiscard]] bool CheckOcrModelExists(const std::wstring& dir);
