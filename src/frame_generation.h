#ifndef ELEX_FRAME_GENERATION_H
#define ELEX_FRAME_GENERATION_H
#include <windows.h>
#include <d3d11.h>
#include <stdbool.h>
#include "renderer.h"
#ifdef __cplusplus
extern "C" {
#endif
HRESULT elex_fg_install(ID3D11Device *, RendererLog, HMODULE);
void elex_fg_simulation_start(unsigned frame);
void elex_fg_render_start(void);
void elex_fg_capture(ID3D11DeviceContext *, ID3D11Texture2D *depth,
    ID3D11Texture2D *motion, ID3D11Resource *hudless, const float globals[192], bool reset);
bool elex_fg_is_active(void);
// Standalone bridge test uses the same production implementation.
HRESULT elex_fg_create_test(ID3D11Device *, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
    const wchar_t *runtime_directory, const wchar_t *log_directory, RendererLog, float target_fps);
#ifdef __cplusplus
}
#endif
#endif
