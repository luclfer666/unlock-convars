#pragma once

#include <Windows.h>

#include <string>

namespace injector
{
    bool  IsAdmin();
    DWORD FindProcess(const char* szName);
    bool  IsProcessRunning(const char* szName);
    bool  KillProcessByName(const char* szName);
    void  LaunchGame();

    bool  Inject(const std::string& sDllPath, DWORD dwPid, char* szErr, size_t nErrSize);
    DWORD WaitForTarget();
}
