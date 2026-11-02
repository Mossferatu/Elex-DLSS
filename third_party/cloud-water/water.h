#ifndef ELEX_WATER_H
#define ELEX_WATER_H
#include <d3d11.h>
typedef void (*WaterLogFn)(const char *, ...);
HRESULT install_water_hooks(ID3D11Device *device, WaterLogFn logger);
#endif
