#include "src/utils/Updater.h"
#include "src/utils/StringUtils.h"
#include "third_party/nlohmann/json.hpp"
#include <winhttp.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

Updater::Updater() = default;

Updater::~Updater()
{
    CancelDownload();
    if (m_workerThread.joinable())
    {
        m_workerThread.join();
    }
}

void Updater::CheckForUpdateAsync(const std::string& currentVersion, std::function<void()> onFinished)
{
    if (m_status.load() == UpdateStatus::Checking || m_status.load() == UpdateStatus::Downloading)
        return;

    m_status = UpdateStatus::Checking;
    m_errorMessage.clear();

    // Clean up the previous thread object before spinning up a new one.
    if (m_workerThread.joinable())
    {
        m_workerThread.join();
    }

    m_workerThread = std::thread(&Updater::RunCheck, this, currentVersion, onFinished);
}

void Updater::DownloadUpdateAsync(std::function<void()> onFinished)
{
    if (m_status.load() != UpdateStatus::UpdateAvailable && 
        m_status.load() != UpdateStatus::DownloadFailed && 
        m_status.load() != UpdateStatus::Error)
    {
        return;
    }

    m_status = UpdateStatus::Downloading;
    m_errorMessage.clear();

    // Clean up the previous thread object before spinning up a new one.
    if (m_workerThread.joinable())
    {
        m_workerThread.join();
    }

    m_workerThread = std::thread(&Updater::RunDownload, this, onFinished);
}

void Updater::CancelDownload()
{
    if (m_status.load() == UpdateStatus::Downloading)
    {
        m_cancelRequested = true;
    }
}

void Updater::RunCheck(const std::string& currentVersion, std::function<void()> onFinished)
{
    std::string response;
    // Query GitHub API endpoint for the latest release.
    if (!SendHttpGetRequest(L"api.github.com", 443, L"/repos/wawahuy/light-translate/releases/latest", response))
    {
        m_status = UpdateStatus::Error;
        if (m_errorMessage.empty())
        {
            m_errorMessage = L"Failed to connect to GitHub API.";
        }
        if (onFinished) onFinished();
        return;
    }

    try
    {
        auto j = nlohmann::json::parse(response);
        m_latestVersion = j.at("tag_name").get<std::string>();
        m_releaseNotes = j.value("body", "");

        bool foundAsset = false;
        if (j.contains("assets") && j["assets"].is_array())
        {
            for (const auto& asset : j["assets"])
            {
                std::string name = asset.value("name", "");
                if (name == "light-translate-windows-x64.zip")
                {
                    m_downloadUrl = asset.value("browser_download_url", "");
                    foundAsset = true;
                    break;
                }
            }
        }

        if (!foundAsset)
        {
            m_status = UpdateStatus::Error;
            m_errorMessage = L"Could not find light-translate-windows-x64.zip in the latest release.";
            if (onFinished) onFinished();
            return;
        }

        if (IsNewerVersion(currentVersion, m_latestVersion))
        {
            m_status = UpdateStatus::UpdateAvailable;
        }
        else
        {
            m_status = UpdateStatus::UpToDate;
        }
    }
    catch (const std::exception& e)
    {
        m_status = UpdateStatus::Error;
        m_errorMessage = L"Data parsing error: " + Utf8ToWide(e.what());
    }

    if (onFinished) onFinished();
}

void Updater::RunDownload(std::function<void()> onFinished)
{
    std::wstring tempDir = GetTempDirectory();
    std::wstring zipPath = tempDir + L"\\light_translate_update.zip";

    if (DownloadFile(m_downloadUrl, zipPath))
    {
        m_status = UpdateStatus::DownloadSuccess;
    }
    else
    {
        if (m_cancelRequested.load())
        {
            m_status = UpdateStatus::Idle;
        }
        else
        {
            m_status = UpdateStatus::DownloadFailed;
            if (m_errorMessage.empty())
            {
                m_errorMessage = L"Failed to download the update package.";
            }
        }
    }

    if (onFinished) onFinished();
}

bool Updater::SendHttpGetRequest(const std::wstring& host, int port, const std::wstring& path, std::string& outResponse)
{
    outResponse.clear();

    HINTERNET hSession = WinHttpOpen(
        L"LightTranslate/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return false;

    // Follow HTTP redirects automatically since GitHub release assets redirect to S3 storage.
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), static_cast<INTERNET_PORT>(port), 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );

    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Set User-Agent and Accept headers required by GitHub API to prevent HTTP 403.
    std::wstring headers = L"User-Agent: light-translate-updater\r\nAccept: application/vnd.github.v3+json\r\n";
    WinHttpAddRequestHeaders(hRequest, headers.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0;
    DWORD len = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &len, WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200)
    {
        m_errorMessage = L"HTTP error: " + std::to_wstring(statusCode);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::vector<char> buffer(4096);
    while (true)
    {
        DWORD sizeToRead = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &sizeToRead)) break;
        if (sizeToRead == 0) break;

        if (sizeToRead > buffer.size())
            buffer.resize(sizeToRead);

        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), sizeToRead, &bytesRead)) break;
        outResponse.append(buffer.data(), bytesRead);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return true;
}

