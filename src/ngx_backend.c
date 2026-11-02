#define COBJMACROS
#include "ngx_backend.h"
#include "../build/version.h"
#include <stdio.h>
#include <wchar.h>
#include <math.h>

typedef NVSDK_NGX_Result (NVSDK_CONV *InitProjectFn)(const char *, NVSDK_NGX_EngineType,
    const char *, const wchar_t *, ID3D11Device *, NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo *);
typedef NVSDK_NGX_Result (NVSDK_CONV *ParamsFn)(NVSDK_NGX_Parameter **);
typedef NVSDK_NGX_Result (NVSDK_CONV *DestroyFn)(NVSDK_NGX_Parameter *);
typedef NVSDK_NGX_Result (NVSDK_CONV *CreateFn)(ID3D11DeviceContext *, NVSDK_NGX_Feature,
    const NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
typedef NVSDK_NGX_Result (NVSDK_CONV *EvalFn)(ID3D11DeviceContext *, const NVSDK_NGX_Handle *,
    const NVSDK_NGX_Parameter *, PFN_NVSDK_NGX_ProgressCallback_C);
typedef NVSDK_NGX_Result (NVSDK_CONV *ReleaseFn)(NVSDK_NGX_Handle *);
typedef NVSDK_NGX_Result (NVSDK_CONV *ShutdownFn)(void);

struct ElexNgx {
    HMODULE core;
    NVSDK_NGX_Parameter *params;
    NVSDK_NGX_Handle *feature;
    DestroyFn destroy;
    CreateFn create;
    EvalFn evaluate;
    ReleaseFn release;
    ShutdownFn shutdown;
    ElexLog log;
    UINT width, height, out_width, out_height;
    bool initialized;
    unsigned long long evaluations;
};
static ElexLog ngx_logger;
static void NVSDK_CONV driver_log(const char *message, NVSDK_NGX_Logging_Level level, NVSDK_NGX_Feature feature)
{
    (void)level; (void)feature;
    if (ngx_logger) ngx_logger("NGX %s", message);
}

void elex_ngx_close(ElexNgx *n)
{
    if (!n) return;
    if (n->feature && n->release) n->release(n->feature);
    if (n->params && n->destroy) n->destroy(n->params);
    if (n->initialized && n->shutdown) n->shutdown();
    if (n->core) FreeLibrary(n->core);
    HeapFree(GetProcessHeap(), 0, n);
}

ElexNgx *elex_ngx_open(ID3D11Device *device, const wchar_t *runtime_dir,
                     const wchar_t *data_dir, ElexLog log)
{
    ElexNgx *n = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*n));
    if (!n) return NULL;
    n->log = log; ngx_logger = log;
    wchar_t path[32768]; DWORD bytes = sizeof(path);
    LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore",
        L"FullPath", RRF_RT_REG_SZ, NULL, path, &bytes);
    if (status != ERROR_SUCCESS || wcslen(path) + 12 >= 32768) {
        log("DLSS unavailable: NVIDIA NGX driver path (%ld)", status); goto fail;
    }
    wcscat(path, L"\\_nvngx.dll");
    n->core = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!n->core) { log("NGX core load failed: %lu", GetLastError()); goto fail; }
#define FN(type,name,symbol) type name = (type)GetProcAddress(n->core,symbol); if (!name) goto fail
    FN(InitProjectFn, init, "NVSDK_NGX_D3D11_Init_ProjectID");
    FN(ParamsFn, get_caps, "NVSDK_NGX_D3D11_GetCapabilityParameters");
    FN(ParamsFn, allocate, "NVSDK_NGX_D3D11_AllocateParameters");
    n->destroy = (DestroyFn)GetProcAddress(n->core, "NVSDK_NGX_D3D11_DestroyParameters");
    n->create = (CreateFn)GetProcAddress(n->core, "NVSDK_NGX_D3D11_CreateFeature");
    n->evaluate = (EvalFn)GetProcAddress(n->core, "NVSDK_NGX_D3D11_EvaluateFeature");
    n->release = (ReleaseFn)GetProcAddress(n->core, "NVSDK_NGX_D3D11_ReleaseFeature");
    n->shutdown = (ShutdownFn)GetProcAddress(n->core, "NVSDK_NGX_D3D11_Shutdown");
    if (!n->destroy || !n->create || !n->evaluate || !n->release || !n->shutdown) goto fail;
    const wchar_t *paths[] = {runtime_dir};
    NVSDK_NGX_FeatureCommonInfo common = {0};
    common.PathListInfo.Path = paths; common.PathListInfo.Length = 1;
    common.LoggingInfo.LoggingCallback = driver_log;
    common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    common.LoggingInfo.DisableOtherLoggingSinks = true;
    NVSDK_NGX_Result result = init("f350c55b-833b-46f2-9234-f451d710b2cb", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "Elex-DLSS-" ELEX_DLSS_VERSION, data_dir, device, NVSDK_NGX_Version_API, &common);
    log("NGX initialization: 0x%08x", result);
    if (NVSDK_NGX_FAILED(result)) goto fail;
    n->initialized = true;
    NVSDK_NGX_Parameter *caps = NULL;
    result = get_caps(&caps);
    if (NVSDK_NGX_FAILED(result) || !caps) goto fail;
    int available = 0;
    result = NVSDK_NGX_Parameter_GetI(caps, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    n->destroy(caps);
    if (NVSDK_NGX_FAILED(result) || !available) { log("DLSS not supported: 0x%08x", result); goto fail; }
    result = allocate(&n->params);
    if (NVSDK_NGX_FAILED(result)) goto fail;
    return n;
fail:
    elex_ngx_close(n);
    return NULL;
}

