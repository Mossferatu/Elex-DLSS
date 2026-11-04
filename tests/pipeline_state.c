#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11_1.h>
#include <stdio.h>
#include <string.h>
#include "../src/pipeline_state.h"
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define OK(x) CHECK(SUCCEEDED(x))
int main(void){
    ID3D11Device *device=NULL;ID3D11DeviceContext *ctx=NULL;ID3D11DeviceContext1 *ctx1=NULL;
    OK(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,0,NULL,0,D3D11_SDK_VERSION,&device,NULL,&ctx));
    OK(ID3D11DeviceContext_QueryInterface(ctx,&IID_ID3D11DeviceContext1,(void **)&ctx1));
    ID3D11Buffer *constants=NULL,*vertices=NULL,*counter=NULL,*readback=NULL;
    D3D11_BUFFER_DESC b={0};b.ByteWidth=1024;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    OK(ID3D11Device_CreateBuffer(device,&b,NULL,&constants));
    b.ByteWidth=128;b.BindFlags=D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_INDEX_BUFFER;
    OK(ID3D11Device_CreateBuffer(device,&b,NULL,&vertices));
    b.BindFlags=D3D11_BIND_UNORDERED_ACCESS;b.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;b.StructureByteStride=4;
    OK(ID3D11Device_CreateBuffer(device,&b,NULL,&counter));
    b.ByteWidth=4;b.BindFlags=0;b.MiscFlags=0;b.StructureByteStride=0;b.Usage=D3D11_USAGE_STAGING;b.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    OK(ID3D11Device_CreateBuffer(device,&b,NULL,&readback));
    ID3D11UnorderedAccessView *uav=NULL;D3D11_UNORDERED_ACCESS_VIEW_DESC u={0};
    u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=32;u.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_COUNTER;
    OK(ID3D11Device_CreateUnorderedAccessView(device,(ID3D11Resource *)counter,&u,&uav));
    UINT first=16,count=32,stride=12,offset=4,initial_count=7;
    ID3D11DeviceContext1_VSSetConstantBuffers1(ctx1,0,1,&constants,&first,&count);
    ID3D11DeviceContext1_PSSetConstantBuffers1(ctx1,13,1,&constants,&first,&count);
    ID3D11DeviceContext1_CSSetConstantBuffers1(ctx1,3,1,&constants,&first,&count);
    ID3D11DeviceContext_IASetVertexBuffers(ctx,2,1,&vertices,&stride,&offset);
    ID3D11DeviceContext_IASetIndexBuffer(ctx,vertices,DXGI_FORMAT_R16_UINT,8);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D11_VIEWPORT vp={3,4,100,200,.1f,.9f};D3D11_RECT rect={4,5,90,180};
    ID3D11DeviceContext_RSSetViewports(ctx,1,&vp);ID3D11DeviceContext_RSSetScissorRects(ctx,1,&rect);
    FLOAT blend[4]={.2f,.3f,.4f,.5f};ID3D11DeviceContext_OMSetBlendState(ctx,NULL,blend,0x12345678);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx,NULL,37);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx,2,1,&uav,&initial_count);
    ElexPipelineState saved,expected,actual;
    elex_pipeline_save(ctx,ctx1,8,&saved);expected=saved;
    ID3D11DeviceContext_ClearState(ctx);
    elex_pipeline_restore(ctx,ctx1,&saved);
    elex_pipeline_save(ctx,ctx1,8,&actual);
    CHECK(memcmp(&actual,&expected,sizeof(actual))==0);
    elex_pipeline_restore(ctx,ctx1,&actual);
    ID3D11DeviceContext_CopyStructureCount(ctx,readback,0,uav);
    D3D11_MAPPED_SUBRESOURCE mapped={0};OK(ID3D11DeviceContext_Map(ctx,(ID3D11Resource *)readback,0,D3D11_MAP_READ,0,&mapped));
    CHECK(*(UINT *)mapped.pData==7);ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource *)readback,0);
    ID3D11DeviceContext_ClearState(ctx);
    ID3D11UnorderedAccessView_Release(uav);ID3D11Buffer_Release(readback);ID3D11Buffer_Release(counter);
    ID3D11Buffer_Release(vertices);ID3D11Buffer_Release(constants);ID3D11DeviceContext1_Release(ctx1);
    ID3D11DeviceContext_Release(ctx);ID3D11Device_Release(device);
    puts("PASS: explicit D3D11 state restoration, ranged constant buffers and UAV counter preservation on WARP");return 0;
}
