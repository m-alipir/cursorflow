#include "crash_restore.h"
#include "cursor_scheme.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <string>

namespace crash_restore {
namespace {

constexpr wchar_t kWatchdogFlag[] = L"--watchdog-pid";

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS*) {
    cursor_scheme::Restore();
    return EXCEPTION_CONTINUE_SEARCH;
}

void OnNormalExit() {
    cursor_scheme::Restore();
}

void SpawnWatchdog() {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        return;
    }

    std::wstring cmdLine = L"\"" + std::wstring(exePath) + L"\" " +
                            kWatchdogFlag + L" " +
                            std::to_wstring(GetCurrentProcessId());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exePath, cmdLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

}  // namespace

void Install() {
    SetUnhandledExceptionFilter(OnUnhandledException);
    std::atexit(OnNormalExit);
    SpawnWatchdog();
}

bool RunIfWatchdogMode() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return false;
    }

    bool isWatchdog = false;
    DWORD targetPid = 0;
    for (int i = 1; i < argc - 1; ++i) {
        if (kWatchdogFlag == std::wstring(argv[i])) {
            isWatchdog = true;
            targetPid = static_cast<DWORD>(_wtoi(argv[i + 1]));
            break;
        }
    }
    LocalFree(argv);

    if (!isWatchdog) {
        return false;
    }

    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, targetPid);
    if (hProcess) {
        WaitForSingleObject(hProcess, INFINITE);
        CloseHandle(hProcess);
    }
    cursor_scheme::Restore();
    return true;
}

}  // namespace crash_restore