bool Updater::DownloadFile(const std::string& url, const std::wstring& targetPath)
{
    m_downloadProgress = 0.0f;
    m_cancelRequested = false;

    std::wstring wUrl = Utf8ToWide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t scheme[16]{}, host[256]{}, path[2048]{}, extra[2048]{};
    uc.lpszScheme      = scheme;  uc.dwSchemeLength      = 16;
    uc.lpszHostName    = host;    uc.dwHostNameLength    = 256;
    uc.lpszUrlPath     = path;    uc.dwUrlPathLength     = 2048;
    uc.lpszExtraInfo   = extra;   uc.dwExtraInfoLength   = 2048;

    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &uc))
    {
        m_errorMessage = L"Error parsing download URL.";
        return false;
    }

    HINTERNET hSession = WinHttpOpen(
        L"LightTranslate/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return false;

    // Follow HTTP redirects automatically since GitHub release assets redirect to S3 storage.
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring fullPath = path;
    if (extra[0]) fullPath += extra;

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        fullPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring headers = L"User-Agent: light-translate-updater\r\n";
    WinHttpAddRequestHeaders(hRequest, headers.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0;
    DWORD len = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &len, WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200)
    {
        m_errorMessage = L"HTTP error: " + std::to_wstring(statusCode);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD contentLength = 0;
    len = sizeof(contentLength);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &len, WINHTTP_NO_HEADER_INDEX);

    HANDLE hFile = CreateFileW(targetPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        m_errorMessage = L"Could not create file to store the update package.";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::vector<char> buffer(65536);
    DWORD totalDownloaded = 0;
    bool success = true;

    while (true)
    {
        if (m_cancelRequested.load())
        {
            success = false;
            m_errorMessage = L"Download process canceled.";
            break;
        }

        DWORD sizeToRead = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &sizeToRead))
        {
            success = false;
            m_errorMessage = L"Error querying data from the server.";
            break;
        }

        if (sizeToRead == 0) break;

        if (sizeToRead > buffer.size())
            buffer.resize(sizeToRead);

        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), sizeToRead, &bytesRead))
        {
            success = false;
            m_errorMessage = L"Error reading data from the server.";
            break;
        }

        DWORD bytesWritten = 0;
        if (!WriteFile(hFile, buffer.data(), bytesRead, &bytesWritten, nullptr) || bytesWritten != bytesRead)
        {
            success = false;
            m_errorMessage = L"Error writing data to disk.";
            break;
        }

        totalDownloaded += bytesRead;
        if (contentLength > 0)
        {
            m_downloadProgress = static_cast<float>(totalDownloaded) / contentLength;
        }
    }

    CloseHandle(hFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!success)
    {
        DeleteFileW(targetPath.c_str());
    }
    return success;
}

bool Updater::IsNewerVersion(const std::string& current, const std::string& latest)
{
    auto CleanVersion = [](const std::string& v) {
        std::string res = v;
        if (!res.empty() && (res[0] == 'v' || res[0] == 'V'))
        {
            res = res.substr(1);
        }
        return res;
    };
    std::string cleanCur = CleanVersion(current);
    std::string cleanLat = CleanVersion(latest);

    int curMajor = 0, curMinor = 0, curPatch = 0;
    int latMajor = 0, latMinor = 0, latPatch = 0;

    sscanf_s(cleanCur.c_str(), "%d.%d.%d", &curMajor, &curMinor, &curPatch);
    sscanf_s(cleanLat.c_str(), "%d.%d.%d", &latMajor, &latMinor, &latPatch);

    if (latMajor != curMajor) return latMajor > curMajor;
    if (latMinor != curMinor) return latMinor > curMinor;
    return latPatch > curPatch;
}

std::wstring Updater::GetAppDirectory()
{
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer);
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"" : path.substr(0, pos);
}

std::wstring Updater::GetTempDirectory()
{
    wchar_t buffer[MAX_PATH];
    GetTempPathW(MAX_PATH, buffer);
    return std::wstring(buffer);
}

bool Updater::InstallAndRestart()
{
    if (GetStatus() != UpdateStatus::DownloadSuccess) return false;

    std::wstring appDir = GetAppDirectory();
    std::wstring tempDir = GetTempDirectory();

    std::wstring srcUpdaterPath = appDir + L"\\updater.exe";
    std::wstring dstUpdaterPath = tempDir + L"\\light_translate_updater.exe";
    std::wstring zipPath = tempDir + L"\\light_translate_update.zip";

    // Copy updater.exe to TEMP before running it so it doesn't lock itself during overwrite.
    if (!CopyFileW(srcUpdaterPath.c_str(), dstUpdaterPath.c_str(), FALSE))
    {
        m_errorMessage = L"Could not copy updater to TEMP directory. Error: " + std::to_wstring(GetLastError());
        return false;
    }

    // Pass parent PID, package ZIP path, and application folder as command-line arguments.
    DWORD currentPid = GetCurrentProcessId();
    std::wstring cmdLine = L"\"" + dstUpdaterPath + L"\" " + std::to_wstring(currentPid) + L" \"" + zipPath + L"\" \"" + appDir + L"\"";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdLineBuf(cmdLine.begin(), cmdLine.end());
    cmdLineBuf.push_back(L'\0');

    // Run the updater silently without creating a console window.
    if (CreateProcessW(
        nullptr,
        cmdLineBuf.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    ))
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        PostQuitMessage(0);
        return true;
    }

    m_errorMessage = L"Could not launch the helper updater process. Error: " + std::to_wstring(GetLastError());
    return false;
}
