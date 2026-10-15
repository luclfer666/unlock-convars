#include <Windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdarg>
#include <string>
#include <thread>
#include <atomic>

#include "Bridge.h"
#include "Inject.h"
#include "LogPath.h"
#include "TrayMenu.h"

#pragma comment(lib, "shell32.lib")

static constexpr const char* TARGET_PROC = "deadlock.exe";
static constexpr UINT  WM_TRAY_MSG = WM_APP + 1;

static constexpr UINT_PTR kHeartbeatTimer = 1;
static constexpr UINT     kHeartbeatMs    = 1000;
static constexpr UINT_PTR kWatchdogTimer  = 2;
static constexpr UINT     kWatchdogMs     = 1000;

static HANDLE      g_hMap    = nullptr;
static BridgeData* g_pBridge = nullptr;
static NOTIFYICONDATAA g_nid{};

static std::string g_sDllPath;
static std::atomic<bool> g_bLoaded{ false };
static std::atomic<bool> g_bInjectStarted{ false };

static FILE* g_pLog = nullptr;

static std::string ExeDir()
{
    char exe[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = exe;
    const auto pos = dir.find_last_of('\\');
    if (pos != std::string::npos)
        dir.resize(pos + 1);
    return dir;
}

static std::string LogFilePath(const char* szName)
{
    const std::string dir = ExeDir();
    LogPath::MigrateLegacy(dir.c_str());
    char szPath[MAX_PATH]{};
    LogPath::Build(szPath, sizeof(szPath), dir.c_str(), szName);
    return szPath;
}

static void LogV(const char* tag, const char* fmt, va_list args)
{
    char body[512];
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, args);
    printf("[%s] %s\n", tag, body);

    if (g_pLog)
    {
        fprintf(g_pLog, "[%s] %s\n", tag, body);
        fflush(g_pLog);
    }
}

static void LogInfo(const char* fmt, ...)
{
    va_list a; va_start(a, fmt); LogV("+", fmt, a); va_end(a);
}

static void LogWarn(const char* fmt, ...)
{
    va_list a; va_start(a, fmt); LogV("!", fmt, a); va_end(a);
}

static void LogErr(const char* fmt, ...)
{
    va_list a; va_start(a, fmt); LogV("-", fmt, a); va_end(a);
}

static void UpdateTip()
{
    _snprintf_s(g_nid.szTip, sizeof(g_nid.szTip), _TRUNCATE,
                "convars - %s", tray_menu::Status());
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

static bool BridgeCreate()
{
    g_hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                0, sizeof(BridgeData), kBridgeName);
    if (!g_hMap)
    {
        LogErr("BridgeCreate: CreateFileMapping failed");
        return false;
    }

    void* pView = MapViewOfFile(g_hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BridgeData));
    if (!pView)
    {
        CloseHandle(g_hMap);
        g_hMap = nullptr;
        LogErr("BridgeCreate: MapViewOfFile failed");
        return false;
    }

    g_pBridge = static_cast<BridgeData*>(pView);
    ZeroMemory(g_pBridge, sizeof(BridgeData));
    g_pBridge->version        = kBridgeVer;
    g_pBridge->bTrayMode      = true;
    g_pBridge->nFovValue      = 120;
    g_pBridge->nAspect        = 150;
    g_pBridge->nWriterAlive   = static_cast<int32_t>(GetTickCount64());
    LogInfo("bridge created (%u bytes, ver %d)", kBridgeSize, kBridgeVer);
    return true;
}

static void BridgeDestroy()
{
    if (g_pBridge)
    {
        UnmapViewOfFile(g_pBridge);
        g_pBridge = nullptr;
    }
    if (g_hMap)
    {
        CloseHandle(g_hMap);
        g_hMap = nullptr;
    }
}

static LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_TRAY_MSG)
    {
        if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP ||
            lParam == WM_LBUTTONDBLCLK || lParam == WM_CONTEXTMENU)
        {
            const int nResult = tray_menu::Popup(hWnd, g_pBridge);
            if (nResult == tray_menu::kResultExitKillGame)
            {
                LogInfo("exit + kill game requested");
                if (injector::KillProcessByName(TARGET_PROC))
                    LogInfo("%s terminated", TARGET_PROC);
                else
                    LogWarn("%s not running", TARGET_PROC);
                DestroyWindow(hWnd);
            }
        }
        return 0;
    }

    if (uMsg == WM_TIMER)
    {
        if (wParam == kHeartbeatTimer && g_pBridge)
            g_pBridge->nWriterAlive = static_cast<int32_t>(GetTickCount64());
        else if (wParam == kWatchdogTimer)
            UpdateTip();
        return 0;
    }

    if (uMsg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

static void StartInject()
{
    if (g_bInjectStarted.exchange(true))
        return;

    std::thread([]{
        tray_menu::SetStatus("waiting for deadlock.exe");
        LogInfo("waiting for %s", TARGET_PROC);

        const DWORD pid = injector::WaitForTarget();
        if (!pid)
        {
            tray_menu::SetStatus("target not found");
            return;
        }
        LogInfo("target PID: %lu", pid);

        Sleep(2000);

        char szErr[256]{};
        const bool ok = injector::Inject(g_sDllPath, pid, szErr, sizeof(szErr));
        g_bLoaded = ok;
        tray_menu::SetStatus(ok ? "injected" : "injection failed");
        if (ok)
        {
            LogInfo("injected OK (manual map)");
            if (szErr[0])
                LogErr("warning: %s", szErr);
        }
        else
        {
            LogErr("injection failed (manual map): %s", szErr[0] ? szErr : "unknown");
        }
    }).detach();
}

static bool RunTray()
{
    if (!BridgeCreate())
    {
        MessageBoxA(nullptr, "Failed to create the shared memory bridge.", "convars",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    static const char* kClass = "dysonbehindTrayWnd";
    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = TrayWndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassExA(&wc);

    HWND hWnd = CreateWindowExA(0, kClass, "convars", WS_POPUP,
                                0, 0, 0, 0, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!hWnd)
    {
        LogErr("tray window creation failed");
        BridgeDestroy();
        return false;
    }

    HICON hIcon = LoadIconA(nullptr, (LPCSTR)IDI_APPLICATION);
    {
        char exe[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        HICON h = ExtractIconA(GetModuleHandleA(nullptr), exe, 0);
        if (h && h != (HICON)(UINT_PTR)1)
            hIcon = h;
    }

    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hWnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_MSG;
    g_nid.hIcon            = hIcon;
    snprintf(g_nid.szTip, sizeof(g_nid.szTip), "convars");
    if (!Shell_NotifyIconA(NIM_ADD, &g_nid))
        LogWarn("Shell_NotifyIcon(NIM_ADD) failed");

    SetTimer(hWnd, kHeartbeatTimer, kHeartbeatMs, nullptr);
    SetTimer(hWnd, kWatchdogTimer, kWatchdogMs, nullptr);

    StartInject();

    bool bRun = true;
    while (bRun)
    {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                bRun = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!bRun)
            break;

        if (g_bLoaded.load() && !injector::IsProcessRunning(TARGET_PROC))
        {
            LogInfo("deadlock.exe exited - closing tray injector");
            bRun = false;
        }

        Sleep(30);
    }

    KillTimer(hWnd, kHeartbeatTimer);
    KillTimer(hWnd, kWatchdogTimer);

    if (g_pBridge)
        ZeroMemory(g_pBridge, sizeof(BridgeData));

    Shell_NotifyIconA(NIM_DELETE, &g_nid);
    DestroyWindow(hWnd);
    UnregisterClassA(kClass, GetModuleHandleA(nullptr));
    BridgeDestroy();
    return true;
}

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    for (int i = 1; i < argc; ++i)
    {
        if (g_sDllPath.empty())
            g_sDllPath = argv[i];
    }
    if (g_sDllPath.empty())
        g_sDllPath = ExeDir() + "dysonbehind.dll";

    fopen_s(&g_pLog, LogFilePath("injector_tray_debug.txt").c_str(), "w");

    printf("convars tray injector\n");
    printf("dll: %s\n", g_sDllPath.c_str());

    if (!injector::IsAdmin())
    {
        LogErr("admin rights required");
        MessageBoxA(nullptr, "Administrator rights are required.", "convars",
                    MB_OK | MB_ICONERROR);
        if (g_pLog) fclose(g_pLog);
        return 1;
    }

    if (GetFileAttributesA(g_sDllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        LogErr("dll not found: %s", g_sDllPath.c_str());
        MessageBoxA(nullptr, "dysonbehind.dll not found next to the executable.", "convars",
                    MB_OK | MB_ICONERROR);
        if (g_pLog) fclose(g_pLog);
        return 1;
    }

    RunTray();

    if (g_pLog) fclose(g_pLog);
    return 0;
}
