Texture2D<float4> DlssColor : register(t0);
RWTexture2D<float4> Output : register(u0);
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    Output.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    Output[id.xy] = float4(DlssColor.Load(int3(id.xy, 0)).rgb, 1);
}
