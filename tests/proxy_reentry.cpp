#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cwchar>
#include "../third_party/minhook/include/MinHook.h"
using Create=HRESULT (WINAPI *)(IDXGIAdapter *,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL *,UINT,UINT,ID3D11Device **,D3D_FEATURE_LEVEL *,ID3D11DeviceContext **);
using Load=HMODULE (WINAPI *)(LPCWSTR);
static Create proxy_create,original_create;
static Load original_load;
static thread_local bool probing;
static bool boot_checked,probe_checked;
static wchar_t system_d3d11[MAX_PATH];
static HRESULT WINAPI create_hook(IDXGIAdapter *a,D3D_DRIVER_TYPE type,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL *levels,UINT count,UINT sdk,ID3D11Device **out,D3D_FEATURE_LEVEL *chosen,ID3D11DeviceContext **context){
    if(!probing){
        probing=true;
        ID3D11Device *probe=nullptr;ID3D11DeviceContext *probe_context=nullptr;
        HRESULT result=proxy_create(a,type,software,flags,levels,count,sdk,&probe,nullptr,&probe_context);
        probe_checked=SUCCEEDED(result)&&probe&&probe_context;
        if(probe_context)probe_context->Release();if(probe)probe->Release();
        probing=false;
    }
    return original_create(a,type,software,flags,levels,count,sdk,out,chosen,context);
}
static HMODULE WINAPI load_hook(LPCWSTR path){
    if(!boot_checked&&proxy_create&&path&&!_wcsicmp(path,system_d3d11)){
        ID3D11Device *device=reinterpret_cast<ID3D11Device *>(1);
        ID3D11DeviceContext *context=reinterpret_cast<ID3D11DeviceContext *>(1);
        HRESULT result=proxy_create(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        boot_checked=result==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE&&!device&&!context;
    }
    return original_load(path);
}
int wmain(int argc,wchar_t **argv){
    if(argc!=2)return 2;
    GetSystemDirectoryW(system_d3d11,MAX_PATH);wcscat(system_d3d11,L"\\d3d11.dll");
    HMODULE native=LoadLibraryW(system_d3d11),mod=LoadLibraryW(argv[1]);if(!native||!mod)return 1;
    proxy_create=reinterpret_cast<Create>(GetProcAddress(mod,"D3D11CreateDevice"));
    void *create_target=reinterpret_cast<void *>(GetProcAddress(native,"D3D11CreateDevice"));
    void *load_target=reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"));
    if(!proxy_create||MH_Initialize()!=MH_OK)return 1;
    if(MH_CreateHook(create_target,reinterpret_cast<void *>(create_hook),reinterpret_cast<void **>(&original_create))!=MH_OK ||
       MH_CreateHook(load_target,reinterpret_cast<void *>(load_hook),reinterpret_cast<void **>(&original_load))!=MH_OK)return 1;
    if(MH_EnableHook(create_target)!=MH_OK||MH_EnableHook(load_target)!=MH_OK)return 1;
    ID3D11Device *device=nullptr;ID3D11DeviceContext *context=nullptr;
    HRESULT result=proxy_create(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
    if(FAILED(result)||!device||!context||!boot_checked||!probe_checked){
        std::printf("FAIL: result=%08lx loader=%d probe=%d\n",static_cast<unsigned long>(result),boot_checked,probe_checked);return 1;
    }
    std::printf("Expected renderer device=%p\n",static_cast<void *>(device));
    context->Release();device->Release();
    MH_DisableHook(create_target);MH_DisableHook(load_target);MH_Uninitialize();
    std::puts("PASS: loader reentry returns promptly; nested overlay probe forwards without initializing renderer");return 0;
}
