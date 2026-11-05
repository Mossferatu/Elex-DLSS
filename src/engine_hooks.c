#include "engine_hooks.h"
#include "frame_generation.h"
#include "../third_party/minhook/include/MinHook.h"
#include <bcrypt.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <wchar.h>
#include <stdatomic.h>

typedef bool (*GetFloatFn)(void *, const char *const *, float *, const float *);
typedef bool (*DependencyFn)(void *, int, void *, void *, void *, void *);
typedef void *(*FrameOwnerFn)(void);
typedef unsigned (*FrameNumberFn)(void *);
typedef void (*FrameTickFn)(void *);
static GetFloatFn original_float;
static DependencyFn original_dependency;
static FrameOwnerFn frame_owner;
static FrameNumberFn frame_number;
static FrameTickFn original_tick;
static bool frame_hook_ready;
static float *jitter;
static uintptr_t smaa_vtable;
static RendererLog log_line;
static float render_scale = 1;
static _Atomic bool active = true;
static LONG scale_logged, jitter_logged, dependency_logged;

static void frame_tick(void *timer)
{
    unsigned next=*(const unsigned *)((const unsigned char *)timer+0x54)+1;
    elex_fg_simulation_start(next);
    original_tick(timer);
}

float elex_engine_scale(void) { return render_scale; }
bool elex_engine_frame_hook_ready(void) { return frame_hook_ready; }
void elex_engine_set_enabled(bool value) { active = value; }

static float halton(unsigned index, unsigned base)
{
    float result=0,weight=1;
    while(index){weight/=base;result+=(index%base)*weight;index/=base;}
    return result;
}

static bool get_float(void *self,const char *const *key,float *out,const float *fallback)
{
    bool found=original_float(self,key,out,fallback);
    if(active&&key&&*key&&out) {
        if(strcmp(*key,"/Engine/Render/VirtualScreenScale")==0) {
            *out=render_scale;
            if(InterlockedIncrement(&scale_logged)==1)log_line("Engine render scale: %.6f",render_scale);
            return true;
        }
        if(render_scale<1&&strcmp(*key,"/Engine/Render/MipMapBias")==0) {
            *out=log2f(render_scale);return true;
        }
    }
    return found;
}

static bool dependency(void *self,int index,void *a,void *b,void *c,void *d)
{
    if(InterlockedIncrement(&dependency_logged)<=5)log_line("Engine dependency: vtable=%p expected=%p index=%d",self?*(void **)self:NULL,(void *)smaa_vtable,index);
    if(active&&self&&*(uintptr_t *)self==smaa_vtable&&index<=0) {
        unsigned frame=frame_number(frame_owner());
        unsigned phases=(unsigned)ceilf(16.f/(render_scale*render_scale));
        unsigned phase=frame%phases+1;
        // Engine applies these as +/-value/viewport to clip space, i.e. half
        // the value in raster pixels. Both raster axes are top-left positive.
        jitter[0]=2*(halton(phase,2)-.5f);
        jitter[1]=2*(halton(phase,3)-.5f);
        if(InterlockedIncrement(&jitter_logged)==1)
            log_line("Engine Halton jitter active: %u phases, frame=%u",phases,frame);
    }
    return original_dependency(self,index,a,b,c,d);
}

static bool exact_executable(const wchar_t *path)
{
    static const unsigned char expected[32]={
        0x7f,0xe4,0xf4,0xc5,0xa4,0x63,0xf7,0xb4,0x28,0xef,0x74,0xd8,0xfe,0x07,0x44,0x5f,
        0x73,0x4f,0xf0,0xdc,0x0a,0xd1,0x00,0x5e,0xd5,0x0c,0xab,0x34,0xbc,0x7f,0x0f,0xd5};
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;
    unsigned char buffer[65536],digest[32];DWORD count=0;bool ok=false;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto end;
    if(BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)<0)goto end;
    for(;;){
        if(!ReadFile(file,buffer,sizeof(buffer),&count,NULL))goto end;
        if(!count)break;
        if(BCryptHashData(hash,buffer,count,0)<0)goto end;
    }
    ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0&&!memcmp(digest,expected,32);
end:
    if(hash)BCryptDestroyHash(hash);
    if(alg)BCryptCloseAlgorithmProvider(alg,0);
    CloseHandle(file);return ok;
}

bool elex_engine_hooks(HMODULE module,RendererLog logger)
{
    log_line=logger;
    wchar_t exe[MAX_PATH],ini[MAX_PATH],mode[32];
    DWORD length=GetModuleFileNameW(NULL,exe,MAX_PATH);
    if(!length||length>=MAX_PATH||!exact_executable(exe)) {
        logger("Engine hooks disabled: executable fingerprint differs (normal for capture replay)");return false;
    }
    length=GetModuleFileNameW(module,ini,MAX_PATH);
    if(!length||length>=MAX_PATH)return false;
    wchar_t *slash=wcsrchr(ini,L'\\');if(!slash||(slash-ini)+16>=MAX_PATH)return false;
    wcscpy(slash+1,L"elex-dlss.ini");
    GetPrivateProfileStringW(L"DLSS",L"Mode",L"DLAA",mode,32,ini);
    if(_wcsicmp(mode,L"Quality")==0)render_scale=2.f/3.f;
    else if(_wcsicmp(mode,L"Balanced")==0)render_scale=.58f;
    else if(_wcsicmp(mode,L"Performance")==0)render_scale=.5f;
    else render_scale=1;
    unsigned char *base=(unsigned char *)GetModuleHandleW(NULL);
    // Full on-disk fingerprint plus exact entry signatures protect the known
    // RVAs; all pointers are rebased for ASLR and unrelated builds fail closed.
    static const unsigned char float_head[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    static const unsigned char dependency_head[]={0x40,0x53,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xb0,0,0,0};
    if(memcmp(base+0x47ca0,float_head,sizeof(float_head))||memcmp(base+0x1724d0,dependency_head,sizeof(dependency_head))) {
        logger("Engine hooks disabled: entry signature mismatch");render_scale=1;return false;
    }
    smaa_vtable=(uintptr_t)(base+0xb25ed8);jitter=(float *)(base+0x116c71c);
    frame_owner=(FrameOwnerFn)(base+0x51d40);frame_number=(FrameNumberFn)(base+0x51b10);
    MH_STATUS result=MH_CreateHook(base+0x47ca0,(void *)get_float,(void **)&original_float);
    if(result!=MH_OK)return false;
    result=MH_CreateHook(base+0x1724d0,(void *)dependency,(void **)&original_dependency);
    if(result!=MH_OK){MH_RemoveHook(base+0x47ca0);return false;}
    MH_QueueEnableHook(base+0x47ca0);MH_QueueEnableHook(base+0x1724d0);
    static const unsigned char tick_head[]={0xff,0x41,0x54,0xc3,0xcc};
    if(!memcmp(base+0x51cb0,tick_head,sizeof(tick_head))) {
        MH_STATUS tick_status=MH_CreateHook(base+0x51cb0,(void *)frame_tick,(void **)&original_tick);
        if(tick_status==MH_OK){MH_QueueEnableHook(base+0x51cb0);frame_hook_ready=true;}
        else logger("Reflex frame-boundary hook unavailable: %s",MH_StatusToString(tick_status));
    }
    result=MH_ApplyQueued();
    frame_hook_ready=frame_hook_ready&&result==MH_OK;
    logger("Reflex engine frame-boundary hook: %s",frame_hook_ready?"ready":"unavailable");
    logger("Engine hooks: %s, mode=%ls scale=%.6f",MH_StatusToString(result),mode,render_scale);
    return result==MH_OK;
}
