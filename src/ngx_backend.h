#ifndef ELEX_NGX_BACKEND_H
#define ELEX_NGX_BACKEND_H
#include <windows.h>
#include <d3d11.h>
#include <stdbool.h>
#include "nvsdk_ngx.h"

typedef void (*ElexLog)(const char *, ...);
typedef struct ElexNgx ElexNgx;
ElexNgx *elex_ngx_open(ID3D11Device *, const wchar_t *runtime_dir, const wchar_t *data_dir, ElexLog);
bool elex_ngx_size(ElexNgx *, ID3D11DeviceContext *, UINT, UINT, UINT, UINT);
bool elex_ngx_evaluate(ElexNgx *, ID3D11DeviceContext *, ID3D11Resource *color,
    ID3D11Resource *depth, ID3D11Resource *motion, ID3D11Resource *output,
    float jitter_x, float jitter_y, float milliseconds, bool reset);
void elex_ngx_close(ElexNgx *);
#endif
