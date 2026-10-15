#include "TrayBridge.h"

#include <cmath>

#include <Windows.h>

#include "DevLog.h"
#include "FovAspect.h"
#include "UISound.h"

namespace
{
    HANDLE           g_hMap     = nullptr;
    BridgeData*      g_pBridge  = nullptr;
    bool             g_bAttached = false;
    bool             g_bLogged  = false;
    uint64_t         g_ullSeen  = 0;

    const char* const kFovName = "fov";
    const char* const kLightName = "directlighting";
    const char* const kSkyName = "skybox";

    bool HeartbeatAlive()
    {
        return static_cast<uint32_t>(GetTickCount64() - g_pBridge->nWriterAlive) < 5000u;
    }

    void Attach()
    {
        g_hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, kBridgeName);
        if (!g_hMap)
            return;

        g_pBridge = static_cast<BridgeData*>(
            MapViewOfFile(g_hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BridgeData)));
        if (!g_pBridge)
        {
            CloseHandle(g_hMap);
            g_hMap = nullptr;
            return;
        }

        if (g_pBridge->version != kBridgeVer)
        {
            DEV_WARN("TrayBridge: version mismatch (%d != %d) — tray control disabled\n",
                     g_pBridge->version, kBridgeVer);
            UnmapViewOfFile(g_pBridge);
            CloseHandle(g_hMap);
            g_pBridge = nullptr;
            g_hMap = nullptr;
            return;
        }

        g_bAttached = true;
        if (g_pBridge->bTrayMode)
            MenuControl::SetMenuOpen(false);
        DEV_LOG("[+] TrayBridge: attached (tray mode %d)\n", g_pBridge->bTrayMode ? 1 : 0);
    }

    void Apply()
    {
        const bool bFov = g_pBridge->bFovEnabled != 0;
        if (bFov != FovAspect::bFovEnabled)
        {
            FovAspect::bFovEnabled = bFov;
            if (!bFov)
                FovAspect::Reset();
            PlayUISound(bFov ? UISound::ToggleOn : UISound::ToggleOff);
            DEV_LOG("TrayBridge: %s %s\n", kFovName, bFov ? "ON" : "OFF");
        }

        const float flFov = static_cast<float>(g_pBridge->nFovValue);
        if (flFov >= 40.f && flFov <= 165.f && fabsf(FovAspect::flFovValue - flFov) >= 0.5f)
            FovAspect::flFovValue = flFov;

        const float flAspect = static_cast<float>(g_pBridge->nAspect) / 100.f;
        if (flAspect >= 0.5f && flAspect <= 3.f && fabsf(FovAspect::flAspectTest - flAspect) >= 0.005f)
        {
            FovAspect::flAspectTest = flAspect;
            FovAspect::bAspectCustom = true;
        }

        const bool bLight = g_pBridge->bDirectLightingOff != 0;
        if (bLight != DirectLighting::bDisabled)
        {
            DirectLighting::bDisabled = bLight;
            if (!bLight)
                DirectLighting::Reset();
            PlayUISound(bLight ? UISound::ToggleOff : UISound::ToggleOn);
            DEV_LOG("TrayBridge: %s %s\n", kLightName, bLight ? "ON" : "OFF");
        }

        const bool bSky = g_pBridge->bSkyboxOff != 0;
        if (bSky != Skybox::bHidden)
        {
            Skybox::bHidden = bSky;
            if (!bSky)
                Skybox::Reset();
            PlayUISound(bSky ? UISound::ToggleOff : UISound::ToggleOn);
            DEV_LOG("TrayBridge: %s %s\n", kSkyName, bSky ? "ON" : "OFF");
        }

        g_ullSeen = GetTickCount64();
        g_pBridge->nFovApplied      = static_cast<int32_t>(lroundf(FovAspect::flFovValue));
        g_pBridge->nAspectApplied   = static_cast<int32_t>(lroundf(FovAspect::flAspectTest * 100.f));
    }
}

namespace TrayBridge
{
    void Tick()
    {
        if (!g_bAttached)
        {
            static uint64_t s_ullLastTry = 0;
            const uint64_t ullNow = GetTickCount64();
            if (s_ullLastTry && ullNow - s_ullLastTry < 3000u)
                return;
            s_ullLastTry = ullNow;
            Attach();
            if (!g_bAttached)
                return;
        }

        if (!HeartbeatAlive())
        {
            if (g_ullSeen && !g_bLogged)
            {
                g_bLogged = true;
                DEV_LOG("[+] TrayBridge: tray process gone — control released\n");
            }
            return;
        }

        if (g_pBridge->version != kBridgeVer)
            return;

        Apply();
    }

    bool IsActive()
    {
        return g_bAttached && g_pBridge && HeartbeatAlive();
    }

    bool IsTrayMode()
    {
        return IsActive() && g_pBridge->bTrayMode != 0;
    }

    void Reset()
    {
        FovAspect::bFovEnabled   = false;
        DirectLighting::bDisabled = false;
        Skybox::bHidden          = false;
        FovAspect::Reset();
        DirectLighting::Reset();
        Skybox::Reset();
    }
}
