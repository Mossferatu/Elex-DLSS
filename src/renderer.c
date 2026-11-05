#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <math.h>
#include "renderer.h"
#include "ngx_backend.h"
#include "engine_hooks.h"
#include "frame_generation.h"
#include "indicator.h"
#include "graphics_hooks.h"
#include "pipeline_state.h"
#include "../third_party/minhook/include/MinHook.h"
#include "../build/shaders.h"

enum { TAG_CAMERA = 1, TAG_OBJECT = 2, TAG_NEIGHBOR = 3, TAG_RESOLVE = 4, TAG_TONEMAP = 5 };
static const GUID shader_key = {0x45790e31,0x636f,0x4c5c,{0x81,0x92,0xab,0x22,0x7e,0x71,0x84,0xc0}};
static const GUID shadow_key = {0x1e6fdb43,0xb980,0x46cc,{0xb6,0x36,0x92,0x05,0x77,0x74,0x21,0x0e}};
static const GUID frame_key = {0xe7bcb934,0x0cb1,0x43b3,{0x92,0xf4,0x32,0x88,0x45,0x74,0x67,0x34}};
typedef struct { float globals[192]; } DeferredFrame;
typedef struct { ID3D11DeviceContext1 *ctx1; ID3D11Buffer *buffer; UINT first, count; } ObjectBinding;
typedef struct { ID3D11DeviceContext *context; ID3D11Resource *resource; void *data; UINT size; } MappedBuffer;
typedef struct { ID3D11Texture2D *texture; ID3D11ShaderResourceView *srv; ID3D11UnorderedAccessView *uav; } Surface;
typedef struct { ID3D11PixelShader *shader; UINT tag; } TaggedPixelShader;
typedef struct { ID3D11ComputeShader *shader; UINT tag; } TaggedComputeShader;
typedef HRESULT (STDMETHODCALLTYPE *MapFn)(ID3D11DeviceContext *, ID3D11Resource *, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE *);
typedef void (STDMETHODCALLTYPE *UnmapFn)(ID3D11DeviceContext *, ID3D11Resource *, UINT);
typedef void (STDMETHODCALLTYPE *DispatchFn)(ID3D11DeviceContext *, UINT, UINT, UINT);
typedef void (STDMETHODCALLTYPE *IndexedFn)(ID3D11DeviceContext *, UINT, UINT, INT);
typedef HRESULT (STDMETHODCALLTYPE *FinishFn)(ID3D11DeviceContext *, BOOL, ID3D11CommandList **);
typedef void (STDMETHODCALLTYPE *ExecuteFn)(ID3D11DeviceContext *, ID3D11CommandList *, BOOL);
typedef HRESULT (STDMETHODCALLTYPE *DeferredFn)(ID3D11Device *, UINT, ID3D11DeviceContext **);
typedef HRESULT (STDMETHODCALLTYPE *Deferred1Fn)(ID3D11Device1 *, UINT, ID3D11DeviceContext1 **);
typedef HRESULT (WINAPI *CompileFn)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO *,ID3DInclude *,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob **,ID3DBlob **);
static DispatchFn original_dispatch;
static ExecuteFn original_execute;
static DeferredFn original_deferred;
static Deferred1Fn original_deferred1;
static RendererLog log_line;
static SRWLOCK maps_lock = SRWLOCK_INIT;
static MappedBuffer mappings[256];
static volatile LONG active_mappings;
static TaggedPixelShader matched_ps[16];
static volatile LONG matched_ps_count;
static TaggedComputeShader matched_cs[8];
static volatile LONG matched_cs_count;
static _Thread_local bool internal_call;
static ID3D11Device *render_device;
static ID3D11DeviceContext *render_context;
static ID3D11DeviceContext1 *context1;
static UINT pipeline_uav_count=D3D11_PS_CS_UAV_REGISTER_COUNT;
static ID3D11ComputeShader *prepare_cs, *output_cs;
static ID3D11PixelShader *motion_ps;
static HMODULE shader_compiler;
static ID3D11ShaderResourceView *scene_color, *scene_depth;
static float frame_globals[192];
static bool has_globals, has_frame, backend_failed, reset_history = true;
static _Atomic bool enabled = true;
static bool toggle_held;
static bool ready;
static wchar_t runtime_dir[MAX_PATH], data_dir[MAX_PATH];
static Surface input_color, input_depth, input_motion, dlss_output;
static UINT input_width, input_height, output_width, output_height;
static ElexNgx *ngx;
static unsigned long long frame_index, last_evaluated_frame;
static DWORD last_evaluation_tick;
static float previous_camera[3];
static LONG object_corrections, fallback_reports, globals_reports, map_reports;

