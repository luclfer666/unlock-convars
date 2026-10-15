#pragma once

#include "Bridge.h"

namespace MenuControl
{
    void SetMenuOpen(bool bOpen);
}

namespace TrayBridge
{
    void Tick();

    bool IsActive();

    bool IsTrayMode();

    void Reset();
}
