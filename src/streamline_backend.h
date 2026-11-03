#ifndef ELEX_STREAMLINE_BACKEND_H
#define ELEX_STREAMLINE_BACKEND_H
#include <windows.h>
#include <d3d12.h>
#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>

using FrameGenLog = void (*)(const char *, ...);

struct StreamlineBackend {
    HMODULE module{};
    bool initialized{};
    FrameGenLog log{};
    sl::ViewportHandle viewport{0};
    PFun_slInit *init{};
    PFun_slShutdown *shutdown{};
    PFun_slSetD3DDevice *set_device{};
    PFun_slUpgradeInterface *upgrade{};
    PFun_slIsFeatureSupported *supported{};
    PFun_slGetFeatureFunction *feature_function{};
    PFun_slGetNewFrameToken *new_token{};
    PFun_slSetConstants *set_constants{};
    PFun_slSetTagForFrame *tag_frame{};
    PFun_slDLSSGGetState *get_fg_state{};
    PFun_slDLSSGSetOptions *set_fg_options{};
    PFun_slReflexSetOptions *set_reflex_options{};
    PFun_slReflexSleep *sleep{};
    PFun_slPCLSetMarker *marker{};

    bool open(const wchar_t *directory, const wchar_t *logs, FrameGenLog logger);
    bool bind(ID3D12Device *device, const LUID &adapter);
    bool options(bool enabled, float target_fps, UINT width, UINT height, UINT render_width, UINT render_height);
    bool state(sl::DLSSGState &out);
    void stop();
    void close();
};
#endif
