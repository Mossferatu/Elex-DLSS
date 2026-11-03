#ifndef ELEX_SR_PRESETS_H
#define ELEX_SR_PRESETS_H
#include <stdbool.h>
#include <wchar.h>
#include "nvsdk_ngx.h"

// Only presets exposed for use by the pinned NGX SDK; reserved/removed letters
// must not silently become Default. E/F remain available for legacy comparisons.
static inline bool elex_sr_preset_parse(const wchar_t *name, unsigned *value)
{
    static const wchar_t *names[]={L"Default",L"E",L"F",L"J",L"K",L"L",L"M"};
    static const unsigned values[]={0,5,6,10,11,12,13};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i){
        if(_wcsicmp(name,names[i])==0){*value=values[i];return true;}
    }
    return false;
}

static inline void elex_sr_preset_apply(NVSDK_NGX_Parameter *params,unsigned preset)
{
    // Preset and resolution mode are independent. Apply to every quality hint
    // before feature creation so the selection also works for native DLAA.
    const char *keys[]={NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i)
        NVSDK_NGX_Parameter_SetUI(params,keys[i],preset);
}
#endif
