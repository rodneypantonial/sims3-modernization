namespace Ts3Agent
{
    // Game-independent stranded rule of unseen_water_watch (tools/selftest/waterrule checks it). Depth below the sea surface alone
    // is not water: IP has dry lots below sea level (Hobart's Hideaway, docs/247 §11), which read 2 m deep and stranded a Sim
    // standing in a lab lecture there (EA ip-uboat-repro, 2026-11-08). The point must also be water by the game's own terrain
    // type, World.GetTerrainType(pos): WorldSea 2, WorldPond 8, LotPool 64, LotPond 128 (Sims3.SimIFace.TerrainType).
    public static class WaterRule
    {
        public const int kWaterBits = 2 | 8 | 64 | 128;
        public static bool IsWater(int terrainType) { return (terrainType & kWaterBits) != 0; }
        // in water deeper than 0.5 m and not standing on something 0.3 m above the surface (deck, dock, pier, bridge)
        public static bool InWater(float depth, float yAboveSea, int terrainType) { return depth > 0.5f && yAboveSea <= 0.3f && IsWater(terrainType); }
    }
}
