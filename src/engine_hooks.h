#ifndef ELEX_ENGINE_HOOKS_H
#define ELEX_ENGINE_HOOKS_H
#include <windows.h>
#include <stdbool.h>
#include "renderer.h"
bool elex_engine_hooks(HMODULE module, RendererLog log);
bool elex_engine_frame_hook_ready(void);
void elex_engine_set_enabled(bool enabled);
float elex_engine_scale(void);
#endif
