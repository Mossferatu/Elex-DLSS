#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <array>
#include <utility>
#include "graphics_hooks.h"
#include "../third_party/minhook/include/MinHook.h"

namespace {
struct Entry {void *target{},*original{},*callback{};ElexGraphicsHook kind{};};
constexpr size_t capacity=64;
Entry entries[capacity];
size_t used{};
SRWLOCK lock=SRWLOCK_INIT;
using Map=HRESULT (STDMETHODCALLTYPE *)(void *,ID3D11DeviceContext *,ID3D11Resource *,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE *);
using Unmap=void (STDMETHODCALLTYPE *)(void *,ID3D11DeviceContext *,ID3D11Resource *,UINT);
using Indexed=void (STDMETHODCALLTYPE *)(void *,ID3D11DeviceContext *,UINT,UINT,INT);
using Finish=HRESULT (STDMETHODCALLTYPE *)(void *,ID3D11DeviceContext *,BOOL,ID3D11CommandList **);
using Pixel=HRESULT (STDMETHODCALLTYPE *)(void *,ID3D11Device *,const void *,SIZE_T,ID3D11ClassLinkage *,ID3D11PixelShader **);
using Draw=void (STDMETHODCALLTYPE *)(void *,ID3D11DeviceContext *,UINT,UINT);
template<size_t i> HRESULT STDMETHODCALLTYPE map(ID3D11DeviceContext *c,ID3D11Resource *r,UINT sub,D3D11_MAP mode,UINT flags,D3D11_MAPPED_SUBRESOURCE *out){
    auto &e=entries[i];return reinterpret_cast<Map>(e.callback)(e.original,c,r,sub,mode,flags,out);
}
template<size_t i> void STDMETHODCALLTYPE unmap(ID3D11DeviceContext *c,ID3D11Resource *r,UINT sub){
    auto &e=entries[i];reinterpret_cast<Unmap>(e.callback)(e.original,c,r,sub);
}
template<size_t i> void STDMETHODCALLTYPE indexed(ID3D11DeviceContext *c,UINT n,UINT start,INT base){
    auto &e=entries[i];reinterpret_cast<Indexed>(e.callback)(e.original,c,n,start,base);
}
template<size_t i> HRESULT STDMETHODCALLTYPE finish(ID3D11DeviceContext *c,BOOL restore,ID3D11CommandList **out){
    auto &e=entries[i];return reinterpret_cast<Finish>(e.callback)(e.original,c,restore,out);
}
template<size_t i> HRESULT STDMETHODCALLTYPE pixel(ID3D11Device *d,const void *bytes,SIZE_T size,ID3D11ClassLinkage *linkage,ID3D11PixelShader **out){
    auto &e=entries[i];return reinterpret_cast<Pixel>(e.callback)(e.original,d,bytes,size,linkage,out);
}
template<size_t i> void STDMETHODCALLTYPE draw(ID3D11DeviceContext *c,UINT n,UINT start){
    auto &e=entries[i];reinterpret_cast<Draw>(e.callback)(e.original,c,n,start);
}
struct Thunks {
    decltype(&map<0>) map;
    decltype(&unmap<0>) unmap;
    decltype(&indexed<0>) indexed;
    decltype(&finish<0>) finish;
    decltype(&pixel<0>) pixel;
    decltype(&draw<0>) draw;
};
template<size_t... i> constexpr std::array<Thunks,sizeof...(i)> make(std::index_sequence<i...>){
    return {{Thunks{&map<i>,&unmap<i>,&indexed<i>,&finish<i>,&pixel<i>,&draw<i>}...}};
}
constexpr auto thunks=make(std::make_index_sequence<capacity>{});
}

extern "C" HRESULT elex_hook_graphics(void *target,ElexGraphicsHook kind,void *callback){
    if(!target||!callback)return E_INVALIDARG;
    AcquireSRWLockExclusive(&lock);
    for(size_t i=0;i<used;++i)if(entries[i].target==target){
        HRESULT result=entries[i].kind==kind&&entries[i].callback==callback?S_OK:E_UNEXPECTED;
        ReleaseSRWLockExclusive(&lock);return result;
    }
    if(used==capacity){ReleaseSRWLockExclusive(&lock);return E_OUTOFMEMORY;}
    void *detour=nullptr;
    switch(kind){
        case ELEX_HOOK_MAP:detour=reinterpret_cast<void *>(thunks[used].map);break;
        case ELEX_HOOK_UNMAP:detour=reinterpret_cast<void *>(thunks[used].unmap);break;
        case ELEX_HOOK_INDEXED:detour=reinterpret_cast<void *>(thunks[used].indexed);break;
        case ELEX_HOOK_FINISH:detour=reinterpret_cast<void *>(thunks[used].finish);break;
        case ELEX_HOOK_PIXEL:detour=reinterpret_cast<void *>(thunks[used].pixel);break;
        case ELEX_HOOK_DRAW:detour=reinterpret_cast<void *>(thunks[used].draw);break;
    }
    if(!detour){ReleaseSRWLockExclusive(&lock);return E_INVALIDARG;}
    MH_STATUS status=MH_Initialize();
    if(status==MH_OK||status==MH_ERROR_ALREADY_INITIALIZED){
        auto &entry=entries[used];entry.target=target;entry.kind=kind;entry.callback=callback;
        status=MH_CreateHook(target,detour,&entry.original);
        if(status==MH_OK){
            status=MH_EnableHook(target);
            if(status==MH_OK)++used;else MH_RemoveHook(target);
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return status==MH_OK?S_OK:E_FAIL;
}