#define DROP(x) do { if (x) { IUnknown_Release((IUnknown *)(x)); (x)=NULL; } } while (0)

static bool hash_is(const void *data, SIZE_T size, const char *wanted)
{
    if (!data || size > 0xffffffff || size < 32 || memcmp(data,"DXBC",4)) return false;
    unsigned char digest[32]; BCRYPT_ALG_HANDLE algorithm = NULL;
    if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0) < 0) return false;
    NTSTATUS status = BCryptHash(algorithm,NULL,0,(PUCHAR)data,(ULONG)size,digest,sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if (status < 0) return false;
    char hex[65];
    for (int i=0;i<32;++i) snprintf(hex+2*i,3,"%02x",digest[i]);
    return strcmp(hex,wanted)==0;
}

void elex_pixel_created(ID3D11Device *device, const void *bytes, SIZE_T size, ID3D11PixelShader *shader)
{
    (void)device;
    if (!shader || internal_call) return;
    UINT tag = 0;
    if (size==1064 && hash_is(bytes,size,"6cf656adf6c91e3f91a7f595f1baf4c5d4cd767cea95c7baaf8ce4b330ef9e8b")) tag=TAG_CAMERA;
    if (size==464 && hash_is(bytes,size,"cbcb7b0a19c19e5bd06c24ad8b77eaac2732a88b0f252665cab6067a1764e8a0")) tag=TAG_OBJECT;
    if (size==2168 && hash_is(bytes,size,"b49842b8f1b69907035dc2d5eae1d6ea3bb3687bb5efb84f7a247c511ad914e1")) tag=TAG_NEIGHBOR;
    if (size==3624 && hash_is(bytes,size,"d1b75f7c3fec5ad501b229724cf46ff8feb5fdee0886ef1f7f23ea3fd7eb50ff")) tag=TAG_TONEMAP;
    if (tag) {
        ID3D11PixelShader_SetPrivateData(shader,&shader_key,sizeof(tag),&tag);
        for(LONG i=0;i<16;++i) {
            if(!matched_ps[i].shader||matched_ps[i].shader==shader) {
                matched_ps[i].tag=tag;
                matched_ps[i].shader=shader;
                if(i>=matched_ps_count)InterlockedExchange(&matched_ps_count,i+1);
                break;
            }
        }
        if(log_line)log_line("Matched engine pixel shader: role=%u size=%llu",tag,(unsigned long long)size);
    }
}

void elex_compute_created(const void *bytes, SIZE_T size, ID3D11ComputeShader *shader)
{
    if (!shader || internal_call) return;
    if (size==3484 && hash_is(bytes,size,"538bd7a73caa5bda7a45106190eb0f8e0cc82a1c9ff706e99c2a1dd88cd8eea2")) {
        UINT tag=TAG_RESOLVE;
        ID3D11ComputeShader_SetPrivateData(shader,&shader_key,sizeof(tag),&tag);
        for(LONG i=0;i<8;++i) {
            if(!matched_cs[i].shader||matched_cs[i].shader==shader) {
                matched_cs[i].tag=tag;
                matched_cs[i].shader=shader;
                if(i>=matched_cs_count)InterlockedExchange(&matched_cs_count,i+1);
                break;
            }
        }
        if(log_line)log_line("Matched SMAA T2x compute resolve; DLSS waits for valid frame inputs");
    }
}

static UINT pixel_tag(ID3D11DeviceContext *context, ID3D11PixelShader **shader)
{
    LONG count=matched_ps_count;
    if(!count){*shader=NULL;return 0;}
    ID3D11DeviceContext_PSGetShader(context,shader,NULL,NULL);
    if(!*shader)return 0;
    for(LONG i=0;i<count&&i<16;++i) {
        if(matched_ps[i].shader==*shader)return matched_ps[i].tag;
    }
    return 0;
}

static HRESULT STDMETHODCALLTYPE map_buffer(void *original_fn,ID3D11DeviceContext *ctx,ID3D11Resource *resource,
    UINT sub,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE *mapped)
{
    MapFn original=(MapFn)original_fn;
    if(!internal_call&&ready)elex_fg_render_start();
    HRESULT hr=original(ctx,resource,sub,type,flags,mapped);
    if (internal_call || FAILED(hr) || !mapped || sub || type==D3D11_MAP_READ) return hr;
    D3D11_RESOURCE_DIMENSION kind;
    ID3D11Resource_GetType(resource,&kind);
    if(kind!=D3D11_RESOURCE_DIMENSION_BUFFER)return hr;
    D3D11_BUFFER_DESC desc;
    ID3D11Buffer_GetDesc((ID3D11Buffer *)resource,&desc);
    if(!(desc.BindFlags&D3D11_BIND_CONSTANT_BUFFER)||desc.ByteWidth<752)return hr;
    if(InterlockedIncrement(&map_reports)<=3)log_line("Constant Map: context=%p type=%u bytes=%u",ctx,ID3D11DeviceContext_GetType(ctx),desc.ByteWidth);
    AcquireSRWLockExclusive(&maps_lock);
    for(UINT i=0;i<256;++i) if(!mappings[i].resource) {
        mappings[i]=(MappedBuffer){ctx,resource,mapped->pData,desc.ByteWidth};
        InterlockedIncrement(&active_mappings);
        break;
    }
    ReleaseSRWLockExclusive(&maps_lock);
    return hr;
}

