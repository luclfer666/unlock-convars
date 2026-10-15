#pragma once

#include <cstdint>

namespace FovAspect
{
    inline bool  bFovEnabled   = false;
    inline float flFovValue    = 120.f;
    inline float flAspectTest  = 1.50f;
    inline bool  bAspectCustom = false;

    void Tick(bool bInGame);

    void Reset();
}

namespace DirectLighting
{
    inline bool bDisabled = false;

    void Tick();

    void Reset();
}

namespace Skybox
{
    inline bool bHidden = false;

    void Tick();
    void Reset();
}
