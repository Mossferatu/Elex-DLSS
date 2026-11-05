#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <softpub.h>
#include <wintrust.h>
#include <wincrypt.h>
#include <cstdio>
#include <cwchar>
#include "streamline_backend.h"
#include "../build/version.h"

static FrameGenLog stream_logger;
static void sl_log(sl::LogType type, const char *message)
{
    if (stream_logger) stream_logger("Streamline[%u] %s", static_cast<unsigned>(type), message);
}

static bool nvidia_signature(const wchar_t *path)
{
    WINTRUST_FILE_INFO file{};
    file.cbStruct=sizeof(file);file.pcwszFilePath=path;
    WINTRUST_DATA data{};
    data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;
    data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;
    data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG result=WinVerifyTrust(nullptr,&policy,&data);
    bool valid=false;
    if (result==ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA *provider=WTHelperProvDataFromStateData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR *signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;
        if(signer&&signer->csCertChain) {
            wchar_t name[256]{};
            CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,name,256);
            valid=wcscmp(name,L"NVIDIA Corporation")==0;
        }
    }
    data.dwStateAction=WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr,&policy,&data);
    return valid;
}

bool StreamlineBackend::open(const wchar_t *directory,const wchar_t *logs,FrameGenLog logger)
{
    log=logger;stream_logger=logger;
    wchar_t path[32768]{};
    if(swprintf(path,32768,L"%ls\\sl.interposer.dll",directory)<0)return false;
    if(!nvidia_signature(path)) {log("Frame generation unavailable: Streamline signature verification failed");return false;}
    module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module){log("Streamline load failed: %lu",GetLastError());return false;}
#define LOAD(field,symbol) field=reinterpret_cast<decltype(field)>(GetProcAddress(module,symbol)); \
    if(!field){log("Missing Streamline export: %s",symbol);close();return false;}
    LOAD(init,"slInit");LOAD(shutdown,"slShutdown");LOAD(set_device,"slSetD3DDevice");
    LOAD(upgrade,"slUpgradeInterface");LOAD(supported,"slIsFeatureSupported");
    LOAD(feature_function,"slGetFeatureFunction");LOAD(new_token,"slGetNewFrameToken");
    LOAD(set_constants,"slSetConstants");LOAD(tag_frame,"slSetTagForFrame");
#undef LOAD
    const wchar_t *paths[]={directory};
    const sl::Feature features[]={sl::kFeatureDLSS_G,sl::kFeatureReflex,sl::kFeaturePCL};
    sl::Preferences preferences;
    preferences.flags=sl::PreferenceFlags::eUseManualHooking|sl::PreferenceFlags::eUseFrameBasedResourceTagging|
        sl::PreferenceFlags::eUseDXGIFactoryProxy|sl::PreferenceFlags::eDisableCLStateTracking;
    preferences.pathsToPlugins=paths;preferences.numPathsToPlugins=1;
    preferences.pathToLogsAndData=logs;preferences.logMessageCallback=sl_log;
    preferences.featuresToLoad=features;preferences.numFeaturesToLoad=3;
    preferences.engine=sl::EngineType::eCustom;preferences.engineVersion="ELEX-DLSS-" ELEX_DLSS_VERSION;
    preferences.projectId="f350c55b-833b-46f2-9234-f451d710b2cb";
    preferences.renderAPI=sl::RenderAPI::eD3D12;
    sl::Result result=init(preferences,sl::kSDKVersion);
    log("Streamline initialization: %u",static_cast<unsigned>(result));
    initialized=result==sl::Result::eOk;
    if(!initialized)close();
    return initialized;
}

bool StreamlineBackend::bind(ID3D12Device *device,const LUID &luid)
{
    if(!initialized)return false;
    sl::AdapterInfo adapter{};
    adapter.deviceLUID=reinterpret_cast<uint8_t *>(const_cast<LUID *>(&luid));adapter.deviceLUIDSizeInBytes=sizeof(luid);
    sl::Result result=supported(sl::kFeatureDLSS_G,adapter);
    log("DLSS Frame Generation support: %u",static_cast<unsigned>(result));
    if(result!=sl::Result::eOk)return false;
    result=set_device(device);
    if(result!=sl::Result::eOk){log("Streamline device binding: %u",static_cast<unsigned>(result));return false;}
#define FEATURE(id,field,symbol) result=feature_function(id,symbol,reinterpret_cast<void *&>(field)); \
    if(result!=sl::Result::eOk||!field){log("Missing Streamline feature function: %s (%u)",symbol,static_cast<unsigned>(result));return false;}
    FEATURE(sl::kFeatureDLSS_G,get_fg_state,"slDLSSGGetState");
    FEATURE(sl::kFeatureDLSS_G,set_fg_options,"slDLSSGSetOptions");
    FEATURE(sl::kFeatureReflex,set_reflex_options,"slReflexSetOptions");
    FEATURE(sl::kFeatureReflex,sleep,"slReflexSleep");
    FEATURE(sl::kFeaturePCL,marker,"slPCLSetMarker");
#undef FEATURE
    sl::DLSSGState capabilities{};
    if(!state(capabilities))return false;
    log("Native Dynamic MFG capability=%d maximum generated frames=%u",capabilities.bIsDynamicMFGSupported,
        capabilities.numFramesToGenerateMax);
    return capabilities.bIsDynamicMFGSupported==sl::eTrue;
}

bool StreamlineBackend::options(bool enabled,float target_fps,UINT width,UINT height,UINT rw,UINT rh)
{
    if(!set_fg_options||!set_reflex_options)return false;
    sl::ReflexOptions reflex{};
    reflex.mode=enabled?sl::ReflexMode::eLowLatency:sl::ReflexMode::eOff;
    reflex.frameLimitUs=0;
    sl::Result result=set_reflex_options(reflex);
    if(result!=sl::Result::eOk){log("Reflex options failed: %u",static_cast<unsigned>(result));return false;}
    sl::DLSSGOptions options{};
    options.mode=enabled?sl::DLSSGMode::eDynamic:sl::DLSSGMode::eOff;
    options.dynamicTargetFrameRate=target_fps;
    options.flags=sl::DLSSGFlags::eRetainResourcesWhenOff|sl::DLSSGFlags::eEnableFullscreenMenuDetection;
    options.numBackBuffers=3;options.colorWidth=width;options.colorHeight=height;
    options.colorBufferFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
    options.mvecDepthWidth=rw;options.mvecDepthHeight=rh;
    options.mvecBufferFormat=DXGI_FORMAT_R16G16_FLOAT;options.depthBufferFormat=DXGI_FORMAT_R32_FLOAT;
    options.hudLessBufferFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
    result=set_fg_options(viewport,options);
    if(result!=sl::Result::eOk)log("Native Dynamic MFG options failed: %u",static_cast<unsigned>(result));
    return result==sl::Result::eOk;
}

bool StreamlineBackend::state(sl::DLSSGState &out)
{
    if(!get_fg_state)return false;
    sl::Result result=get_fg_state(viewport,out,nullptr);
    if(result!=sl::Result::eOk)log("Streamline frame-generation state failed: %u",static_cast<unsigned>(result));
    return result==sl::Result::eOk;
}

void StreamlineBackend::stop()
{
    if(initialized&&shutdown){shutdown();initialized=false;}
}

void StreamlineBackend::close()
{
    stop();
    if(module){FreeLibrary(module);module=nullptr;}
}
