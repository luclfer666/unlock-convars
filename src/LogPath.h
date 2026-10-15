#pragma once

#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace LogPath
{
    inline constexpr const char* kDirName = "debug";

    inline bool EnsureDir(const char* szBaseDir)
    {
        char szDir[MAX_PATH]{};
        _snprintf_s(szDir, sizeof(szDir), _TRUNCATE, "%s%s", szBaseDir, kDirName);
        if (CreateDirectoryA(szDir, nullptr))
            return true;
        return GetLastError() == ERROR_ALREADY_EXISTS;
    }

    inline bool Build(char* szOut, size_t nSize, const char* szBaseDir, const char* szName)
    {
        if (!szOut || !nSize || !szBaseDir || !szName || !szBaseDir[0] || !szName[0])
            return false;

        if (EnsureDir(szBaseDir))
        {
            _snprintf_s(szOut, nSize, _TRUNCATE, "%s%s\\%s", szBaseDir, kDirName, szName);
            return true;
        }

        _snprintf_s(szOut, nSize, _TRUNCATE, "%s%s", szBaseDir, szName);
        return false;
    }

    inline void MigrateLegacy(const char* szBaseDir)
    {
        if (!EnsureDir(szBaseDir))
            return;

        char szPattern[MAX_PATH]{};
        _snprintf_s(szPattern, sizeof(szPattern), _TRUNCATE, "%s*_debug.txt", szBaseDir);

        WIN32_FIND_DATAA fd{};
        HANDLE hFind = FindFirstFileA(szPattern, &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            return;

        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            char szOld[MAX_PATH]{};
            char szNew[MAX_PATH]{};
            _snprintf_s(szOld, sizeof(szOld), _TRUNCATE, "%s%s", szBaseDir, fd.cFileName);
            _snprintf_s(szNew, sizeof(szNew), _TRUNCATE, "%s%s\\%s", szBaseDir, kDirName, fd.cFileName);

            if (GetFileAttributesA(szNew) != INVALID_FILE_ATTRIBUTES)
                DeleteFileA(szOld);
            else
                MoveFileA(szOld, szNew);
        } while (FindNextFileA(hFind, &fd));

        FindClose(hFind);
    }

    inline bool DirFromModule(const void* pAddress, char* szOut, size_t nSize)
    {
        char szPath[MAX_PATH]{};
        HMODULE hSelf = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(pAddress), &hSelf))
            return false;

        if (GetModuleFileNameA(hSelf, szPath, MAX_PATH) == 0)
            return false;

        char* pSlash = strrchr(szPath, '\\');
        if (!pSlash)
            return false;
        *(pSlash + 1) = '\0';

        _snprintf_s(szOut, nSize, _TRUNCATE, "%s", szPath);
        return true;
    }

    inline bool DirFromExe(char* szOut, size_t nSize)
    {
        char szPath[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, szPath, MAX_PATH) == 0)
            return false;

        char* pSlash = strrchr(szPath, '\\');
        if (!pSlash)
            return false;
        *(pSlash + 1) = '\0';

        _snprintf_s(szOut, nSize, _TRUNCATE, "%s", szPath);
        return true;
    }
}
