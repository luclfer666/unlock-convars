#pragma once

#include <Windows.h>

#include <cstdint>

#pragma pack(push, 1)
struct MapHandoffData
{
    uint32_t nMagic;
    uint32_t nVersion;
    uint32_t nAck;
    char     szModuleDir[MAX_PATH];
};
#pragma pack(pop)

namespace MapHandoff
{
    inline constexpr uint32_t kMagic   = 0x48444D52u;
    inline constexpr uint32_t kVersion = 1u;
    inline constexpr uint32_t kAck     = 0x4B4341u;

    static_assert(sizeof(MapHandoffData) == 12 + MAX_PATH, "MapHandoffData size");
}
