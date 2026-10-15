#include "TrayMenu.h"

#include <cstdio>

#include <vector>

using tray_menu::kResultExitKillGame;
using tray_menu::kResultNone;

namespace
{
    constexpr int kIdStatus     = 10;
    constexpr int kIdExitKill   = 12;

    constexpr int kIdFovEnable  = 200;
    constexpr int kIdFovBase    = 210;

    constexpr int kIdAspectBase = 260;

    constexpr int kIdLight      = 300;
    constexpr int kIdSky        = 301;

    const int kFovPresets[] = { 90, 100, 105, 110, 115, 120, 125, 130, 135, 140, 150, 165 };
    constexpr int kFovPresetCount = static_cast<int>(sizeof(kFovPresets) / sizeof(kFovPresets[0]));

    const int kAspectPresets[] = { 0, 133, 150, 160, 178, 200, 235, 300 };
    constexpr int kAspectPresetCount = static_cast<int>(sizeof(kAspectPresets) / sizeof(kAspectPresets[0]));

    char g_szStatus[96] = "starting injection...";

    std::vector<int> g_fovItems;
    std::vector<int> g_aspectItems;

    UINT Checked(bool bOn)
    {
        return bOn ? MF_CHECKED : MF_UNCHECKED;
    }

    void Item(HMENU hMenu, UINT uFlags, UINT_PTR uId, const char* szText)
    {
        AppendMenuA(hMenu, uFlags | MF_STRING, uId, szText);
    }

    void BuildFovItems(int nCurrent)
    {
        g_fovItems.clear();
        bool bInserted = false;
        for (int i = 0; i < kFovPresetCount; ++i)
        {
            if (!bInserted && nCurrent < kFovPresets[i])
            {
                g_fovItems.push_back(nCurrent);
                bInserted = true;
            }
            g_fovItems.push_back(kFovPresets[i]);
        }
        if (!bInserted && nCurrent > kFovPresets[kFovPresetCount - 1])
            g_fovItems.push_back(nCurrent);
    }

    void BuildAspectItems(int nCurrent)
    {
        g_aspectItems.clear();
        bool bInserted = false;
        for (int i = 0; i < kAspectPresetCount; ++i)
        {
            if (!bInserted && nCurrent < kAspectPresets[i])
            {
                g_aspectItems.push_back(nCurrent);
                bInserted = true;
            }
            g_aspectItems.push_back(kAspectPresets[i]);
        }
        if (!bInserted && nCurrent > kAspectPresets[kAspectPresetCount - 1])
            g_aspectItems.push_back(nCurrent);
    }

    void AspectLabel(int nAspect, char* szOut, size_t nSize)
    {
        if (nAspect <= 0)
            snprintf(szOut, nSize, "Native");
        else
            snprintf(szOut, nSize, "%.2f", static_cast<float>(nAspect) / 100.f);
    }

