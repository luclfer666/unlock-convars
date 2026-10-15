#include "Inject.h"

#include <TlHelp32.h>
#include <shellapi.h>

#include <cstdio>
#include <fstream>
#include <vector>

#include "manualmap.h"
#include "MapHandoff.h"
#include "ManualMapProbe.h"

#pragma comment(lib, "shell32.lib")

namespace
{
    constexpr DWORD STEAM_APPID        = 1422450;
    constexpr DWORD PROCESS_STALE_SECS = 10;
    constexpr const char* TARGET_PROC  = "deadlock.exe";

    void KillProc(DWORD pid)
    {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h)
        {
            TerminateProcess(h, 0);
            CloseHandle(h);
        }
    }

    DWORD Uptime(DWORD pid)
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h)
            return 0;
        FILETIME fc{}, fe{}, fk{}, fu{};
        DWORD up = 0;
        if (GetProcessTimes(h, &fc, &fe, &fk, &fu))
        {
            FILETIME fn{};
            GetSystemTimeAsFileTime(&fn);
            ULARGE_INTEGER c{}, n{};
            c.LowPart = fc.dwLowDateTime; c.HighPart = fc.dwHighDateTime;
            n.LowPart = fn.dwLowDateTime; n.HighPart = fn.dwHighDateTime;
            up = static_cast<DWORD>((n.QuadPart - c.QuadPart) / 10000000ULL);
        }
        CloseHandle(h);
        return up;
    }
}

namespace injector
{
    bool IsAdmin()
    {
        BOOL ok = FALSE;
        PSID g = nullptr;
        SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
        if (AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                     DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &g))
        {
            CheckTokenMembership(nullptr, g, &ok);
            FreeSid(g);
        }
        return ok != FALSE;
    }

    DWORD FindProcess(const char* szName)
    {
        DWORD pid = 0;
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (h == INVALID_HANDLE_VALUE)
            return 0;
        PROCESSENTRY32 pe{ sizeof(pe) };
        if (Process32First(h, &pe)) do {
            if (_stricmp(pe.szExeFile, szName) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(h, &pe));
        CloseHandle(h);
        return pid;
    }

    bool IsProcessRunning(const char* szName)
    {
        return FindProcess(szName) != 0;
    }

    bool KillProcessByName(const char* szName)
    {
        const DWORD pid = FindProcess(szName);
        if (!pid)
            return false;
        KillProc(pid);
        return true;
    }

    void LaunchGame()
    {
        char url[40]{};
        _snprintf_s(url, sizeof(url), _TRUNCATE, "steam://rungameid/%lu", STEAM_APPID);
        ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
    }

    DWORD WaitForTarget()
    {
        DWORD waited = 0;
        bool launched = false;
        while (true)
        {
            const DWORD pid = FindProcess(TARGET_PROC);
            if (!pid)
            {
                if (!launched)
                {
                    waited += 1000;
                    if (waited >= 2000)
                    {
                        launched = true;
                        LaunchGame();
                    }
                }
                Sleep(1000);
                continue;
            }

            const DWORD up = Uptime(pid);
            if (up == 0 || up <= PROCESS_STALE_SECS)
                return pid;

            KillProc(pid);
            Sleep(1500);
            LaunchGame();
            while (true)
            {
                const DWORD np = FindProcess(TARGET_PROC);
                if (np && (Uptime(np) == 0 || Uptime(np) <= PROCESS_STALE_SECS))
                    return np;
                Sleep(1000);
            }
        }
    }

    static std::string ExeDirLocal()
    {
        char exe[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string dir = exe;
        const auto pos = dir.find_last_of('\\');
        if (pos != std::string::npos)
            dir.resize(pos + 1);
        return dir;
    }

    static void SetErr(char* szErr, size_t nSize, const char* szText)
    {
        if (!szErr || !nSize)
            return;
        _snprintf_s(szErr, nSize, _TRUNCATE, "%s", szText ? szText : "");
    }

    bool Inject(const std::string& sDllPath, DWORD dwPid, char* szErr, size_t nErrSize)
    {
        SetErr(szErr, nErrSize, "");

        std::ifstream file(sDllPath, std::ios::binary | std::ios::ate);
        if (file.fail())
        {
            SetErr(szErr, nErrSize, "cannot open the dll file");
            return false;
        }

        const std::streamoff nFileSize = file.tellg();
        if (nFileSize < 0x1000)
        {
            SetErr(szErr, nErrSize, "dll file is too small");
            return false;
        }

        std::vector<BYTE> data(static_cast<size_t>(nFileSize));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(data.data()), nFileSize);
        file.close();

        if (!ManualMapProbe::LdrpTlsResolvable())
        {
            SetErr(szErr, nErrSize,
                   "ntdll TLS handler not resolvable - manual map cannot register the DLL's "
                   "TLS on this Windows build");
            return false;
        }

        const DWORD acc = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                          PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION;
        HANDLE hProc = OpenProcess(acc, FALSE, dwPid);
        if (!hProc)
        {
            SetErr(szErr, nErrSize, "OpenProcess failed");
            return false;
        }

        MapHandoffData handoff{};
        handoff.nMagic   = MapHandoff::kMagic;
        handoff.nVersion = MapHandoff::kVersion;
        _snprintf_s(handoff.szModuleDir, MAX_PATH, _TRUNCATE, "%s", ExeDirLocal().c_str());

        LPVOID pRemote = VirtualAllocEx(hProc, nullptr, sizeof(MapHandoffData),
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pRemote)
        {
            CloseHandle(hProc);
            SetErr(szErr, nErrSize, "VirtualAllocEx for the handoff failed");
            return false;
        }

        if (!WriteProcessMemory(hProc, pRemote, &handoff, sizeof(handoff), nullptr))
        {
            VirtualFreeEx(hProc, pRemote, 0, MEM_RELEASE);
            CloseHandle(hProc);
            SetErr(szErr, nErrSize, "WriteProcessMemory for the handoff failed");
            return false;
        }

        const bool ok = ManualMapDll(hProc, data.data(),
                                     true,
                                     true,
                                     true,
                                     true,
                                     true,
                                     DLL_PROCESS_ATTACH,
                                     pRemote);

        MapHandoffData acked{};
        const bool bAcked = ReadProcessMemory(hProc, pRemote, &acked, sizeof(acked), nullptr)
                            && acked.nAck == MapHandoff::kAck;

        VirtualFreeEx(hProc, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProc);

        if (!ok)
        {
            SetErr(szErr, nErrSize, "ManualMapDll failed (see injector_tray_debug.txt)");
            return false;
        }

        if (!bAcked)
            SetErr(szErr, nErrSize,
                   "mapped, but the module did not take the handoff - it cannot find its own "
                   "directory, so it will write no logs");

        return true;
    }
}
