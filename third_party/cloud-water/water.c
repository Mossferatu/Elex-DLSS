#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <string.h>
#include "water.h"
#include "patch.h"
#include "../../src/renderer.h"
#include "../../src/graphics_hooks.h"

typedef HRESULT (STDMETHODCALLTYPE *PixelFn)(ID3D11Device *, const void *, SIZE_T,
    ID3D11ClassLinkage *, ID3D11PixelShader **);
typedef void (STDMETHODCALLTYPE *DrawFn)(ID3D11DeviceContext *, UINT, UINT);
static WaterLogFn log_line;
static LONG draws_applied;
static const GUID variant_key = {0x19365a74,0x663f,0x438a,{0x82,0x7e,0x37,0x23,0x99,0x71,0xc5,0xf3}};

static HRESULT STDMETHODCALLTYPE pixel_shader(void *original_fn,ID3D11Device *device, const void *data,
    SIZE_T size, ID3D11ClassLinkage *linkage, ID3D11PixelShader **result)
{
    PixelFn original = (PixelFn)original_fn;
    HRESULT hr = original(device, data, size, linkage, result);
    if (FAILED(hr) || !result || !*result || linkage) return hr;
    elex_pixel_created(device, data, size, *result);
    unsigned char *patched = NULL;
    HRESULT patch = patch_water_shader(data, size, &patched);
    if (patch == S_FALSE) return hr;
    if (FAILED(patch)) {
        log_line("Water variant validation failed: 0x%08lx", (unsigned long)patch);
        return hr;
    }
    ID3D11PixelShader *variant = NULL;
    patch = original(device, patched, size, NULL, &variant);
    HeapFree(GetProcessHeap(), 0, patched);
    if (SUCCEEDED(patch)) {
        /* The original shader owns the variant, with no global device/shader leak. */
        patch = ID3D11PixelShader_SetPrivateDataInterface(*result, &variant_key, (IUnknown *)variant);
        ID3D11PixelShader_Release(variant);
    }
    log_line("Water impulse shader SHA256 matched; slot-zero variant %s (0x%08lx)",
        SUCCEEDED(patch) ? "ready" : "FAILED", (unsigned long)patch);
    return hr;
}

static BOOL wave_target(ID3D11RenderTargetView *view, DXGI_FORMAT format)
{
    if (!view) return FALSE;
    D3D11_RENDER_TARGET_VIEW_DESC desc;
    ID3D11RenderTargetView_GetDesc(view, &desc);
    if (desc.Format != format || desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2DARRAY ||
        desc.Texture2DArray.MipSlice != 0) return FALSE;
    ID3D11Resource *resource = NULL;
    ID3D11RenderTargetView_GetResource(view, &resource);
    D3D11_RESOURCE_DIMENSION dimension;
    ID3D11Resource_GetType(resource, &dimension);
    BOOL matches = FALSE;
    if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC texture;
        ID3D11Texture2D_GetDesc((ID3D11Texture2D *)resource, &texture);
        matches = texture.Width == 64 && texture.Height == 64 &&
            texture.ArraySize == 4 && texture.MipLevels == 4 && texture.SampleDesc.Count == 1;
    }
    ID3D11Resource_Release(resource);
    return matches;
}

static BOOL additive_pair(ID3D11BlendState *blend)
{
    if (!blend) return FALSE;
    D3D11_BLEND_DESC desc;
    ID3D11BlendState_GetDesc(blend, &desc);
    const D3D11_RENDER_TARGET_BLEND_DESC *a = &desc.RenderTarget[0];
    const D3D11_RENDER_TARGET_BLEND_DESC *b = &desc.RenderTarget[desc.IndependentBlendEnable ? 1 : 0];
    return !desc.AlphaToCoverageEnable && !memcmp(a, b, sizeof(*a)) && a->BlendEnable &&
        a->SrcBlend == D3D11_BLEND_ONE && a->DestBlend == D3D11_BLEND_ONE &&
        a->BlendOp == D3D11_BLEND_OP_ADD && a->SrcBlendAlpha == D3D11_BLEND_ONE &&
        a->DestBlendAlpha == D3D11_BLEND_ONE && a->BlendOpAlpha == D3D11_BLEND_OP_ADD &&
        a->RenderTargetWriteMask == D3D11_COLOR_WRITE_ENABLE_ALL;
}

