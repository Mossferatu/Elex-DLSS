#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <string.h>
#include "indicator.h"
#include "../third_party/minhook/include/MinHook.h"

// NVIDIA's indicator is drawn by the runtime itself, so it reports the chosen
// model rather than merely echoing our preset hint. Override its registry READ
// inside this process only. Never create or change a Windows registry value.
typedef LONG (NTAPI *QueryKeyFn)(HANDLE,ULONG,void *,ULONG,ULONG *);
typedef LSTATUS (WINAPI *QueryWFn)(HKEY,LPCWSTR,LPDWORD,LPDWORD,LPBYTE,LPDWORD);
typedef LSTATUS (WINAPI *QueryAFn)(HKEY,LPCSTR,LPDWORD,LPDWORD,LPBYTE,LPDWORD);
typedef LSTATUS (WINAPI *GetWFn)(HKEY,LPCWSTR,LPCWSTR,DWORD,LPDWORD,PVOID,LPDWORD);
typedef LSTATUS (WINAPI *GetAFn)(HKEY,LPCSTR,LPCSTR,DWORD,LPDWORD,PVOID,LPDWORD);
static QueryKeyFn query_key;
static QueryWFn original_query_w;
static QueryAFn original_query_a;
static GetWFn original_get_w;
static GetAFn original_get_a;
static RendererLog logger;
static LONG mode,reported;
static BOOL installed;

static BOOL ngx_key(HKEY key,const wchar_t *subkey)
{
    static const wchar_t full[]=L"\\REGISTRY\\MACHINE\\SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore";
    wchar_t path[1024];
    if(key==HKEY_LOCAL_MACHINE)wcscpy(path,L"\\REGISTRY\\MACHINE");
    else {
        struct {ULONG bytes;WCHAR text[1024];} name;
        ULONG required=0;
        if(!query_key||query_key(key,3,&name,sizeof(name),&required)<0||
           name.bytes%sizeof(WCHAR)||name.bytes>=sizeof(name.text))return FALSE;
        name.text[name.bytes/sizeof(WCHAR)]=0;
        wcscpy(path,name.text);
    }
    if(subkey&&*subkey){
        if(wcslen(path)+wcslen(subkey)+2>sizeof(path)/sizeof(path[0]))return FALSE;
        wcscat(path,L"\\");wcscat(path,subkey);
    }
    return _wcsicmp(path,full)==0;
}

static BOOL convertible_status(LSTATUS status)
{
    return status==ERROR_SUCCESS||status==ERROR_FILE_NOT_FOUND||status==ERROR_MORE_DATA;
}

static LSTATUS indicator_value(DWORD capacity,LPDWORD type,void *data,LPDWORD size,DWORD flags)
{
    DWORD types=flags&RRF_RT_ANY;
    if(types&&!(types&RRF_RT_REG_DWORD))return ERROR_UNSUPPORTED_TYPE;
    if(data&&!size)return ERROR_INVALID_PARAMETER;
    if(type)*type=REG_DWORD;
    if(size)*size=sizeof(DWORD);
    if(data){
        if(capacity<sizeof(DWORD))return ERROR_MORE_DATA;
        DWORD value=InterlockedCompareExchange(&mode,0,0)>0?0x400:0;
        memcpy(data,&value,sizeof(value));
        if(InterlockedCompareExchange(&reported,1,0)==0&&logger)
            logger("NVIDIA native DLSS indicator read overridden for this process: 0x%lx; registry unchanged",value);
    }
    return ERROR_SUCCESS;
}

