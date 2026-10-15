#pragma once

#include <cstdint>

constexpr const char* kBridgeName = "DysonBehindBridge_1422450";
constexpr int32_t     kBridgeVer  = 2;
constexpr uint32_t    kBridgeSize = 39;

#pragma pack(push, 1)
struct BridgeData
{
    int32_t version;

    bool bFovEnabled;
    bool bDirectLightingOff;
    bool bSkyboxOff;

    int32_t nFovValue;
    int32_t nAspect;
    int32_t nSkyPreset;

    int32_t nFovApplied;
    int32_t nAspectApplied;

    int32_t nWriterAlive;

    bool bTrayMode;
    uint8_t bPad[7];
};
#pragma pack(pop)

static_assert(sizeof(BridgeData) == kBridgeSize);
