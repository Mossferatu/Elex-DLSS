// The engine's current positions include raster jitter, but previous positions
// use the unjittered camera matrix. Remove only that current jitter from motion.
cbuffer EngineGlobals : register(b13) { float4 Engine[48]; }
float2 main(float4 position : SV_Position, float4 current : CLIPPOSITION,
            float4 previous : PREVCLIPPOSITION, bool front : SV_IsFrontFace) : SV_Target0
{
    float2 rasterUV = float2(Engine[8].z - Engine[20].z, Engine[9].z - Engine[21].z) * float2(.5, -.5);
    return (current.xy / current.w - previous.xy / previous.w) * float2(.5, -.5) - rasterUV;
}
