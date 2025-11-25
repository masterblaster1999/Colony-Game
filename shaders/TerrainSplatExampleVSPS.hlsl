#include "TerrainSplatExample.hlsli"

VSOut mainVS(VSIn IN)
{
    return VSTerrain(IN);
}

PSOut mainPS(VSOut IN)
{
    return PSTerrain(IN);
}
