#ifndef ELEX_DLSS_RENDERER_H
#define ELEX_DLSS_RENDERER_H
#include <windows.h>
#include <d3d11.h>
typedef void (*RendererLog)(const char *, ...);
typedef void (STDMETHODCALLTYPE *ElexDrawFn)(ID3D11DeviceContext *, UINT, UINT);
HRESULT elex_renderer_init(ID3D11Device *, RendererLog, HMODULE);
void elex_pixel_created(ID3D11Device *, const void *, SIZE_T, ID3D11PixelShader *);
void elex_compute_created(const void *, SIZE_T, ID3D11ComputeShader *);
void elex_draw(ID3D11DeviceContext *, ElexDrawFn, UINT, UINT);
void elex_renderer_shutdown(void);
#endif
