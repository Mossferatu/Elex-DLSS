#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdarg>
#include "../src/streamline_backend.h"

static void log_message(const char *format,...)
{
    va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');fflush(stdout);
}
int wmain(int argc,wchar_t **argv)
{
    if(argc!=3){fputs("fg-capability <Streamline runtime directory> <log directory>\n",stderr);return 2;}
    StreamlineBackend api;
    if(!api.open(argv[1],argv[2],log_message))return 1;
    IDXGIFactory1 *factory=nullptr;IDXGIAdapter1 *adapter=nullptr;ID3D12Device *device=nullptr;
    HRESULT hr=CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void **>(&factory));
    DXGI_ADAPTER_DESC1 description{};
    if(SUCCEEDED(hr))for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){
        adapter->GetDesc1(&description);
        if(description.VendorId==0x10de)break;
        adapter->Release();adapter=nullptr;
    }
    if(adapter)hr=D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),reinterpret_cast<void **>(&device));
    bool success=SUCCEEDED(hr)&&device&&api.bind(device,description.AdapterLuid);
    printf("Native Dynamic MFG capability probe: %s\n",success?"PASS":"UNAVAILABLE");
    api.close();
    if(device)device->Release();if(adapter)adapter->Release();if(factory)factory->Release();
    return success?0:1;
}