static LSTATUS WINAPI query_w(HKEY key,LPCWSTR value,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD size)
{
    DWORD capacity=size?*size:0;
    LSTATUS result=original_query_w(key,value,reserved,type,data,size);
    if(!mode||reserved||!value||_wcsicmp(value,L"ShowDlssIndicator")||!convertible_status(result)||!ngx_key(key,NULL))return result;
    return indicator_value(capacity,type,data,size,0);
}
static LSTATUS WINAPI query_a(HKEY key,LPCSTR value,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD size)
{
    DWORD capacity=size?*size:0;
    LSTATUS result=original_query_a(key,value,reserved,type,data,size);
    if(!mode||reserved||!value||_stricmp(value,"ShowDlssIndicator")||!convertible_status(result)||!ngx_key(key,NULL))return result;
    return indicator_value(capacity,type,data,size,0);
}
static LSTATUS WINAPI get_w(HKEY key,LPCWSTR subkey,LPCWSTR value,DWORD flags,LPDWORD type,PVOID data,LPDWORD size)
{
    DWORD capacity=size?*size:0;
    LSTATUS result=original_get_w(key,subkey,value,flags,type,data,size);
    if(!mode||!value||_wcsicmp(value,L"ShowDlssIndicator")||!convertible_status(result)||!ngx_key(key,subkey))return result;
    result=indicator_value(capacity,type,data,size,flags);
    if(result!=ERROR_SUCCESS&&(flags&RRF_ZEROONFAILURE)&&data&&capacity)memset(data,0,capacity);
    return result;
}
static LSTATUS WINAPI get_a(HKEY key,LPCSTR subkey,LPCSTR value,DWORD flags,LPDWORD type,PVOID data,LPDWORD size)
{
    DWORD capacity=size?*size:0;
    LSTATUS result=original_get_a(key,subkey,value,flags,type,data,size);
    if(!mode||!value||_stricmp(value,"ShowDlssIndicator")||!convertible_status(result))return result;
    wchar_t wide[1024];
    if(subkey&&!MultiByteToWideChar(CP_ACP,0,subkey,-1,wide,1024))return result;
    if(!ngx_key(key,subkey?wide:NULL))return result;
    result=indicator_value(capacity,type,data,size,flags);
    if(result!=ERROR_SUCCESS&&(flags&RRF_ZEROONFAILURE)&&data&&capacity)memset(data,0,capacity);
    return result;
}

HRESULT elex_indicator_install(const wchar_t *ini,RendererLog log)
{
    logger=log;
    wchar_t option[24];GetPrivateProfileStringW(L"DLSS",L"Indicator",L"On",option,24,ini);
    LONG requested=0;
    if(!_wcsicmp(option,L"On"))requested=1;
    else if(!_wcsicmp(option,L"Off"))requested=-1;
    else if(_wcsicmp(option,L"Driver"))log("Invalid DLSS Indicator=%ls; using Driver",option);
    if(!installed&&requested){
        query_key=(QueryKeyFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryKey");
        if(!query_key)return E_NOINTERFACE;
        HMODULE advapi=GetModuleHandleW(L"advapi32.dll");
        if(!advapi)return E_NOINTERFACE;
        const char *names[]={"RegQueryValueExW","RegQueryValueExA","RegGetValueW","RegGetValueA"};
        void *detours[]={(void *)query_w,(void *)query_a,(void *)get_w,(void *)get_a};
        void **originals[]={(void **)&original_query_w,(void **)&original_query_a,(void **)&original_get_w,(void **)&original_get_a};
        void *targets[4]={0};unsigned count=0;
        MH_STATUS status=MH_Initialize();
        if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return E_FAIL;
        for(unsigned i=0;i<4;++i){
            targets[i]=(void *)GetProcAddress(advapi,names[i]);
            if(!targets[i]||MH_CreateHook(targets[i],detours[i],originals[i])!=MH_OK)break;
            ++count;
        }
        if(count!=4){for(unsigned i=0;i<count;++i)MH_RemoveHook(targets[i]);return E_FAIL;}
        for(unsigned i=0;i<4;++i)MH_QueueEnableHook(targets[i]);
        status=MH_ApplyQueued();
        if(status!=MH_OK){for(unsigned i=0;i<4;++i)MH_RemoveHook(targets[i]);return E_FAIL;}
        installed=TRUE;
    }
    InterlockedExchange(&mode,requested);InterlockedExchange(&reported,0);
    log("Native DLSS indicator: %s (process-local; Windows registry is not modified)",requested>0?"On":requested<0?"Off":"Driver");
    return S_OK;
}
