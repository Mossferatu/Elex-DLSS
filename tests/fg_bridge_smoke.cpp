#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include "../src/frame_generation.h"

static void logger(const char *format,...){va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');fflush(stdout);}
static LRESULT CALLBACK window_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){return DefWindowProcW(hwnd,msg,wp,lp);}
static const char shader[]=R"(
cbuffer Frame : register(b0) { float x; float previous; float2 padding; }
struct V { float4 position : SV_Position; };
V vs(uint id:SV_VertexID) { V v;float2 uv=float2((id<<1)&2,id&2);v.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);return v; }
struct P { float4 color:SV_Target0;float depth:SV_Target1;float2 motion:SV_Target2; };
P ps(V v) { P p;float2 uv=v.position.xy/float2(1280,720);bool box=abs(uv.x-x)<.1&&abs(uv.y-.5)<.15;
float checker=fmod(floor(uv.x*32)+floor(uv.y*18),2);p.color=box?float4(1,.2,.1,1):float4(.1+checker*.3,.2,.5,1);
p.depth=box?.6:.95;p.motion=box?float2(x-previous,0):float2(0,0);return p; }
)";

int wmain(int argc,wchar_t **argv){
    if(argc!=3){fputs("fg-bridge-smoke <Streamline runtime directory> <logs>\n",stderr);return 2;}
    HRESULT hr=S_OK;
#define CHECK(call) do{hr=(call);if(FAILED(hr)){logger("FAIL line %d: %s=0x%08lx",__LINE__,#call,static_cast<unsigned long>(hr));return 1;}}while(0)
    WNDCLASSW cls{};cls.lpfnWndProc=window_proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"ElexFgBridgeCheck";
    RegisterClassW(&cls);
    HWND hwnd=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"ELEX frame generation bridge check",WS_OVERLAPPEDWINDOW,
        40,40,960,580,nullptr,nullptr,cls.hInstance,nullptr);
    if(!hwnd)return 1;
    ID3D11Device *device=nullptr;ID3D11DeviceContext *context=nullptr;
    CHECK(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=1280;desc.BufferDesc.Height=720;
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=1;desc.OutputWindow=hwnd;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain *swap=nullptr;
    CHECK(elex_fg_create_test(device,&desc,&swap,argv[1],argv[2],logger,122));
    ID3D11Texture2D *color=nullptr,*depth=nullptr,*motion=nullptr;
    CHECK(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void **>(&color)));
    D3D11_TEXTURE2D_DESC td{};color->GetDesc(&td);td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    td.Format=DXGI_FORMAT_R32_FLOAT;CHECK(device->CreateTexture2D(&td,nullptr,&depth));
    td.Format=DXGI_FORMAT_R16G16_FLOAT;CHECK(device->CreateTexture2D(&td,nullptr,&motion));
    ID3D11RenderTargetView *targets[3]{};
    CHECK(device->CreateRenderTargetView(color,nullptr,&targets[0]));
    CHECK(device->CreateRenderTargetView(depth,nullptr,&targets[1]));
    CHECK(device->CreateRenderTargetView(motion,nullptr,&targets[2]));
    ID3DBlob *vs=nullptr,*ps=nullptr,*error=nullptr;
    hr=D3DCompile(shader,sizeof(shader),"fg-test",nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error);
    if(error){logger("compiler %s",static_cast<char *>(error->GetBufferPointer()));error->Release();error=nullptr;}CHECK(hr);
    hr=D3DCompile(shader,sizeof(shader),"fg-test",nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error);
    if(error){logger("compiler %s",static_cast<char *>(error->GetBufferPointer()));error->Release();error=nullptr;}CHECK(hr);
    ID3D11VertexShader *vertex=nullptr;ID3D11PixelShader *pixel=nullptr;
    CHECK(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex));
    CHECK(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel));
    vs->Release();ps->Release();
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer *cb=nullptr;CHECK(device->CreateBuffer(&bd,nullptr,&cb));
    float globals[192]{};
    for(unsigned i=0;i<4;++i){globals[48+i*5]=1;globals[80+i*5]=1;}
    globals[80]=.97427857f;globals[85]=1.7320508f;globals[90]=1.00010001f;globals[91]=-.100010001f;globals[94]=1;globals[95]=0;
    globals[139]=1000;globals[147]=.1f;globals[152]=1280;globals[153]=720;globals[162]=.02f;
    D3D11_VIEWPORT viewport{0,0,1280,720,0,1};float previous=.5f;
    // Respect the process startup show state; the tool launches this probe hidden.
    STARTUPINFOW startup{};startup.cb=sizeof(startup);GetStartupInfoW(&startup);
    ShowWindow(hwnd,(startup.dwFlags&STARTF_USESHOWWINDOW)?startup.wShowWindow:SW_SHOWNOACTIVATE);
    for(unsigned frame=1;frame<=150;++frame){
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        elex_fg_simulation_start(frame);Sleep(18);elex_fg_render_start();
        D3D11_MAPPED_SUBRESOURCE mapped{};CHECK(context->Map(cb,0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
        float x=.5f+.2f*std::sin(frame*.025f);float values[4]={x,previous,0,0};std::memcpy(mapped.pData,values,sizeof(values));context->Unmap(cb,0);previous=x;
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex,nullptr,0);context->PSSetShader(pixel,nullptr,0);context->PSSetConstantBuffers(0,1,&cb);
        context->RSSetViewports(1,&viewport);context->OMSetRenderTargets(3,targets,nullptr);context->Draw(3,0);
        elex_fg_capture(context,depth,motion,color,globals,frame==1);
        CHECK(swap->Present(0,0));
    }
    context->ClearState();context->Flush();
    for(auto *view:targets)view->Release();color->Release();depth->Release();motion->Release();cb->Release();vertex->Release();pixel->Release();
    swap->Release();context->Release();device->Release();DestroyWindow(hwnd);
    logger("PASS: D3D11 virtual backbuffer, shared resources/fences and native Streamline presents completed");
    return 0;
}
