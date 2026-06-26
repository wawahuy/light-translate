#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 4)
    {
        MessageBoxW(nullptr, L"This updater must be run from Light Translate.", L"Light Translate Updater", MB_ICONERROR);
        if (argv) LocalFree(argv);
        return 1;
    }

    DWORD parentPid = std::stoul(argv[1]);
    std::wstring zipPath = argv[2];
    std::wstring appDir = argv[3];
    LocalFree(argv);

    // Wait for parent process to exit so locked DLLs and EXE can be overwritten.
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (hProcess)
    {
        WaitForSingleObject(hProcess, 10000);
        CloseHandle(hProcess);
    }

    // Use built-in tar.exe to extract the update package, avoiding third-party zip library dependencies.
    std::wstring systemDir(MAX_PATH, L'\0');
    UINT sysLen = GetSystemDirectoryW(&systemDir[0], MAX_PATH);
    systemDir.resize(sysLen);
    std::wstring tarPath = systemDir + L"\\tar.exe";

    std::wstring cmdLine = L"\"" + tarPath + L"\" -xf \"" + zipPath + L"\" -C \"" + appDir + L"\"";

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdLineBuf(cmdLine.begin(), cmdLine.end());
    cmdLineBuf.push_back(L'\0');

    bool extractSuccess = false;
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
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        extractSuccess = (exitCode == 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    if (!extractSuccess)
    {
        MessageBoxW(nullptr, L"Failed to extract update package.", L"Update Error", MB_ICONERROR);
        return 2;
    }

    DeleteFileW(zipPath.c_str());

    // Restart the main application.
    std::wstring mainExePath = appDir + L"\\light_translate.exe";
    STARTUPINFOW siMain = { sizeof(siMain) };
    PROCESS_INFORMATION piMain = {};
    if (CreateProcessW(
        mainExePath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        appDir.c_str(),
        &siMain,
        &piMain
    ))
    {
        CloseHandle(piMain.hProcess);
        CloseHandle(piMain.hThread);
    }
    else
    {
        MessageBoxW(nullptr, L"Failed to restart Light Translate.", L"Update Error", MB_ICONERROR);
        return 3;
    }

    return 0;
}
