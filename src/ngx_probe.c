/* Standalone hardware probe. This executable does not load or modify ELEX. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <wchar.h>
#include "nvsdk_ngx.h"

/* Driver-core ABI, analogous to NVIDIA's standalone CUDA loader. The public
 * SDK's parameter wrappers below preserve the Microsoft C++ object ABI. */
typedef NVSDK_NGX_Result (NVSDK_CONV *InitProjectFn)(const char *, NVSDK_NGX_EngineType,
    const char *, const wchar_t *, ID3D11Device *, NVSDK_NGX_Version,
    const NVSDK_NGX_FeatureCommonInfo *);
typedef NVSDK_NGX_Result (NVSDK_CONV *ParamsFn)(NVSDK_NGX_Parameter **);
typedef NVSDK_NGX_Result (NVSDK_CONV *DestroyFn)(NVSDK_NGX_Parameter *);
typedef NVSDK_NGX_Result (NVSDK_CONV *CreateFn)(ID3D11DeviceContext *, NVSDK_NGX_Feature,
    const NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
typedef NVSDK_NGX_Result (NVSDK_CONV *EvalFn)(ID3D11DeviceContext *, const NVSDK_NGX_Handle *,
    const NVSDK_NGX_Parameter *, PFN_NVSDK_NGX_ProgressCallback_C);
typedef NVSDK_NGX_Result (NVSDK_CONV *ReleaseFn)(NVSDK_NGX_Handle *);
typedef NVSDK_NGX_Result (NVSDK_CONV *ShutdownFn)(void);

static void NVSDK_CONV ngx_log(const char *message, NVSDK_NGX_Logging_Level level,
                              NVSDK_NGX_Feature feature)
{
    (void)level; (void)feature;
    fprintf(stderr, "NGX: %s\n", message);
}

static HRESULT texture(ID3D11Device *device, ID3D11DeviceContext *context,
    UINT width, UINT height, DXGI_FORMAT format, const float clear[4], ID3D11Texture2D **out)
{
    D3D11_TEXTURE2D_DESC desc = {0};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
    HRESULT hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, out);
    if (FAILED(hr)) return hr;
    ID3D11RenderTargetView *rtv = NULL;
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)*out, NULL, &rtv);
    if (SUCCEEDED(hr)) {
        ID3D11DeviceContext_ClearRenderTargetView(context, rtv, clear);
        ID3D11RenderTargetView_Release(rtv);
    }
    return hr;
}

static float half_float(unsigned short h)
{
    float value = (h & 1023) / 1024.0f;
    unsigned exponent = (h >> 10) & 31;
    if (exponent == 31) return NAN;
    value = exponent ? ldexpf(1.0f + value, (int)exponent - 15) : ldexpf(value, -14);
    return (h & 32768) ? -value : value;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3) {
        fputs("Usage: ngx-probe.exe <official-DLSS-runtime-directory> <scratch-directory>\n", stderr);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    int exit_code = 1, initialized = 0;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *color = NULL, *depth = NULL, *motion = NULL, *output = NULL, *staging = NULL;
    NVSDK_NGX_Parameter *caps = NULL, *params = NULL;
    NVSDK_NGX_Handle *feature = NULL;
    HMODULE core = NULL;
    DestroyFn destroy = NULL; ReleaseFn release = NULL; ShutdownFn shutdown = NULL;
    wchar_t path[32768]; DWORD bytes = sizeof(path);
    LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore",
        L"FullPath", RRF_RT_REG_SZ, NULL, path, &bytes);
    if (status != ERROR_SUCCESS || wcslen(path) + 12 >= 32768) {
        fprintf(stderr, "NGX driver path unavailable: %ld\n", status); goto cleanup;
    }
    wcscat(path, L"\\_nvngx.dll");
    core = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!core) { fprintf(stderr, "NGX core load: %lu\n", GetLastError()); goto cleanup; }
