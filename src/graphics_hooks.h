#ifndef ELEX_GRAPHICS_HOOKS_H
#define ELEX_GRAPHICS_HOOKS_H
#include <windows.h>
typedef enum {
    ELEX_HOOK_MAP,ELEX_HOOK_UNMAP,ELEX_HOOK_INDEXED,ELEX_HOOK_FINISH,
    ELEX_HOOK_PIXEL,ELEX_HOOK_DRAW
} ElexGraphicsHook;
#ifdef __cplusplus
extern "C" {
#endif
// Each detour receives its stable original function as its first argument.
// Never resolve an inline detour through an object's mutable/wrapped vtable.
HRESULT elex_hook_graphics(void *target,ElexGraphicsHook kind,void *callback);
#ifdef __cplusplus
}
#endif
#endif