    HMENU Build(BridgeData* b)
    {
        BuildFovItems(b->nFovValue);
        BuildAspectItems(b->nAspect);

        HMENU hRoot = CreatePopupMenu();
        if (!hRoot)
            return nullptr;

        Item(hRoot, MF_GRAYED, kIdStatus, g_szStatus);
        AppendMenuA(hRoot, MF_SEPARATOR, 0, nullptr);

        HMENU hFov = CreatePopupMenu();
        Item(hFov, Checked(b->bFovEnabled != 0), kIdFovEnable, "Enabled");
        AppendMenuA(hFov, MF_SEPARATOR, 0, nullptr);

        HMENU hFovVal = CreatePopupMenu();
        for (int i = 0; i < static_cast<int>(g_fovItems.size()); ++i)
        {
            char szNum[24];
            snprintf(szNum, sizeof(szNum), "%d", g_fovItems[i]);
            Item(hFovVal, MFT_RADIOCHECK | Checked(g_fovItems[i] == b->nFovValue),
                 kIdFovBase + i, szNum);
        }
        {
            char szLabel[64];
            snprintf(szLabel, sizeof(szLabel), "Field of view  (%d)", b->nFovValue);
            AppendMenuA(hFov, MF_POPUP, reinterpret_cast<UINT_PTR>(hFovVal), szLabel);
        }

        HMENU hAspect = CreatePopupMenu();
        for (int i = 0; i < static_cast<int>(g_aspectItems.size()); ++i)
        {
            char szNum[24];
            AspectLabel(g_aspectItems[i], szNum, sizeof(szNum));
            Item(hAspect, MFT_RADIOCHECK | Checked(g_aspectItems[i] == b->nAspect),
                 kIdAspectBase + i, szNum);
        }
        {
            char szVal[24];
            char szLabel[64];
            AspectLabel(b->nAspect, szVal, sizeof(szVal));
            snprintf(szLabel, sizeof(szLabel), "Aspect ratio  (%s)", szVal);
            AppendMenuA(hFov, MF_POPUP, reinterpret_cast<UINT_PTR>(hAspect), szLabel);
        }

        AppendMenuA(hRoot, MF_POPUP | Checked(b->bFovEnabled != 0),
                    reinterpret_cast<UINT_PTR>(hFov), "FOV Changer");

        HMENU hRender = CreatePopupMenu();
        Item(hRender, Checked(b->bDirectLightingOff != 0), kIdLight, "Direct Lighting");
        Item(hRender, Checked(b->bSkyboxOff != 0), kIdSky, "Skybox");
        AppendMenuA(hRoot, MF_POPUP, reinterpret_cast<UINT_PTR>(hRender), "Rendering");

        AppendMenuA(hRoot, MF_SEPARATOR, 0, nullptr);
        Item(hRoot, MF_UNCHECKED, kIdExitKill, "Exit + kill game");

        return hRoot;
    }

    int Apply(BridgeData* b, int cmd)
    {
        if (cmd == kIdExitKill)
            return kResultExitKillGame;

        if (cmd == kIdFovEnable)
        {
            b->bFovEnabled = !b->bFovEnabled;
            return kResultNone;
        }

        if (cmd >= kIdFovBase && cmd < kIdFovBase + static_cast<int>(g_fovItems.size()))
        {
            b->nFovValue = g_fovItems[cmd - kIdFovBase];
            b->bFovEnabled = true;
            return kResultNone;
        }

        if (cmd >= kIdAspectBase && cmd < kIdAspectBase + static_cast<int>(g_aspectItems.size()))
        {
            b->nAspect = g_aspectItems[cmd - kIdAspectBase];
            b->bFovEnabled = true;
            return kResultNone;
        }

        if (cmd == kIdLight)
        {
            b->bDirectLightingOff = !b->bDirectLightingOff;
            return kResultNone;
        }

        if (cmd == kIdSky)
        {
            b->bSkyboxOff = !b->bSkyboxOff;
            return kResultNone;
        }

        return kResultNone;
    }
}

void tray_menu::SetStatus(const char* szText)
{
    if (!szText)
        return;
    snprintf(g_szStatus, sizeof(g_szStatus), "%s", szText);
}

const char* tray_menu::Status()
{
    return g_szStatus;
}

int tray_menu::Popup(HWND hWnd, BridgeData* b)
{
    if (!b)
        return kResultNone;

    POINT pt{};
    GetCursorPos(&pt);

    SetForegroundWindow(hWnd);

    int nResult = kResultNone;
    while (true)
    {
        HMENU hRoot = Build(b);
        if (!hRoot)
            break;

        const int cmd = TrackPopupMenu(hRoot,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
            pt.x, pt.y, 0, hWnd, nullptr);
        PostMessageA(hWnd, WM_NULL, 0, 0);
        DestroyMenu(hRoot);

        if (cmd == 0)
            break;

        const int r = Apply(b, cmd);
        if (r != kResultNone)
        {
            nResult = r;
            break;
        }
    }
    return nResult;
}