#define LOAD(type, variable, symbol) type variable = (type)GetProcAddress(core, symbol); \
    if (!variable) { fprintf(stderr, "Missing export: %s\n", symbol); goto cleanup; }
    LOAD(InitProjectFn, init, "NVSDK_NGX_D3D11_Init_ProjectID");
    LOAD(ParamsFn, get_caps, "NVSDK_NGX_D3D11_GetCapabilityParameters");
    LOAD(ParamsFn, allocate, "NVSDK_NGX_D3D11_AllocateParameters");
    LOAD(CreateFn, create, "NVSDK_NGX_D3D11_CreateFeature");
    LOAD(EvalFn, evaluate, "NVSDK_NGX_D3D11_EvaluateFeature");
    destroy = (DestroyFn)GetProcAddress(core, "NVSDK_NGX_D3D11_DestroyParameters");
    release = (ReleaseFn)GetProcAddress(core, "NVSDK_NGX_D3D11_ReleaseFeature");
    shutdown = (ShutdownFn)GetProcAddress(core, "NVSDK_NGX_D3D11_Shutdown");
    if (!destroy || !release || !shutdown) goto cleanup;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
        D3D11_SDK_VERSION, &device, NULL, &context);
    printf("D3D11CreateDevice: 0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr)) goto cleanup;
    const wchar_t *paths[] = {argv[1]};
    NVSDK_NGX_FeatureCommonInfo common = {0};
    common.PathListInfo.Path = paths; common.PathListInfo.Length = 1;
    common.LoggingInfo.LoggingCallback = ngx_log;
    common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    common.LoggingInfo.DisableOtherLoggingSinks = true;
    NVSDK_NGX_Result result = init("f350c55b-833b-46f2-9234-f451d710b2cb", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "Elex-DLSS-research-0.1", argv[2], device, NVSDK_NGX_Version_API, &common);
    printf("NGX Init_ProjectID: 0x%08x\n", result);
    if (NVSDK_NGX_FAILED(result)) goto cleanup;
    initialized = 1;
    result = get_caps(&caps);
    printf("GetCapabilityParameters: 0x%08x\n", result);
    if (NVSDK_NGX_FAILED(result)) goto cleanup;
    int available = 0;
    result = NVSDK_NGX_Parameter_GetI(caps, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    printf("DLSS available: %d (0x%08x)\n", available, result);
    if (NVSDK_NGX_FAILED(result) || !available) goto cleanup;
    result = allocate(&params);
    if (NVSDK_NGX_FAILED(result)) goto cleanup;
    NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_Width, 640);
    NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_Height, 360);
    NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_OutWidth, 960);
    NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_OutHeight, 540);
    NVSDK_NGX_Parameter_SetI(params, NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_MaxQuality);
    NVSDK_NGX_Parameter_SetI(params, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
        NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
        NVSDK_NGX_DLSS_Feature_Flags_AutoExposure);
    result = create(context, NVSDK_NGX_Feature_SuperSampling, params, &feature);
    printf("Create DLSS 640x360 -> 960x540: 0x%08x\n", result);
    if (NVSDK_NGX_FAILED(result)) goto cleanup;
    const float rgb[4] = {0.25f, 0.5f, 0.75f, 1}, z[4] = {0.5f,0,0,0}, zero[4] = {0,0,0,0};
#define TEX(w,h,f,c,t) if (FAILED(texture(device,context,w,h,f,c,&t))) goto cleanup
    TEX(640,360,DXGI_FORMAT_R16G16B16A16_FLOAT,rgb,color);
    TEX(640,360,DXGI_FORMAT_R32_FLOAT,z,depth);
    TEX(640,360,DXGI_FORMAT_R16G16_FLOAT,zero,motion);
    TEX(960,540,DXGI_FORMAT_R16G16B16A16_FLOAT,zero,output);
    NVSDK_NGX_Parameter_SetD3d11Resource(params, NVSDK_NGX_Parameter_Color, (ID3D11Resource *)color);
    NVSDK_NGX_Parameter_SetD3d11Resource(params, NVSDK_NGX_Parameter_Depth, (ID3D11Resource *)depth);
    NVSDK_NGX_Parameter_SetD3d11Resource(params, NVSDK_NGX_Parameter_MotionVectors, (ID3D11Resource *)motion);
    NVSDK_NGX_Parameter_SetD3d11Resource(params, NVSDK_NGX_Parameter_Output, (ID3D11Resource *)output);
    NVSDK_NGX_Parameter_SetF(params, NVSDK_NGX_Parameter_MV_Scale_X, 1);
    NVSDK_NGX_Parameter_SetF(params, NVSDK_NGX_Parameter_MV_Scale_Y, 1);
    NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, 640);
    NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, 360);
    NVSDK_NGX_Parameter_SetF(params, NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1);
    NVSDK_NGX_Parameter_SetF(params, NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1);
    NVSDK_NGX_Parameter_SetF(params, NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, 16.6667f);
    for (int i = 0; i < 16; ++i) {
        NVSDK_NGX_Parameter_SetI(params, NVSDK_NGX_Parameter_Reset, i == 0);
        NVSDK_NGX_Parameter_SetF(params, NVSDK_NGX_Parameter_Jitter_Offset_X, (i & 1) ? .25f : -.25f);
        NVSDK_NGX_Parameter_SetF(params, NVSDK_NGX_Parameter_Jitter_Offset_Y, (i & 1) ? -.25f : .25f);
        result = evaluate(context, feature, params, NULL);
        if (NVSDK_NGX_FAILED(result)) { printf("Evaluate frame %d: 0x%08x\n", i, result); goto cleanup; }
    }
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(output, &desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    if (FAILED(hr)) goto cleanup;
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)output);
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) goto cleanup;
    const unsigned short *pixel = (const unsigned short *)((const char *)mapped.pData + mapped.RowPitch * 270 + 480 * 8);
    float actual[3] = {half_float(pixel[0]), half_float(pixel[1]), half_float(pixel[2])};
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    printf("16 evaluations completed; output RGB: %.5f %.5f %.5f\n", actual[0], actual[1], actual[2]);
    for (int i = 0; i < 3; ++i) if (!isfinite(actual[i]) || fabsf(actual[i] - rgb[i]) > .1f) goto cleanup;
    puts("PASS: native NGX D3D11 feature evaluated synthetic input. ELEX integration is NOT tested.");
    exit_code = 0;
cleanup:
    if (feature && release) release(feature);
    if (params && destroy) destroy(params);
    if (caps && destroy) destroy(caps);
    if (initialized && shutdown) shutdown();
#define RELEASE(p) if (p) IUnknown_Release((IUnknown *)p)
    RELEASE(staging); RELEASE(output); RELEASE(motion); RELEASE(depth); RELEASE(color);
    RELEASE(context); RELEASE(device);
    if (core) FreeLibrary(core);
    return exit_code;
}
