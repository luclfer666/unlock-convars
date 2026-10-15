#pragma once

#include <Windows.h>

#include "Bridge.h"

namespace tray_menu
{
    constexpr int kResultNone         = 0;
    constexpr int kResultExitKillGame = 2;

    void SetStatus(const char* szText);
    const char* Status();

    int Popup(HWND hWnd, BridgeData* pBridge);
}