static void STDMETHODCALLTYPE unmap_buffer(void *original_fn,ID3D11DeviceContext *ctx,ID3D11Resource *resource,UINT sub)
{
    if(!internal_call && !sub && active_mappings > 0) {
        void *mapped_data=NULL;
        UINT mapped_size=0;
        AcquireSRWLockExclusive(&maps_lock);
        for(UINT i=0;i<256;++i)if(mappings[i].resource==resource && mappings[i].context==ctx) {
            mapped_data=mappings[i].data;
            mapped_size=mappings[i].size<65536?mappings[i].size:65536;
            mappings[i].resource=NULL;
            InterlockedDecrement(&active_mappings);
            break;
        }
        ReleaseSRWLockExclusive(&maps_lock);
        if(mapped_data && mapped_size)
            ID3D11Resource_SetPrivateData(resource,&shadow_key,mapped_size,mapped_data);
    }
    UnmapFn original=(UnmapFn)original_fn;
    if(original)original(ctx,resource,sub);
}

static bool texture_desc(ID3D11View *view,D3D11_TEXTURE2D_DESC *desc)
{
    if(!view)return false;
    ID3D11Resource *resource=NULL;
    ID3D11View_GetResource(view,&resource);
    D3D11_RESOURCE_DIMENSION kind;
    ID3D11Resource_GetType(resource,&kind);
    if(kind==D3D11_RESOURCE_DIMENSION_TEXTURE2D)ID3D11Texture2D_GetDesc((ID3D11Texture2D *)resource,desc);
    DROP(resource);
    return kind==D3D11_RESOURCE_DIMENSION_TEXTURE2D;
}

static bool validate_globals_slice(const unsigned char *raw,UINT offset,float values[192])
{
    const float *f=(const float *)(raw+offset*16);
    if(!isfinite(f[34])||!isfinite(f[38])||f[152]<64||f[153]<32||f[152]>16384||f[153]>16384||
       fabsf(f[46]-1)>1e-5f||fabsf(f[47])>1e-5f||f[147]<=0||f[139]<=f[147]) {
        if(InterlockedIncrement(&globals_reports)<=5)log_line("Globals invalid: viewport=%.1f,%.1f P=%.6f,%.6f near/far=%.1f,%.1f",f[152],f[153],f[46],f[47],f[147],f[139]);
        return false;
    }
    float jx=(f[34]-f[82])*f[152]*.5f;
    float jy=-(f[38]-f[86])*f[153]*.5f;
    if(fabsf(jx)>.51f||fabsf(jy)>.51f)return false;
    memset(values,0,768);
    memcpy(values,f,752);
    return true;
}

static __declspec(noinline) bool read_globals_large(ID3D11Buffer *buffer,UINT size,UINT offset,float values[192])
{
    unsigned char raw[65536];
    HRESULT hr=ID3D11Buffer_GetPrivateData(buffer,&shadow_key,&size,raw);
    return SUCCEEDED(hr)&&validate_globals_slice(raw,offset,values);
}

static bool read_globals_from_buffer(ID3D11Buffer *buffer,UINT offset,UINT count,float values[192])
{
    if(!buffer)return false;
    UINT size=0;
    HRESULT hr=ID3D11Buffer_GetPrivateData(buffer,&shadow_key,&size,NULL);
    if(FAILED(hr)||offset>4096||offset*16+752>size||size>65536) {
        if(InterlockedIncrement(&globals_reports)<=5)log_line("Globals unavailable: hr=0x%08lx firstConstant=%u count=%u captured=%u",(unsigned long)hr,offset,count,size);
        return false;
    }
    if(size<=1024) {
        unsigned char raw[1024];
        hr=ID3D11Buffer_GetPrivateData(buffer,&shadow_key,&size,raw);
        return SUCCEEDED(hr)&&validate_globals_slice(raw,offset,values);
    }
    return read_globals_large(buffer,size,offset,values);
}