bool elex_ngx_size(ElexNgx *n, ID3D11DeviceContext *context, UINT w, UINT h, UINT ow, UINT oh)
{
    if (!n || !w || !h || w > ow || h > oh) return false;
    if (n->feature && n->width == w && n->height == h && n->out_width == ow && n->out_height == oh) return true;
    if (n->feature) { n->release(n->feature); n->feature = NULL; }
    NVSDK_NGX_Parameter_SetUI(n->params, NVSDK_NGX_Parameter_Width, w);
    NVSDK_NGX_Parameter_SetUI(n->params, NVSDK_NGX_Parameter_Height, h);
    NVSDK_NGX_Parameter_SetUI(n->params, NVSDK_NGX_Parameter_OutWidth, ow);
    NVSDK_NGX_Parameter_SetUI(n->params, NVSDK_NGX_Parameter_OutHeight, oh);
    NVSDK_NGX_Parameter_SetI(n->params, NVSDK_NGX_Parameter_PerfQualityValue,
        w == ow && h == oh ? NVSDK_NGX_PerfQuality_Value_DLAA :
        (float)w/ow < .54f ? NVSDK_NGX_PerfQuality_Value_MaxPerf :
        (float)w/ow < .63f ? NVSDK_NGX_PerfQuality_Value_Balanced : NVSDK_NGX_PerfQuality_Value_MaxQuality);
    // Input is the engine's tone-mapped SDR scene, not its linear HDR buffer.
    NVSDK_NGX_Parameter_SetI(n->params, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes);
    NVSDK_NGX_Result result = n->create(context, NVSDK_NGX_Feature_SuperSampling, n->params, &n->feature);
    n->log("DLSS create %ux%u -> %ux%u: 0x%08x", w, h, ow, oh, result);
    if (NVSDK_NGX_FAILED(result)) return false;
    n->width = w; n->height = h; n->out_width = ow; n->out_height = oh; n->evaluations = 0;
    return true;
}

bool elex_ngx_evaluate(ElexNgx *n, ID3D11DeviceContext *context, ID3D11Resource *color,
    ID3D11Resource *depth, ID3D11Resource *motion, ID3D11Resource *output,
    float jx, float jy, float milliseconds, bool reset)
{
    if (!n || !n->feature || !isfinite(jx) || !isfinite(jy) || fabsf(jx) > .51f || fabsf(jy) > .51f) return false;
    NVSDK_NGX_Parameter *p = n->params;
    NVSDK_NGX_Parameter_SetD3d11Resource(p, NVSDK_NGX_Parameter_Color, color);
    NVSDK_NGX_Parameter_SetD3d11Resource(p, NVSDK_NGX_Parameter_Depth, depth);
    NVSDK_NGX_Parameter_SetD3d11Resource(p, NVSDK_NGX_Parameter_MotionVectors, motion);
    NVSDK_NGX_Parameter_SetD3d11Resource(p, NVSDK_NGX_Parameter_Output, output);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_Jitter_Offset_X, jx);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_Jitter_Offset_Y, jy);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_MV_Scale_X, -(float)n->width);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_MV_Scale_Y, -(float)n->height);
    NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_Reset, reset || !n->evaluations);
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, n->width);
    NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, n->height);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1);
    NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, milliseconds);
    NVSDK_NGX_Result result = n->evaluate(context, n->feature, p, NULL);
    if (NVSDK_NGX_FAILED(result)) { n->log("DLSS evaluate failed: 0x%08x; using engine resolve", result); return false; }
    ++n->evaluations;
    if (n->evaluations == 1 || n->evaluations % 600 == 0)
        n->log("DLSS EVALUATED %llu: %ux%u -> %ux%u jitter=(%.5f,%.5f) reset=%d",
            n->evaluations, n->width, n->height, n->out_width, n->out_height, jx, jy, reset);
    return true;
}
