#pragma once

#include <Windows.h>

#include "pattern.h"

namespace ManualMapProbe
{
    inline const char* const kTlsPattern = "44 8D ? 09 [5-24] B2 01 48 8B ? 30 E8";

    inline bool LdrpTlsResolvable()
    {
        HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
        if (!hNtdll)
            return false;

        const DWORD64 hit = Pattern::ScanPatternInSection(hNtdll, ".text", kTlsPattern);
        if (!hit)
            return false;

        const DWORD64 window = hit > 0x400 ? 0x400 : hit;
        const DWORD64 padHit = Pattern::ScanBackward(hit - window, window, "CC CC CC");
        if (!padHit)
            return false;

        const BYTE* p    = reinterpret_cast<const BYTE*>(padHit);
        const BYTE* pEnd = p + 64;
        while (p < pEnd && *p == 0xCC)
            ++p;

        return p < pEnd;
    }
}