static void begin_camera(ID3D11DeviceContext *ctx)
{
    DROP(scene_color); DROP(scene_depth);
    ID3D11DeviceContext_PSGetShaderResources(ctx,0,1,&scene_depth);
    has_frame=scene_depth!=NULL; has_globals=false; ++frame_index;
}

static bool prepare_object(ID3D11DeviceContext *ctx,ObjectBinding *saved)
{
    if(!ready||!enabled)return false;
    ID3D11DeviceContext1 *ctx1=NULL;
    if(ctx==render_context&&context1){ctx1=context1;ID3D11DeviceContext1_AddRef(ctx1);}
    else if(FAILED(ID3D11DeviceContext_QueryInterface(ctx,&IID_ID3D11DeviceContext1,(void **)&ctx1)))return false;
    ID3D11Buffer *globals_buffer=NULL;
    UINT first=0,count=0;
    ID3D11DeviceContext1_VSGetConstantBuffers1(ctx1,0,1,&globals_buffer,&first,&count);
    if(!globals_buffer){DROP(ctx1);return false;}
    if(ctx==render_context) {
        if(!has_globals&&read_globals_from_buffer(globals_buffer,first,count,frame_globals))has_globals=true;
    } else {
        UINT existing=0;
        if(ID3D11DeviceContext_GetPrivateData(ctx,&frame_key,&existing,NULL)!=S_OK||existing!=sizeof(DeferredFrame)) {
            DeferredFrame frame;
            if(read_globals_from_buffer(globals_buffer,first,count,frame.globals))
                ID3D11DeviceContext_SetPrivateData(ctx,&frame_key,sizeof(frame),&frame);
        }
    }
    saved->ctx1=ctx1;
    internal_call=true;
    ID3D11DeviceContext1_PSGetConstantBuffers1(ctx1,13,1,&saved->buffer,&saved->first,&saved->count);
    ID3D11DeviceContext1_PSSetConstantBuffers1(ctx1,13,1,&globals_buffer,&first,&count);
    ID3D11DeviceContext_PSSetShader(ctx,motion_ps,NULL,0);
    internal_call=false;
    DROP(globals_buffer);
    return true;
}

static void after_object(ID3D11DeviceContext *ctx,ID3D11PixelShader *shader,ObjectBinding *saved)
{
    ID3D11DeviceContext_PSSetShader(ctx,shader,NULL,0);
    if(saved->ctx1) {
        ID3D11DeviceContext1_PSSetConstantBuffers1(saved->ctx1,13,1,&saved->buffer,&saved->first,&saved->count);
        DROP(saved->ctx1);
    }
    DROP(saved->buffer);
    if(InterlockedIncrement(&object_corrections)==1)log_line("Object motion normalized; engine camera jitter captured");
}

void elex_draw(ID3D11DeviceContext *ctx,ElexDrawFn original,UINT count,UINT start)
{
    if(internal_call||!ready||ctx!=render_context||ID3D11DeviceContext_GetType(ctx)!=D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        original(ctx,count,start);return;
    }
    ID3D11PixelShader *shader=NULL;UINT tag=pixel_tag(ctx,&shader);
    if(tag==TAG_CAMERA)begin_camera(ctx);
    if(tag==TAG_NEIGHBOR) {
        DROP(scene_color);
        ID3D11DeviceContext_PSGetShaderResources(ctx,1,1,&scene_color);
        if(ngx&&enabled&&!backend_failed&&has_frame&&has_globals&&scene_color) {
            DROP(shader);
            return;
        }
    }
    ObjectBinding saved={0};
    bool patched=tag==TAG_OBJECT&&prepare_object(ctx,&saved);
    original(ctx,count,start);
    if(patched)after_object(ctx,shader,&saved);
    DROP(shader);
}

static void STDMETHODCALLTYPE draw_indexed(void *original_fn,ID3D11DeviceContext *ctx,UINT count,UINT start,INT base)
{
    IndexedFn original=(IndexedFn)original_fn;
    if(internal_call||!ready) {
        original(ctx,count,start,base);return;
    }
    ID3D11PixelShader *shader=NULL;UINT tag=pixel_tag(ctx,&shader);
    if(tag==TAG_CAMERA&&ctx==render_context)begin_camera(ctx);
    ObjectBinding saved={0};
    bool patched=tag==TAG_OBJECT&&prepare_object(ctx,&saved);
    original(ctx,count,start,base);
    if(patched)after_object(ctx,shader,&saved);
    DROP(shader);
}

