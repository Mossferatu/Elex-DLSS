Texture2D<float4> Scene : register(t0);
Texture2D<float> Depth : register(t1);
Texture2D<float2> Motion : register(t2);
RWTexture2D<float4> ColorOut : register(u0);
RWTexture2D<float> DepthOut : register(u1);
RWTexture2D<float2> MotionOut : register(u2);
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    ColorOut.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    ColorOut[id.xy] = float4(Scene.Load(int3(id.xy, 0)).rgb, 1);
    DepthOut[id.xy] = Depth.Load(int3(id.xy, 0));
    MotionOut[id.xy] = Motion.Load(int3(id.xy, 0));
}
