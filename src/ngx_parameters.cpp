// Compile for the Microsoft C++ ABI, even when the rest of the probe uses
// MinGW. NGX returns a Microsoft C++ object with overloaded virtual methods.
// These original C adapters call the interfaces declared by NVIDIA's SDK.
#include <stddef.h>
#include "nvsdk_ngx_params.h"

// MSVC ABI floating-point-use marker; MinGW supplies the actual CRT.
extern "C" { int _fltused = 0; }

extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetUI(NVSDK_NGX_Parameter *p, const char *name, unsigned value)
{ p->Set(name, value); }
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetI(NVSDK_NGX_Parameter *p, const char *name, int value)
{ p->Set(name, value); }
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetF(NVSDK_NGX_Parameter *p, const char *name, float value)
{ p->Set(name, value); }
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetD3d11Resource(NVSDK_NGX_Parameter *p, const char *name, ID3D11Resource *value)
{ p->Set(name, value); }
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetI(NVSDK_NGX_Parameter *p, const char *name, int *value)
{ return p->Get(name, value); }