static HRESULT STDMETHODCALLTYPE finish_list(void *original_fn,ID3D11DeviceContext *ctx,BOOL restore,ID3D11CommandList **list)
{
    FinishFn original=(FinishFn)original_fn;
    DeferredFrame frame;UINT size=sizeof(frame);
    HRESULT captured=ID3D11DeviceContext_GetPrivateData(ctx,&frame_key,&size,&frame);
    HRESULT hr=original(ctx,restore,list);
    if(SUCCEEDED(hr)&&list&&*list&&SUCCEEDED(captured))
        ID3D11CommandList_SetPrivateData(*list,&frame_key,sizeof(frame),&frame);
    ID3D11DeviceContext_SetPrivateData(ctx,&frame_key,0,NULL);
    return hr;
}

static void STDMETHODCALLTYPE execute_list(ID3D11DeviceContext *ctx,ID3D11CommandList *list,BOOL restore)
{
    original_execute(ctx,list,restore);
    if(ctx==render_context&&list) {
        DeferredFrame frame;UINT size=sizeof(frame);
        if(SUCCEEDED(ID3D11CommandList_GetPrivateData(list,&frame_key,&size,&frame))) {
            memcpy(frame_globals,frame.globals,sizeof(frame_globals));has_globals=true;
        }
    }
}

static void release_surface(Surface *surface)
{ DROP(surface->uav);DROP(surface->srv);DROP(surface->texture); }

static bool make_surface(Surface *s,UINT w,UINT h,DXGI_FORMAT format)
{
    D3D11_TEXTURE2D_DESC d={0};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;
    d.Format=format;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    HRESULT hr=ID3D11Device_CreateTexture2D(render_device,&d,NULL,&s->texture);
    if(SUCCEEDED(hr))hr=ID3D11Device_CreateShaderResourceView(render_device,(ID3D11Resource *)s->texture,NULL,&s->srv);
    if(SUCCEEDED(hr))hr=ID3D11Device_CreateUnorderedAccessView(render_device,(ID3D11Resource *)s->texture,NULL,&s->uav);
    if(FAILED(hr)) {release_surface(s);log_line("DLSS resource allocation failed: 0x%08lx",(unsigned long)hr);return false;}
    return true;
}

static bool resize_surfaces(UINT w,UINT h,UINT ow,UINT oh,bool need_output)
{
    if(input_width==w&&input_height==h&&output_width==ow&&output_height==oh&&input_color.texture&&(!need_output||dlss_output.texture))return true;
    release_surface(&input_color);release_surface(&input_depth);release_surface(&input_motion);release_surface(&dlss_output);
    input_width=input_height=output_width=output_height=0;
    if(!make_surface(&input_color,w,h,DXGI_FORMAT_R8G8B8A8_UNORM)||
       !make_surface(&input_depth,w,h,DXGI_FORMAT_R32_FLOAT)||
       !make_surface(&input_motion,w,h,DXGI_FORMAT_R16G16_FLOAT)||
       (need_output&&!make_surface(&dlss_output,ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT)))return false;
    input_width=w;input_height=h;output_width=ow;output_height=oh;reset_history=true;
    return true;
}

