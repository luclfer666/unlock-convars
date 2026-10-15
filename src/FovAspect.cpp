#include "FovAspect.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <Windows.h>

#include "DevLog.h"

namespace
{

    void* ScanCode(HMODULE hMod, const char* szPattern, size_t nPatLen, const bool* mask)
    {
        if (!hMod || !szPattern || !mask)
            return nullptr;

        const uintptr_t kBase = reinterpret_cast<uintptr_t>(hMod);
        auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
        if (pDos->e_magic != IMAGE_DOS_SIGNATURE)
            return nullptr;
        auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(kBase + pDos->e_lfanew);
        if (pNt->Signature != IMAGE_NT_SIGNATURE)
            return nullptr;

        const uintptr_t uStart = kBase + pNt->OptionalHeader.BaseOfCode;
        const uintptr_t uEnd   = uStart + pNt->OptionalHeader.SizeOfCode;

        for (uintptr_t u = uStart; u + nPatLen <= uEnd; ++u)
        {
            bool ok = true;
            for (size_t i = 0; i < nPatLen; ++i)
            {
                if (mask[i] && *reinterpret_cast<const uint8_t*>(u + i) !=
                                static_cast<uint8_t>(szPattern[i]))
                {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return reinterpret_cast<void*>(u);
        }
        return nullptr;
    }

    size_t BuildMask(const char* szPattern, uint8_t* outBytes, bool* outMask, size_t nCap)
    {
        size_t nLen = 0;
        const char* p = szPattern;
        while (*p && nLen < nCap)
        {
            if (*p == ' ') { ++p; continue; }
            if (*p == '?')
            {
                outBytes[nLen] = 0;
                outMask[nLen] = false;
                ++nLen;
                while (*p == '?') ++p;
                continue;
            }
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            const int hi = hex(p[0]);
            const int lo = p[1] ? hex(p[1]) : -1;
            if (hi < 0 || lo < 0)
                break;
            outBytes[nLen] = static_cast<uint8_t>((hi << 4) | lo);
            outMask[nLen] = true;
            ++nLen;
            p += 2;
        }
        return nLen;
    }

    size_t FindStringInModule(HMODULE hMod, const char* szStr,
                              uintptr_t* outList, size_t nMax)
    {
        if (!hMod)
            return 0;

        const uintptr_t kBase = reinterpret_cast<uintptr_t>(hMod);
        auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
        auto* pNt  = reinterpret_cast<IMAGE_NT_HEADERS*>(kBase + pDos->e_lfanew);
        auto* pSec = IMAGE_FIRST_SECTION(pNt);

        const size_t nName = strlen(szStr);
        size_t nFound = 0;

        for (int s = 0; s < pNt->FileHeader.NumberOfSections && nFound < nMax; ++s)
        {
            const auto* pS = &pSec[s];
            const uint8_t* p = reinterpret_cast<const uint8_t*>(kBase + pS->VirtualAddress);
            const size_t   n = pS->Misc.VirtualSize;
            if (n < nName + 1)
                continue;

            for (size_t i = 0; i + nName + 1 <= n; ++i)
            {
                if (p[i + nName] != 0x00 || p[i + nName - 1] != szStr[nName - 1])
                    continue;
                if (memcmp(p + i, szStr, nName) != 0)
                    continue;
                if (i > 0 && p[i - 1] != 0x00)
                    continue;

                outList[nFound++] = reinterpret_cast<uintptr_t>(p + i);
            }
        }
        return nFound;
    }

    constexpr const char* kEngineToClientVersion = "Source2EngineToClient001";

    constexpr const char* kIsInGamePat =
        "48 8B ? ? ? ? ? 48 85 C0 74 15 80 B8 ? ? ? ? ? 75 0C 83 B8 ? ? ? ? 06";
    constexpr const char* kExecuteClientCmdPat =
        "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 56 41 57 48 81 EC ? ? ? ? "
        "0F 29 70 ? 41 0F B6 E9";

    void*   g_pEngineToClient = nullptr;
    void*   g_pfnExecuteClientCmd = nullptr;

    using IsInGame_t = bool(__fastcall*)(void*);
    IsInGame_t g_pfnIsInGame = nullptr;

    template <typename T>
    T CaptureInterface(void* pfnFactory, const char* szVersion)
    {
        using CreateInterfaceFn = T(*)(const char*, int*);
        return reinterpret_cast<CreateInterfaceFn>(pfnFactory)(szVersion, nullptr);
    }

    void* GetFactory(HMODULE hMod)
    {
        if (!hMod)
            return nullptr;
        return reinterpret_cast<void*>(GetProcAddress(hMod, "CreateInterface"));
    }

    void* ResolveEngineFn(const char* szPattern)
    {
        HMODULE hEngine = GetModuleHandleA("engine2.dll");
        if (!hEngine)
            return nullptr;

        uint8_t bytes[128] = {};
        bool    mask[128] = {};
        const size_t nLen = BuildMask(szPattern, bytes, mask, 128);
        if (!nLen)
            return nullptr;

        return ScanCode(hEngine, reinterpret_cast<const char*>(bytes), nLen, mask);
    }

    bool EnsureEngine()
    {
        if (!g_pEngineToClient)
        {
            HMODULE hEngine = GetModuleHandleA("engine2.dll");
            void* pfnFactory = GetFactory(hEngine);
            if (!pfnFactory)
                return false;
            g_pEngineToClient = CaptureInterface<void*>(pfnFactory, kEngineToClientVersion);
            if (!g_pEngineToClient)
            {
                DEV_LOG("[FOV] engine2: no '%s' interface\n", kEngineToClientVersion);
                return false;
            }
        }

        if (!g_pfnExecuteClientCmd)
        {
            g_pfnExecuteClientCmd = ResolveEngineFn(kExecuteClientCmdPat);
            if (g_pfnExecuteClientCmd)
                DEV_LOG("[+] FOV: ExecuteClientCmd resolved @ %p\n", g_pfnExecuteClientCmd);
            else
                DEV_WARN("[FOV] ExecuteClientCmd NOT FOUND (fallback: sv_cheats-style path only)\n");
        }

        return g_pEngineToClient != nullptr && g_pfnExecuteClientCmd != nullptr;
    }

    void ExecuteClientCmd(const char* szCmd)
    {
        if (!szCmd || !EnsureEngine())
            return;
        using Fn = void(__fastcall*)(void*, uint32_t, const char*, bool);
        reinterpret_cast<Fn>(g_pfnExecuteClientCmd)(g_pEngineToClient, 0, szCmd, true);
    }

    struct FovConVar
    {
        float* pflValue  = nullptr;
        float* pflMin    = nullptr;
        float* pflMax    = nullptr;
        float  flOrigMin = 0.f;
        float  flOrigMax = 0.f;
        bool   bPatched  = false;
    };

    bool ResolveConVarByName(const char* szName, FovConVar& out)
    {
        out = FovConVar{};

        HMODULE hClient = GetModuleHandleA("client.dll");
        if (!hClient)
        {
            DEV_LOG("[FOV] '%s': client.dll not loaded\n", szName);
            return false;
        }

        const uintptr_t kBase = reinterpret_cast<uintptr_t>(hClient);
        auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(hClient);
        auto* pNt  = reinterpret_cast<IMAGE_NT_HEADERS*>(kBase + pDos->e_lfanew);
        auto* pSec = IMAGE_FIRST_SECTION(pNt);

        uintptr_t kStrList[8] = {};
        const size_t nStrCount = FindStringInModule(hClient, szName, kStrList, 8);
        if (!nStrCount)
        {
            DEV_LOG("[FOV] '%s': string NOT found in any section\n", szName);
            return false;
        }

        bool bFoundNameLea = false;
        for (int s = 0; s < pNt->FileHeader.NumberOfSections; ++s)
        {
            const auto* pS = &pSec[s];
            if (!(pS->Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;

            const uint8_t* p = reinterpret_cast<const uint8_t*>(kBase + pS->VirtualAddress);
            const size_t   n = pS->Misc.VirtualSize;

            for (size_t i = 0; i + 40 <= n; ++i)
            {

                if (p[i] != 0x48 || p[i + 1] != 0x8D || p[i + 2] != 0x15)
                    continue;

                const uintptr_t kLea  = reinterpret_cast<uintptr_t>(p + i);
                const int32_t   nDisp = *reinterpret_cast<const int32_t*>(p + i + 3);
                const uintptr_t kTarget = kLea + 7 + nDisp;

                bool bName = false;
                for (size_t k = 0; k < nStrCount; ++k)
                    if (kTarget == kStrList[k]) { bName = true; break; }
                if (!bName)
                    continue;

                bFoundNameLea = true;

                for (int j = 7; j <= 32; ++j)
                {

                    if (i + j + 7 > n) break;
                    if (p[i + j] != 0x48 || p[i + j + 1] != 0x8D || p[i + j + 2] != 0x0D)
                        continue;

                    const uintptr_t kLea2   = reinterpret_cast<uintptr_t>(p + i + j);
                    const int32_t   nDisp2  = *reinterpret_cast<const int32_t*>(p + i + j + 3);
                    const uintptr_t kMirror = kLea2 + 7 + nDisp2;

                    const uintptr_t nState = *reinterpret_cast<uintptr_t*>(kMirror + 0x08);
                    if (!nState)
                        continue;

                    float* pflValue = reinterpret_cast<float*>(*reinterpret_cast<uintptr_t*>(nState + 0x08));
                    float* pflMin   = reinterpret_cast<float*>(*reinterpret_cast<uintptr_t*>(nState + 0x10));
                    float* pflMax   = reinterpret_cast<float*>(*reinterpret_cast<uintptr_t*>(nState + 0x18));

                    if (pflValue && pflMin && pflMax)
                    {
                        out.pflValue = pflValue;
                        out.pflMin   = pflMin;
                        out.pflMax   = pflMax;
                        DEV_LOG("[+] FOV: '%s' resolved (value=%.1f min=%.1f max=%.1f)\n",
                            szName, *pflValue, *pflMin, *pflMax);
                        return true;
                    }
                }
            }
        }

        DEV_LOG("[FOV] '%s': FAILED (%s)\n", szName,
            bFoundNameLea ? "name lea found, no valid mirror" : "no lea rdx -> string");
        return false;
    }

    void FovPatchBounds(FovConVar& cv, float flLo, float flHi)
    {
        if (!cv.pflValue || cv.bPatched)
            return;
        cv.flOrigMin = *cv.pflMin;
        cv.flOrigMax = *cv.pflMax;
        *cv.pflMin = flLo;
        *cv.pflMax = flHi;
        cv.bPatched = true;
    }

    void FovRestoreBounds(FovConVar& cv)
    {
        if (!cv.bPatched)
            return;
        *cv.pflMin = cv.flOrigMin;
        *cv.pflMax = cv.flOrigMax;
        cv.bPatched = false;
    }

    struct GameInfoDefaults
    {
        bool  bParsed    = false;
        bool  bHasHero   = false;
        bool  bHasDesired = false;
        bool  bHasAspect = false;
        float flHero     = 0.f;
        float flDesired  = 0.f;
        float flAspect   = 0.f;
    };

    float GameInfoLineValue(const std::string& sLine, const char* szName)
    {
        const size_t nName = strlen(szName);
        const size_t pos = sLine.find_first_not_of(" \t");
        if (pos == std::string::npos || sLine.compare(pos, nName, szName) != 0)
            return FLT_MAX;

        const size_t q1 = sLine.find('"', pos + nName);
        if (q1 == std::string::npos)
            return FLT_MAX;
        const size_t q2 = sLine.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 == q1 + 1)
            return FLT_MAX;

        return strtof(sLine.c_str() + q1 + 1, nullptr);
    }

    const GameInfoDefaults& GetGameInfoDefaults()
    {
        static GameInfoDefaults s_Def;
        if (s_Def.bParsed)
            return s_Def;
        s_Def.bParsed = true;

        char szModule[MAX_PATH] = {};
        HMODULE hClient = GetModuleHandleA("client.dll");
        if (!hClient || !GetModuleFileNameA(hClient, szModule, MAX_PATH))
        {
            DEV_LOG("[+] FOV Changer: gameinfo — client.dll path failed\n");
            return s_Def;
        }

        std::error_code ec;
        std::filesystem::path fsDir = std::filesystem::path(szModule).parent_path();
        std::filesystem::path fsGameInfo;
        for (int i = 0; i < 8 && fsDir != fsDir.root_path(); ++i)
        {
            std::filesystem::path fsTry = fsDir / "gameinfo.gi";
            if (std::filesystem::exists(fsTry, ec))
            {
                fsGameInfo = fsTry;
                break;
            }
            fsDir = fsDir.parent_path();
        }

        if (fsGameInfo.empty())
        {
            DEV_LOG("[+] FOV Changer: gameinfo not found near '%s'\n", szModule);
            return s_Def;
        }

        std::ifstream file(fsGameInfo);
        if (!file.is_open())
        {
            DEV_LOG("[+] FOV Changer: gameinfo open failed '%s'\n", fsGameInfo.string().c_str());
            return s_Def;
        }

        std::string sLine;
        while (std::getline(file, sLine))
        {
            if (!sLine.empty() && sLine.back() == '\r')
                sLine.pop_back();

            float v;
            if ((v = GameInfoLineValue(sLine, "citadel_camera_hero_fov")) != FLT_MAX)
            {
                s_Def.bHasHero = true;
                s_Def.flHero = v;
            }
            else if ((v = GameInfoLineValue(sLine, "fov_desired")) != FLT_MAX)
            {
                s_Def.bHasDesired = true;
                s_Def.flDesired = v;
            }
            else if ((v = GameInfoLineValue(sLine, "r_aspectratio")) != FLT_MAX)
            {
                s_Def.bHasAspect = true;
                s_Def.flAspect = v;
            }
        }

        DEV_LOG("[+] FOV Changer: gameinfo '%s': hero_fov=%s%.1f fov_desired=%s%.1f aspect=%s%.2f\n",
            fsGameInfo.string().c_str(),
            s_Def.bHasHero ? "" : "def:", s_Def.bHasHero ? s_Def.flHero : 90.f,
            s_Def.bHasDesired ? "" : "def:", s_Def.bHasDesired ? s_Def.flDesired : 75.f,
            s_Def.bHasAspect ? "" : "native:", s_Def.bHasAspect ? s_Def.flAspect : 0.f);
        return s_Def;
    }

    constexpr float kDefaultDesiredFov = 75.f;
    constexpr float kDefaultHeroFov    = 90.f;

    float* g_pflAspectValue = nullptr;

    void ResolveAspect()
    {
        HMODULE hEngine = GetModuleHandleA("engine2.dll");
        if (!hEngine)
        {
            DEV_LOG("[+] FOV Changer: engine2.dll not loaded — aspect off\n");
            return;
        }

        constexpr const char* kAspectPat =
            "48 83 EC 28 48 8D 0D ? ? ? ? E8 ? ? ? ? 0F 57 C9 0F 2F C1 76 ? "
            "48 8D 0D ? ? ? ? 48 83 C4 28 E9";

        uint8_t bytes[128] = {};
        bool    mask[128] = {};
        const size_t nLen = BuildMask(kAspectPat, bytes, mask, 128);
        if (!nLen)
            return;

        void* pMatch = ScanCode(hEngine, reinterpret_cast<const char*>(bytes), nLen, mask);
        if (!pMatch)
        {
            DEV_LOG("[+] FOV Changer: r_aspectratio NOT FOUND (aspect off)\n");
            return;
        }

        const uintptr_t kMatch = reinterpret_cast<uintptr_t>(pMatch);
        const uintptr_t kObj1 = kMatch + 11 + *reinterpret_cast<int32_t*>(kMatch + 7);
        const uintptr_t kObj2 = kMatch + 31 + *reinterpret_cast<int32_t*>(kMatch + 27);
        const uintptr_t kGet1 = kMatch + 16 + *reinterpret_cast<int32_t*>(kMatch + 12);
        const uintptr_t kGet2 = kMatch + 40 + *reinterpret_cast<int32_t*>(kMatch + 36);

        if (kObj1 != kObj2 || kGet1 != kGet2)
        {
            DEV_LOG("[+] FOV Changer: r_aspectratio self-check failed\n");
            return;
        }

        const uintptr_t nState = *reinterpret_cast<uintptr_t*>(kObj1 + 8);
        if (nState)
            g_pflAspectValue = reinterpret_cast<float*>(nState + 0x58);

        DEV_LOG("[+] FOV Changer: r_aspectratio %s\n",
            g_pflAspectValue ? "resolved" : "NOT FOUND (aspect off)");
    }

    bool  s_bResolved       = false;
    bool  s_bAspectResolved = false;
    FovConVar s_HeroFov;
    FovConVar s_DesiredFov;

    bool  s_bApplied   = false;
    int   s_nApplied   = 0;
    bool  s_bWasInGame = false;

    void ApplyInternal(bool bInGame)
    {
        if (!s_bResolved)
        {
            s_bResolved = true;
            ResolveConVarByName("citadel_camera_hero_fov", s_HeroFov);
            ResolveConVarByName("fov_desired", s_DesiredFov);
        }

        if (!s_bAspectResolved)
        {
            s_bAspectResolved = true;
            ResolveAspect();
        }

        const bool bReapply = bInGame && !s_bWasInGame;
        s_bWasInGame = bInGame;

        if (!bInGame)
        {
            if (s_bApplied)
            {
                FovRestoreBounds(s_HeroFov);
                FovRestoreBounds(s_DesiredFov);
            }
            s_bApplied = false;
            return;
        }

        const bool bOn = FovAspect::bFovEnabled;

        {
            const GameInfoDefaults& gi = GetGameInfoDefaults();
            const float flAspect = bOn ? FovAspect::flAspectTest
                                       : (gi.bHasAspect ? gi.flAspect : 0.f);
            if (g_pflAspectValue && fabsf(*g_pflAspectValue - flAspect) >= 0.005f)
                *g_pflAspectValue = flAspect;
        }

        if (!bOn)
        {
            if (s_bApplied)
            {
                FovRestoreBounds(s_HeroFov);
                FovRestoreBounds(s_DesiredFov);

                const GameInfoDefaults& gi = GetGameInfoDefaults();
                const float flDesired = gi.bHasDesired ? gi.flDesired : kDefaultDesiredFov;
                const float flHero    = gi.bHasHero    ? gi.flHero    : kDefaultHeroFov;

                char szCmd[48];
                std::snprintf(szCmd, sizeof(szCmd), "fov_desired %d",
                    static_cast<int>(lroundf(flDesired)));
                ExecuteClientCmd(szCmd);
                std::snprintf(szCmd, sizeof(szCmd), "citadel_camera_hero_fov %d",
                    static_cast<int>(lroundf(flHero)));
                ExecuteClientCmd(szCmd);

                if (s_DesiredFov.pflValue)
                    *s_DesiredFov.pflValue = flDesired;
                if (s_HeroFov.pflValue)
                    *s_HeroFov.pflValue = flHero;

                DEV_LOG("[+] FOV Changer: off -> fov_desired %.1f (%s) / hero_fov %.1f (%s) / aspect native\n",
                    flDesired, gi.bHasDesired ? "gameinfo" : "engine default",
                    flHero,    gi.bHasHero    ? "gameinfo" : "engine default");

                s_bApplied = false;
                s_nApplied = 0;
            }
            return;
        }

        const int nTarget = static_cast<int>(lroundf(FovAspect::flFovValue));

        if (s_HeroFov.pflValue && fabsf(*s_HeroFov.pflValue - FovAspect::flFovValue) >= 0.01f)
            *s_HeroFov.pflValue = FovAspect::flFovValue;
        if (s_DesiredFov.pflValue && fabsf(*s_DesiredFov.pflValue - FovAspect::flFovValue) >= 0.01f)
            *s_DesiredFov.pflValue = FovAspect::flFovValue;

        if (s_bApplied && !bReapply && nTarget == s_nApplied)
            return;

        FovPatchBounds(s_HeroFov, 40.f, 200.f);
        FovPatchBounds(s_DesiredFov, 40.f, 200.f);

        char szCmd[48];
        std::snprintf(szCmd, sizeof(szCmd), "fov_desired %d", nTarget);
        ExecuteClientCmd(szCmd);
        std::snprintf(szCmd, sizeof(szCmd), "citadel_camera_hero_fov %d", nTarget);
        ExecuteClientCmd(szCmd);

        if (!s_bApplied)
            DEV_LOG("[+] FOV Changer: on -> fov_desired %d / hero_fov %d\n", nTarget, nTarget);

        s_bApplied = true;
        s_nApplied = nTarget;
    }
}

namespace FovAspect
{
    void Tick(bool bInGame)
    {
        ApplyInternal(bInGame);
    }

    void Reset()
    {
        s_bApplied = false;
        s_nApplied = 0;
        if (s_bResolved)
        {
            FovRestoreBounds(s_HeroFov);
            FovRestoreBounds(s_DesiredFov);
        }
        if (g_pflAspectValue && *g_pflAspectValue != 0.f)
            *g_pflAspectValue = 0.f;
        bFovEnabled = false;
    }
}

namespace
{

    struct BoolConVar
    {
        uint8_t** ppCached  = nullptr;
        uint8_t*  pbValue   = nullptr;
        uint8_t   bOrig     = 0;
        bool      bOrigSet  = false;
        bool      bResolved = false;
    };

    uint8_t** CachedSlotFromSite(uintptr_t kSite)
    {
        const int32_t nDisp = *reinterpret_cast<const int32_t*>(kSite + 3);
        return reinterpret_cast<uint8_t**>(kSite + 7 + nDisp);
    }

    uintptr_t FindStringInExec(uintptr_t kBase, const IMAGE_NT_HEADERS* pNt, const char* szStr)
    {
        const auto* pSec = IMAGE_FIRST_SECTION(pNt);
        const size_t nName = strlen(szStr);
        for (int s = 0; s < pNt->FileHeader.NumberOfSections; ++s)
        {
            const auto* pS = &pSec[s];
            const uint8_t* p = reinterpret_cast<const uint8_t*>(kBase + pS->VirtualAddress);
            const size_t   n = pS->Misc.VirtualSize;
            if (n < nName + 1)
                continue;
            for (size_t i = 0; i + nName + 1 <= n; ++i)
            {
                if (p[i + nName] != 0 || memcmp(p + i, szStr, nName) != 0)
                    continue;
                if (i > 0 && p[i - 1] != 0)
                    continue;
                return reinterpret_cast<uintptr_t>(p + i);
            }
        }
        return 0;
    }

    bool CachedSlotMatchesName(uint8_t** ppCached, const char* szName)
    {
        HMODULE hEngine = GetModuleHandleA("engine2.dll");
        if (!hEngine || !ppCached || !szName)
            return false;

        const uintptr_t kBase = reinterpret_cast<uintptr_t>(hEngine);
        auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(hEngine);
        auto* pNt  = reinterpret_cast<IMAGE_NT_HEADERS*>(kBase + pDos->e_lfanew);

        const uintptr_t kStr = FindStringInExec(kBase, pNt, szName);
        if (!kStr)
            return false;

        const auto* pSec = IMAGE_FIRST_SECTION(pNt);
        for (int s = 0; s < pNt->FileHeader.NumberOfSections; ++s)
        {
            const auto* pS = &pSec[s];
            if (!(pS->Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;

            const uint8_t* p = reinterpret_cast<const uint8_t*>(kBase + pS->VirtualAddress);
            const size_t   n = pS->Misc.VirtualSize;

            for (size_t i = 0; i + 40 <= n; ++i)
            {
                if (p[i] != 0x48 || p[i + 1] != 0x8D || p[i + 2] != 0x15)
                    continue;

                const uintptr_t kLea  = reinterpret_cast<uintptr_t>(p + i);
                const int32_t   nDisp = *reinterpret_cast<const int32_t*>(p + i + 3);
                if (kLea + 7 + nDisp != kStr)
                    continue;

                for (int j = 3; j <= 64; ++j)
                {
                    if (i + j + 7 > n)
                        break;
                    if (p[i + j] != 0x48 || p[i + j + 1] != 0x8D || p[i + j + 2] != 0x0D)
                        continue;

                    const uintptr_t kLea2  = reinterpret_cast<uintptr_t>(p + i + j);
                    const int32_t   nDisp2 = *reinterpret_cast<const int32_t*>(p + i + j + 3);
                    const uintptr_t kObj   = kLea2 + 7 + nDisp2;

                    if (reinterpret_cast<uint8_t**>(kObj + 8) == ppCached)
                        return true;
                }
            }
        }
        return false;
    }

    void ResolveBoolConVar(BoolConVar& cv, const char* szSitePat, const char* szName,
                           const char* szLogTag)
    {
        HMODULE hEngine = GetModuleHandleA("engine2.dll");
        if (!hEngine)
            return;

        uint8_t bytes[128] = {};
        bool    mask[128] = {};
        const size_t nLen = BuildMask(szSitePat, bytes, mask, 128);
        if (!nLen)
            return;

        void* pSite = ScanCode(hEngine, reinterpret_cast<const char*>(bytes), nLen, mask);
        if (!pSite)
        {
            DEV_LOG("[%s] site NOT FOUND — pattern stale after game patch\n", szLogTag);
            return;
        }

        uint8_t** ppCached = CachedSlotFromSite(reinterpret_cast<uintptr_t>(pSite));

        if (!CachedSlotMatchesName(ppCached, szName))
        {
            DEV_WARN("[%s] site found @ %p but it is NOT '%s' (registration "
                     "mismatch) — disabled\n", szLogTag, pSite, szName);
            return;
        }

        const uintptr_t nCached = *reinterpret_cast<uintptr_t*>(ppCached);
        if (!nCached)
            return;

        cv.ppCached  = ppCached;
        cv.pbValue   = reinterpret_cast<uint8_t*>(nCached + 0x58);
        cv.bResolved = true;
        cv.bOrigSet  = false;

        DEV_LOG("[%s] '%s' value @ %p (now=%u)\n", szLogTag, szName, cv.pbValue, *cv.pbValue);
    }

    void TickBoolConVar(BoolConVar& cv, bool bForce, const char* szSitePat,
                        const char* szName, const char* szLogTag)
    {
        if (!cv.bResolved)
        {
            ResolveBoolConVar(cv, szSitePat, szName, szLogTag);
            return;
        }

        const uintptr_t nCached = *reinterpret_cast<uintptr_t*>(cv.ppCached);
        if (!nCached)
            return;
        cv.pbValue = reinterpret_cast<uint8_t*>(nCached + 0x58);

        if (!bForce)
            return;

        if (!cv.bOrigSet)
        {
            cv.bOrig    = *cv.pbValue;
            cv.bOrigSet = true;
            DEV_LOG("[%s] '%s' snapshot on enable: %u\n", szLogTag, szName, cv.bOrig);
        }

        if (*cv.pbValue != 0)
            *cv.pbValue = 0;
    }

    void ResetBoolConVar(BoolConVar& cv)
    {
        if (!cv.bResolved)
            return;

        const uintptr_t nCached = *reinterpret_cast<uintptr_t*>(cv.ppCached);
        if (nCached)
            cv.pbValue = reinterpret_cast<uint8_t*>(nCached + 0x58);

        if (cv.pbValue && cv.bOrigSet)
            *cv.pbValue = cv.bOrig;

        cv.bOrigSet = false;
    }

    constexpr const char* kDirectLightingSitePat =
        "48 8B 05 ? ? ? ? "
        "83 FA 01 "
        "BA 45 0C 90 1F "
        "45 8B C5 "
        "74 ? "
        "80 78 58 00 "
        "41 0F 95 C0 "
        "E8 ? ? ? ?";

    constexpr const char* kSkyboxSitePat =
        "48 8B 05 ? ? ? ? "
        "48 8D 8E ? ? ? ? "
        "45 8B C5 "
        "BA 95 45 7B 98 "
        "80 78 58 00 "
        "41 0F 95 C0 "
        "E8 ? ? ? ?";

    BoolConVar g_DirectLighting;
    BoolConVar g_Skybox;
}

namespace DirectLighting
{
    void Tick()
    {
        TickBoolConVar(g_DirectLighting, bDisabled, kDirectLightingSitePat,
                       "r_directlighting", "DirectLight");
    }

    void Reset()
    {
        ResetBoolConVar(g_DirectLighting);
        bDisabled = false;
    }
}

namespace Skybox
{
    void Tick()
    {
        TickBoolConVar(g_Skybox, bHidden, kSkyboxSitePat,
                       "r_drawskybox", "Skybox");
    }

    void Reset()
    {
        ResetBoolConVar(g_Skybox);
        bHidden = false;
    }
}
