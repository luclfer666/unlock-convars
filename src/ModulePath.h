#pragma once

#include <Windows.h>

#include <cstdio>
#include <cstring>

#include "LogPath.h"
#include "MapHandoff.h"

namespace ModulePath
{
    inline char g_szDir[MAX_PATH] = {};
    inline bool g_bFromManualMap  = false;

    inline void NormalizeDir(char* szDir)
    {
        const size_t n = strlen(szDir);
        if (n == 0 || n + 1 >= MAX_PATH)
            return;

        if (szDir[n - 1] != '\\')
        {
            szDir[n]     = '\\';
            szDir[n + 1] = '\0';
        }
    }

    inline void CaptureFromReserved(void* pReserved)
    {
        if (!pReserved)
            return;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(pReserved, &mbi, sizeof(mbi)) != sizeof(mbi))
            return;
        if (mbi.State != MEM_COMMIT || mbi.Type == MEM_IMAGE)
            return;
        if ((mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0)
            return;

        const auto* pData = static_cast<const MapHandoffData*>(pReserved);
        if (pData->nMagic != MapHandoff::kMagic || pData->nVersion != MapHandoff::kVersion)
            return;

        _snprintf_s(g_szDir, MAX_PATH, _TRUNCATE, "%s", pData->szModuleDir);
        NormalizeDir(g_szDir);
        g_bFromManualMap = g_szDir[0] != '\0';

        if (g_bFromManualMap)
            const_cast<MapHandoffData*>(pData)->nAck = MapHandoff::kAck;
    }

    inline bool Has()
    {
        return g_szDir[0] != '\0';
    }

    inline bool IsManualMap()
    {
        return g_bFromManualMap;
    }

    inline bool OwnDir(const void* pFallbackAddress, char* szOut, size_t nSize)
    {
        if (Has())
        {
            _snprintf_s(szOut, nSize, _TRUNCATE, "%s", g_szDir);
            return true;
        }
        return LogPath::DirFromModule(pFallbackAddress, szOut, nSize);
    }
}
