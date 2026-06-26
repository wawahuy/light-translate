#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

enum class UpdateStatus
{
    Idle,
    Checking,
    UpToDate,
    UpdateAvailable,
    Downloading,
    DownloadSuccess,
    DownloadFailed,
    Error
};

class Updater
{
public:
    Updater();
    ~Updater();

    // Check for updates in a background thread to prevent blocking the UI loop.
    void CheckForUpdateAsync(const std::string& currentVersion, std::function<void()> onFinished = nullptr);
    
    // Download the release package in a background thread to keep the interface responsive.
    void DownloadUpdateAsync(std::function<void()> onFinished = nullptr);
    
    // Cancel the ongoing download thread.
    void CancelDownload();
    
    // Copy and launch updater.exe from TEMP directory to avoid file locking on the running exe.
    bool InstallAndRestart();

    UpdateStatus GetStatus() const { return m_status.load(); }
    std::string GetLatestVersion() const { return m_latestVersion; }
    std::string GetReleaseNotes() const { return m_releaseNotes; }
    std::wstring GetErrorMessage() const { return m_errorMessage; }
    float GetDownloadProgress() const { return m_downloadProgress.load(); }

private:
    void RunCheck(const std::string& currentVersion, std::function<void()> onFinished);
    void RunDownload(std::function<void()> onFinished);

    bool SendHttpGetRequest(const std::wstring& host, int port, const std::wstring& path, std::string& outResponse);
    bool DownloadFile(const std::string& url, const std::wstring& targetPath);

    static bool IsNewerVersion(const std::string& current, const std::string& latest);
    static std::wstring GetAppDirectory();
    static std::wstring GetTempDirectory();

    std::atomic<UpdateStatus> m_status{ UpdateStatus::Idle };
    std::string m_latestVersion;
    std::string m_releaseNotes;
    std::string m_downloadUrl;
    std::wstring m_errorMessage;
    std::atomic<float> m_downloadProgress{ 0.0f };
    std::atomic<bool> m_cancelRequested{ false };

    std::thread m_workerThread;
};
