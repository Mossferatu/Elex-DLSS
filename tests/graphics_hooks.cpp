#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdint>
#include "../src/graphics_hooks.h"

// Deliberately unrelated current vtable: overlays and D3D10 emulation can pass
// such a context into a hooked native implementation. Routing must still reach
// the implementation whose entry point was intercepted, with every argument.
struct Wrapped {void **vtable;unsigned value;};
static volatile unsigned calls;
static void *fake_table[160]{};
using Map=HRESULT (STDMETHODCALLTYPE *)(ID3D11DeviceContext *,ID3D11Resource *,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE *);
using Unmap=void (STDMETHODCALLTYPE *)(ID3D11DeviceContext *,ID3D11Resource *,UINT);
using Indexed=void (STDMETHODCALLTYPE *)(ID3D11DeviceContext *,UINT,UINT,INT);
using Finish=HRESULT (STDMETHODCALLTYPE *)(ID3D11DeviceContext *,BOOL,ID3D11CommandList **);
using Pixel=HRESULT (STDMETHODCALLTYPE *)(ID3D11Device *,const void *,SIZE_T,ID3D11ClassLinkage *,ID3D11PixelShader **);
using Draw=void (STDMETHODCALLTYPE *)(ID3D11DeviceContext *,UINT,UINT);
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
__declspec(noinline) static HRESULT STDMETHODCALLTYPE map_a(ID3D11DeviceContext *ctx,ID3D11Resource *r,UINT sub,D3D11_MAP kind,UINT flags,D3D11_MAPPED_SUBRESOURCE *out){
    ++calls;out->pData=r;out->RowPitch=sub+kind;out->DepthPitch=flags;reinterpret_cast<Wrapped *>(ctx)->value=11;return S_OK;
}
__declspec(noinline) static HRESULT STDMETHODCALLTYPE map_b(ID3D11DeviceContext *ctx,ID3D11Resource *r,UINT sub,D3D11_MAP kind,UINT flags,D3D11_MAPPED_SUBRESOURCE *out){
    ++calls;out->pData=r;out->RowPitch=sub+kind;out->DepthPitch=flags;reinterpret_cast<Wrapped *>(ctx)->value=22;return S_FALSE;
}
__declspec(noinline) static void STDMETHODCALLTYPE unmap_original(ID3D11DeviceContext *ctx,ID3D11Resource *r,UINT sub){
    ++calls;reinterpret_cast<Wrapped *>(ctx)->value=sub+static_cast<unsigned>(reinterpret_cast<uintptr_t>(r));
}
__declspec(noinline) static void STDMETHODCALLTYPE indexed_original(ID3D11DeviceContext *ctx,UINT count,UINT start,INT base){
    ++calls;reinterpret_cast<Wrapped *>(ctx)->value=count+start+base;
}
__declspec(noinline) static HRESULT STDMETHODCALLTYPE finish_original(ID3D11DeviceContext *ctx,BOOL restore,ID3D11CommandList **out){
    ++calls;*out=reinterpret_cast<ID3D11CommandList *>(ctx);return restore?S_FALSE:S_OK;
}
__declspec(noinline) static HRESULT STDMETHODCALLTYPE pixel_original(ID3D11Device *device,const void *data,SIZE_T size,ID3D11ClassLinkage *linkage,ID3D11PixelShader **out){
    ++calls;reinterpret_cast<Wrapped *>(device)->value=static_cast<unsigned>(size)+static_cast<unsigned>(reinterpret_cast<uintptr_t>(linkage));*out=(ID3D11PixelShader *)data;return S_FALSE;
}
__declspec(noinline) static void STDMETHODCALLTYPE draw_original(ID3D11DeviceContext *ctx,UINT count,UINT start){
    ++calls;reinterpret_cast<Wrapped *>(ctx)->value=count*start;
}
static HRESULT STDMETHODCALLTYPE map_hook(void *fn,ID3D11DeviceContext *ctx,ID3D11Resource *r,UINT sub,D3D11_MAP kind,UINT flags,D3D11_MAPPED_SUBRESOURCE *out){return reinterpret_cast<Map>(fn)(ctx,r,sub,kind,flags,out);}
static void STDMETHODCALLTYPE unmap_hook(void *fn,ID3D11DeviceContext *ctx,ID3D11Resource *r,UINT sub){reinterpret_cast<Unmap>(fn)(ctx,r,sub);}
static void STDMETHODCALLTYPE indexed_hook(void *fn,ID3D11DeviceContext *ctx,UINT count,UINT start,INT base){reinterpret_cast<Indexed>(fn)(ctx,count,start,base);}
static HRESULT STDMETHODCALLTYPE finish_hook(void *fn,ID3D11DeviceContext *ctx,BOOL restore,ID3D11CommandList **out){return reinterpret_cast<Finish>(fn)(ctx,restore,out);}
static HRESULT STDMETHODCALLTYPE pixel_hook(void *fn,ID3D11Device *d,const void *data,SIZE_T size,ID3D11ClassLinkage *l,ID3D11PixelShader **out){return reinterpret_cast<Pixel>(fn)(d,data,size,l,out);}
static void STDMETHODCALLTYPE draw_hook(void *fn,ID3D11DeviceContext *ctx,UINT count,UINT start){reinterpret_cast<Draw>(fn)(ctx,count,start);}
int main(){
    Wrapped object{fake_table,0};auto *ctx=reinterpret_cast<ID3D11DeviceContext *>(&object);
    auto *resource=reinterpret_cast<ID3D11Resource *>(13);
    CHECK(SUCCEEDED(elex_hook_graphics((void *)map_a,ELEX_HOOK_MAP,(void *)map_hook)));
    CHECK(SUCCEEDED(elex_hook_graphics((void *)map_b,ELEX_HOOK_MAP,(void *)map_hook)));
    CHECK(SUCCEEDED(elex_hook_graphics((void *)map_a,ELEX_HOOK_MAP,(void *)map_hook)));
    CHECK(FAILED(elex_hook_graphics((void *)map_a,ELEX_HOOK_DRAW,(void *)draw_hook)));
    Map volatile first=map_a,second=map_b;D3D11_MAPPED_SUBRESOURCE mapped{};
    CHECK(first(ctx,resource,3,D3D11_MAP_WRITE_DISCARD,7,&mapped)==S_OK);
    CHECK(object.value==11&&mapped.pData==resource&&mapped.RowPitch==7&&mapped.DepthPitch==7);
    CHECK(second(ctx,resource,2,D3D11_MAP_READ,9,&mapped)==S_FALSE&&object.value==22);
    CHECK(SUCCEEDED(elex_hook_graphics((void *)unmap_original,ELEX_HOOK_UNMAP,(void *)unmap_hook)));
    Unmap volatile unmap=unmap_original;unmap(ctx,resource,4);CHECK(object.value==17);
    CHECK(SUCCEEDED(elex_hook_graphics((void *)indexed_original,ELEX_HOOK_INDEXED,(void *)indexed_hook)));
    Indexed volatile indexed=indexed_original;indexed(ctx,100,12,-3);CHECK(object.value==109);
    CHECK(SUCCEEDED(elex_hook_graphics((void *)finish_original,ELEX_HOOK_FINISH,(void *)finish_hook)));
    Finish volatile finish=finish_original;ID3D11CommandList *list=nullptr;CHECK(finish(ctx,TRUE,&list)==S_FALSE&&list==reinterpret_cast<ID3D11CommandList *>(ctx));
    CHECK(SUCCEEDED(elex_hook_graphics((void *)pixel_original,ELEX_HOOK_PIXEL,(void *)pixel_hook)));
    Pixel volatile pixel=pixel_original;ID3D11PixelShader *shader=nullptr;
    CHECK(pixel(reinterpret_cast<ID3D11Device *>(&object),resource,31,reinterpret_cast<ID3D11ClassLinkage *>(5),&shader)==S_FALSE&&object.value==36&&shader==reinterpret_cast<ID3D11PixelShader *>(resource));
    CHECK(SUCCEEDED(elex_hook_graphics((void *)draw_original,ELEX_HOOK_DRAW,(void *)draw_hook)));
    Draw volatile draw=draw_original;draw(ctx,7,8);CHECK(object.value==56);
    CHECK(calls==7);
    std::puts("PASS: stable originals for wrapped contexts, multiple targets, all six graphics hook signatures and idempotent registration");
}