static bool replace_resolve(ID3D11DeviceContext *ctx)
{
    if(!enabled||backend_failed||!has_frame||!has_globals||!scene_color||!scene_depth)return false;
    ID3D11ShaderResourceView *motion=NULL;
    ID3D11UnorderedAccessView *target=NULL;
    ID3D11Resource *hudless=NULL;
    ID3D11DeviceContext_CSGetShaderResources(ctx,0,1,&motion);
    ID3D11DeviceContext_CSGetUnorderedAccessViews(ctx,0,1,&target);
    D3D11_TEXTURE2D_DESC cd={0},dd={0},md={0},od={0};bool applied=false;
    if(!texture_desc((ID3D11View *)scene_color,&cd)||!texture_desc((ID3D11View *)scene_depth,&dd)||
       !texture_desc((ID3D11View *)motion,&md)||!texture_desc((ID3D11View *)target,&od))goto finish;
    if(cd.Width!=dd.Width||cd.Height!=dd.Height||cd.Width!=md.Width||cd.Height!=md.Height||
       cd.Width>od.Width||cd.Height>od.Height||cd.SampleDesc.Count!=1||dd.SampleDesc.Count!=1||
       cd.Width!=(UINT)frame_globals[152]||cd.Height!=(UINT)frame_globals[153])goto finish;
    ID3D11UnorderedAccessView_GetResource(target,&hudless);
    if(!hudless)goto finish;
    bool direct_output=(od.Format==DXGI_FORMAT_R8G8B8A8_UNORM||od.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)&&
        (od.BindFlags&D3D11_BIND_UNORDERED_ACCESS)!=0;
    float jx=(frame_globals[34]-frame_globals[82])*cd.Width*.5f;
    float jy=-(frame_globals[38]-frame_globals[86])*cd.Height*.5f;
    internal_call=true;
    ElexPipelineState saved;
    elex_pipeline_save(ctx,context1,pipeline_uav_count,&saved);
    ID3D11DeviceContext_OMSetRenderTargets(ctx,0,NULL,NULL);
    if(!ngx)ngx=elex_ngx_open(render_device,runtime_dir,data_dir,log_line);
    if(!ngx){backend_failed=true;elex_engine_set_enabled(false);goto restore;}
    if(!resize_surfaces(cd.Width,cd.Height,od.Width,od.Height,!direct_output)||
       !elex_ngx_size(ngx,ctx,cd.Width,cd.Height,od.Width,od.Height))goto restore;
    ID3D11ShaderResourceView *inputs[3]={scene_color,scene_depth,motion};
    ID3D11UnorderedAccessView *outputs[3]={input_color.uav,input_depth.uav,input_motion.uav};
    ID3D11DeviceContext_CSSetShader(ctx,prepare_cs,NULL,0);
    ID3D11DeviceContext_CSSetShaderResources(ctx,0,3,inputs);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx,0,3,outputs,NULL);
    original_dispatch(ctx,(cd.Width+7)/8,(cd.Height+7)/8,1);
    ID3D11ShaderResourceView *null_srvs[3]={NULL,NULL,NULL};
    ID3D11UnorderedAccessView *null_uavs[3]={NULL,NULL,NULL};
    ID3D11DeviceContext_CSSetShaderResources(ctx,0,3,null_srvs);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx,0,3,null_uavs,NULL);
    DWORD now=GetTickCount();
    bool reset=reset_history||frame_index!=last_evaluated_frame+1||now-last_evaluation_tick>500;
    float distance=0;
    for(UINT i=0;i<3;++i){float delta=frame_globals[140+i]-previous_camera[i];distance+=delta*delta;}
    if(distance>25000000.f)reset=true;
    float milliseconds=frame_globals[162]*1000.f;
    if(!isfinite(milliseconds)||milliseconds<=0||milliseconds>1000)milliseconds=16.6667f;
    ID3D11Resource *eval_output=direct_output?hudless:(ID3D11Resource *)dlss_output.texture;
    if(!elex_ngx_evaluate(ngx,ctx,(ID3D11Resource *)input_color.texture,(ID3D11Resource *)input_depth.texture,
        (ID3D11Resource *)input_motion.texture,eval_output,jx,jy,milliseconds,reset))goto restore;
    if(!direct_output) {
        ID3D11DeviceContext_CSSetShader(ctx,output_cs,NULL,0);
        ID3D11DeviceContext_CSSetShaderResources(ctx,0,1,&dlss_output.srv);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx,0,1,&target,NULL);
        original_dispatch(ctx,(od.Width+7)/8,(od.Height+7)/8,1);
    }
    if(elex_fg_is_active())
        elex_fg_capture(ctx,input_depth.texture,input_motion.texture,hudless,frame_globals,reset);
    reset_history=false;last_evaluation_tick=now;last_evaluated_frame=frame_index;
    memcpy(previous_camera,frame_globals+140,sizeof(previous_camera));
    applied=true;
restore:
    elex_pipeline_restore(ctx,context1,&saved);
    internal_call=false;
finish:
    DROP(hudless);DROP(motion);DROP(target);
    if(!applied)reset_history=true;
    return applied;
}

static void STDMETHODCALLTYPE dispatch(ID3D11DeviceContext *ctx,UINT x,UINT y,UINT z)
{
    if(internal_call||!ready||ctx!=render_context||ID3D11DeviceContext_GetType(ctx)!=D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        original_dispatch(ctx,x,y,z);return;
    }
    LONG cs_count=matched_cs_count;
    if(!cs_count){original_dispatch(ctx,x,y,z);return;}
    ID3D11ComputeShader *shader=NULL;UINT tag=0;
    ID3D11DeviceContext_CSGetShader(ctx,&shader,NULL,NULL);
    if(shader) {
        for(LONG i=0;i<cs_count&&i<8;++i) {
            if(matched_cs[i].shader==shader){tag=matched_cs[i].tag;break;}
        }
    }
    DROP(shader);
    if(tag==TAG_RESOLVE) {
        DWORD foreground_pid=0;
        GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
        bool down=foreground_pid==GetCurrentProcessId()&&(GetAsyncKeyState(VK_F10)&0x8000)!=0;
        if(down&&!toggle_held) {
            enabled=!enabled;elex_engine_set_enabled(enabled);reset_history=true;
            log_line("DLSS %s",enabled?"enabled":"disabled");
        }
        toggle_held=down;
        if(replace_resolve(ctx))return;
        if(enabled&&InterlockedIncrement(&fallback_reports)<=3)
            log_line("Engine resolve retained: depth=%d globals=%d raw_color=%d backend_failed=%d",
                has_frame,has_globals,scene_color!=NULL,backend_failed);
    }
    original_dispatch(ctx,x,y,z);
}