static void STDMETHODCALLTYPE draw(void *original_fn,ID3D11DeviceContext *context, UINT count, UINT start)
{
    DrawFn original = (DrawFn)original_fn;
    ID3D11PixelShader *shader = NULL, *variant = NULL;
    ID3D11RenderTargetView *targets[8] = {0};
    ID3D11UnorderedAccessView *uavs[8] = {0};
    ID3D11DepthStencilView *depth = NULL;
    ID3D11BlendState *blend = NULL;
    BOOL applied = FALSE;
    if (ID3D11DeviceContext_GetType(context) != D3D11_DEVICE_CONTEXT_IMMEDIATE) goto finish;
    ID3D11DeviceContext_PSGetShader(context, &shader, NULL, NULL);
    if (!shader) goto finish;
    UINT size = sizeof(variant);
    if (FAILED(ID3D11PixelShader_GetPrivateData(shader, &variant_key, &size, &variant)) ||
        !variant) goto finish;
    ID3D11DeviceContext_OMGetRenderTargetsAndUnorderedAccessViews(context,
        8, targets, &depth, 0, 8, uavs);
    if (depth || !wave_target(targets[0], DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        !wave_target(targets[1], DXGI_FORMAT_R16G16_FLOAT)) goto finish;
    for (unsigned i = 0; i < 8; ++i)
        if (uavs[i] || (i >= 2 && targets[i])) goto finish;
    FLOAT factors[4]; UINT mask;
    ID3D11DeviceContext_OMGetBlendState(context, &blend, factors, &mask);
    if (!additive_pair(blend)) goto finish;
    D3D11_RENDER_TARGET_VIEW_DESC a, b;
    ID3D11RenderTargetView_GetDesc(targets[0], &a);
    ID3D11RenderTargetView_GetDesc(targets[1], &b);
    if (a.Texture2DArray.FirstArraySlice != b.Texture2DArray.FirstArraySlice ||
        a.Texture2DArray.ArraySize != b.Texture2DArray.ArraySize) goto finish;

    /* Both outputs retain ONE/ONE blending and their original values. Only
     * the physical output slot changes. The second target is unbound while
     * rendering each pass, avoiding unstable simultaneous RT1 accumulation. */
    ID3D11DeviceContext_OMSetRenderTargets(context, 1, &targets[0], NULL);
    original(context, count, start);
    ID3D11DeviceContext_OMSetRenderTargets(context, 1, &targets[1], NULL);
    ID3D11DeviceContext_PSSetShader(context, variant, NULL, 0);
    original(context, count, start);
    ID3D11DeviceContext_PSSetShader(context, shader, NULL, 0);
    ID3D11DeviceContext_OMSetRenderTargets(context, 8, targets, depth);
    applied = TRUE;
    if (InterlockedIncrement(&draws_applied) == 1)
        log_line("APPLIED water: exact impulse shader; two additive slot-zero draws; native D3D11");
finish:
    if (blend) ID3D11BlendState_Release(blend);
    if (depth) ID3D11DepthStencilView_Release(depth);
    for (unsigned i = 0; i < 8; ++i) {
        if (targets[i]) ID3D11RenderTargetView_Release(targets[i]);
        if (uavs[i]) ID3D11UnorderedAccessView_Release(uavs[i]);
    }
    if (variant) ID3D11PixelShader_Release(variant);
    if (shader) ID3D11PixelShader_Release(shader);
    if (!applied) elex_draw(context, original, count, start);
}

HRESULT install_water_hooks(ID3D11Device *device, WaterLogFn logger)
{
    log_line = logger;
    HRESULT hr=elex_hook_graphics((*(void ***)device)[15],ELEX_HOOK_PIXEL,(void *)pixel_shader);
    ID3D11DeviceContext *context = NULL;
    ID3D11Device_GetImmediateContext(device, &context);
    if (SUCCEEDED(hr) && context) hr=elex_hook_graphics((*(void ***)context)[13],ELEX_HOOK_DRAW,(void *)draw);
    if (context) ID3D11DeviceContext_Release(context);
    logger("Water native interception %s (0x%08lx); waiting for exact shader and draw",
        SUCCEEDED(hr) ? "ready" : "FAILED", (unsigned long)hr);
    return hr;
}