static HRESULT compile_shader(const char *text,const char *profile,ID3DBlob **code)
{
    // Returned ID3DBlob vtables belong to the compiler DLL. Keep it loaded
    // until callers have consumed and released every blob.
    if(!shader_compiler)shader_compiler=LoadLibraryExW(L"d3dcompiler_47.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!shader_compiler)return HRESULT_FROM_WIN32(GetLastError());
    CompileFn compile=(CompileFn)GetProcAddress(shader_compiler,"D3DCompile");
    ID3DBlob *errors=NULL;
    HRESULT hr=compile?compile(text,strlen(text),"ELEX-DLSS",NULL,NULL,"main",profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code,&errors):E_NOINTERFACE;
    if(errors){log_line("Shader compiler: %s",(char *)ID3D10Blob_GetBufferPointer(errors));DROP(errors);}
    return hr;
}

static HRESULT shaders_init(ID3D11Device *device)
{
    ID3DBlob *code=NULL;HRESULT hr=compile_shader(shader_prepare,"cs_5_0",&code);
    if(SUCCEEDED(hr))hr=ID3D11Device_CreateComputeShader(device,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&prepare_cs);
    DROP(code);if(FAILED(hr))return hr;
    hr=compile_shader(shader_output,"cs_5_0",&code);
    if(SUCCEEDED(hr))hr=ID3D11Device_CreateComputeShader(device,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&output_cs);
    DROP(code);if(FAILED(hr))return hr;
    hr=compile_shader(shader_object_motion,"ps_5_0",&code);
    if(SUCCEEDED(hr))hr=ID3D11Device_CreatePixelShader(device,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&motion_ps);
    DROP(code);if(FAILED(hr))return hr;
    return S_OK;
}

static HRESULT hook(void *target,void *replacement,void **original)
{
    MH_STATUS status=MH_CreateHook(target,replacement,original);
    if(status!=MH_OK){log_line("DLSS hook creation: %s",MH_StatusToString(status));return E_FAIL;}
    status=MH_EnableHook(target);
    return status==MH_OK?S_OK:E_FAIL;
}

static HRESULT hook_context(ID3D11DeviceContext *ctx)
{
    HRESULT hr=elex_hook_graphics((void *)ctx->lpVtbl->Map,ELEX_HOOK_MAP,(void *)map_buffer);
    if(SUCCEEDED(hr))hr=elex_hook_graphics((void *)ctx->lpVtbl->Unmap,ELEX_HOOK_UNMAP,(void *)unmap_buffer);
    if(SUCCEEDED(hr))hr=elex_hook_graphics((void *)ctx->lpVtbl->DrawIndexed,ELEX_HOOK_INDEXED,(void *)draw_indexed);
    if(SUCCEEDED(hr)&&ID3D11DeviceContext_GetType(ctx)==D3D11_DEVICE_CONTEXT_DEFERRED)
        hr=elex_hook_graphics((void *)ctx->lpVtbl->FinishCommandList,ELEX_HOOK_FINISH,(void *)finish_list);
    return hr;
}

static HRESULT STDMETHODCALLTYPE create_deferred(ID3D11Device *device,UINT flags,ID3D11DeviceContext **out)
{
    HRESULT hr=original_deferred(device,flags,out);
    if(SUCCEEDED(hr)&&out&&*out) {
        HRESULT hook_hr=hook_context(*out);
        log_line("Deferred context hooked: %p result=0x%08lx",*out,(unsigned long)hook_hr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE create_deferred1(ID3D11Device1 *device,UINT flags,ID3D11DeviceContext1 **out)
{
    HRESULT hr=original_deferred1(device,flags,out);
    if(SUCCEEDED(hr)&&out&&*out) {
        HRESULT hook_hr=hook_context((ID3D11DeviceContext *)*out);
        log_line("Deferred context1 hooked: %p result=0x%08lx",*out,(unsigned long)hook_hr);
    }
    return hr;
}

HRESULT elex_renderer_init(ID3D11Device *device,RendererLog logger,HMODULE module)
{
    if(ready||render_device)return S_OK;
    logger("DLSS device candidate: %p feature=0x%x flags=0x%x",device,ID3D11Device_GetFeatureLevel(device),ID3D11Device_GetCreationFlags(device));
    if(ID3D11Device_GetFeatureLevel(device)<D3D_FEATURE_LEVEL_11_0){logger("Skipping non-D3D11-capable probe device");return S_FALSE;}
    log_line=logger;
    UINT len=GetModuleFileNameW(module,runtime_dir,MAX_PATH);
    if(!len||len>=MAX_PATH)return E_FAIL;
    wchar_t *slash=wcsrchr(runtime_dir,L'\\');if(!slash)return E_FAIL;*slash=0;
    if(wcslen(runtime_dir)+12>=MAX_PATH)return E_FAIL;
    wcscpy(data_dir,runtime_dir);wcscat(data_dir,L"\\elex-dlss");CreateDirectoryW(data_dir,NULL);
    wchar_t ini[MAX_PATH];
    if(swprintf(ini,MAX_PATH,L"%ls\\elex-dlss.ini",runtime_dir)<0)return E_FAIL;
    HRESULT indicator=elex_indicator_install(ini,logger);
    if(FAILED(indicator))logger("Native DLSS indicator unavailable: 0x%08lx",(unsigned long)indicator);
    render_device=device;ID3D11Device_AddRef(device);
    ID3D11Device1 *device1=NULL;ID3D11DeviceContext *ctx=NULL;
    ID3D11Device_GetImmediateContext(device,&ctx);
    render_context=ctx;ID3D11DeviceContext_AddRef(render_context);
    HRESULT hr=ID3D11Device_QueryInterface(device,&IID_ID3D11Device1,(void **)&device1);
    if(SUCCEEDED(hr))hr=ID3D11DeviceContext_QueryInterface(ctx,&IID_ID3D11DeviceContext1,(void **)&context1);
    D3D_FEATURE_LEVEL level=ID3D11Device_GetFeatureLevel(device);
    pipeline_uav_count=level>=D3D_FEATURE_LEVEL_11_1?D3D11_1_UAV_SLOT_COUNT:D3D11_PS_CS_UAV_REGISTER_COUNT;
    internal_call=true;
    if(SUCCEEDED(hr))hr=shaders_init(device);
    internal_call=false;
    if(FAILED(hr)){
        logger("DLSS device preparation failed: 0x%08lx; later devices may retry",(unsigned long)hr);
        DROP(device1);DROP(ctx);elex_renderer_shutdown();return hr;
    }
    if(SUCCEEDED(hr))hr=hook_context(ctx);
    if(SUCCEEDED(hr))hr=hook((void *)ctx->lpVtbl->ExecuteCommandList,(void *)execute_list,(void **)&original_execute);
    if(SUCCEEDED(hr))hr=hook((*(void ***)ctx)[41],(void *)dispatch,(void **)&original_dispatch);
    if(SUCCEEDED(hr))hr=hook((void *)device->lpVtbl->CreateDeferredContext,(void *)create_deferred,(void **)&original_deferred);
    if(SUCCEEDED(hr))hr=hook((void *)device1->lpVtbl->CreateDeferredContext1,(void *)create_deferred1,(void **)&original_deferred1);
    // Probe a deferred context now so the shared implementation is hooked even
    // when the engine has created its worker contexts before renderer startup.
    ID3D11DeviceContext *deferred=NULL;
    if(SUCCEEDED(hr)) {
        HRESULT probe=original_deferred(device,0,&deferred);
        if(SUCCEEDED(probe)&&deferred)hr=hook_context(deferred);
        DROP(deferred);
    }
    DROP(device1);DROP(ctx);
    ready=SUCCEEDED(hr);
    if(ready)elex_engine_hooks(module,logger);
    if(ready&&elex_engine_frame_hook_ready()) {
        HRESULT fg=elex_fg_install(device,logger,module);
        if(FAILED(fg))logger("Frame-generation bridge initialization failed: 0x%08lx; Super Resolution retained",(unsigned long)fg);
    }
    logger("ELEX DLSS renderer %s: 0x%08lx. F10 toggles DLSS at the temporal pass.",ready?"ready":"unavailable",(unsigned long)hr);
    return hr;
}

void elex_renderer_shutdown(void)
{
    // Explicit process-exit/test cleanup only; never call into drivers in DllMain.
    ready=false;elex_ngx_close(ngx);ngx=NULL;
    InterlockedExchange(&matched_ps_count,0);memset(matched_ps,0,sizeof(matched_ps));
    InterlockedExchange(&matched_cs_count,0);memset(matched_cs,0,sizeof(matched_cs));
    InterlockedExchange(&active_mappings,0);memset(mappings,0,sizeof(mappings));
    release_surface(&input_color);release_surface(&input_depth);release_surface(&input_motion);release_surface(&dlss_output);
    DROP(scene_color);DROP(scene_depth);DROP(motion_ps);DROP(prepare_cs);DROP(output_cs);
    DROP(context1);DROP(render_context);DROP(render_device);
    if(shader_compiler){FreeLibrary(shader_compiler);shader_compiler=NULL;}
}
