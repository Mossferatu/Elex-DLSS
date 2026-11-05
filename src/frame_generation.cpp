#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <new>
#include "frame_generation.h"
#include "streamline_backend.h"
#include "../third_party/minhook/include/MinHook.h"

namespace {
template<class T> void drop(T *&p){if(p){p->Release();p=nullptr;}}
using Mat=double[4][4];
void identity(Mat &m){std::memset(m,0,sizeof(m));for(int i=0;i<4;++i)m[i][i]=1;}
void multiply(const Mat &a,const Mat &b,Mat &out){Mat r{};for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)r[i][j]+=a[i][k]*b[k][j];std::memcpy(out,r,sizeof(r));}
bool inverse(const Mat &in,Mat &out){
    double a[4][8]{};for(int i=0;i<4;++i)for(int j=0;j<4;++j){a[i][j]=in[i][j];a[i][j+4]=i==j;}
    for(int col=0;col<4;++col){int pivot=col;for(int r=col+1;r<4;++r)if(std::fabs(a[r][col])>std::fabs(a[pivot][col]))pivot=r;
        if(std::fabs(a[pivot][col])<1e-12)return false;
        if(pivot!=col)for(int j=0;j<8;++j){double v=a[col][j];a[col][j]=a[pivot][j];a[pivot][j]=v;}
        double d=a[col][col];for(double &v:a[col])v/=d;
        for(int r=0;r<4;++r)if(r!=col){double f=a[r][col];for(int j=0;j<8;++j)a[r][j]-=f*a[col][j];}
    }
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)out[i][j]=a[i][j+4];return true;
}
void sl_matrix(const Mat &m,sl::float4x4 &out){for(int i=0;i<4;++i)out[i]=sl::float4(float(m[i][0]),float(m[i][1]),float(m[i][2]),float(m[i][3]));}

struct SharedTexture {
    ID3D12Resource *d12{};
    ID3D11Texture2D *d11{};
    UINT width{},height{};
    DXGI_FORMAT format{};
    void release(){drop(d11);drop(d12);width=height=0;}
};

class Bridge final : public IDXGISwapChain4 {
    std::atomic<ULONG> references{1};
    ID3D11Device *device11{};
    ID3D11Device5 *device11_5{};
    ID3D11DeviceContext *context11{};
    ID3D11DeviceContext4 *context11_4{};
    ID3D12Device *device12{};
    ID3D12Device *proxy_device12{};
    ID3D12CommandQueue *queue{};
    ID3D12CommandAllocator *allocators[3]{};
    UINT64 allocator_done[3]{};
    unsigned allocator_index{};
    ID3D12GraphicsCommandList *commands{};
    ID3D12Fence *fence12{};
    ID3D11Fence *fence11{};
    UINT64 fence_value{},reuse_value{},list_done{};
    HANDLE fence_event{};
    IDXGIFactory1 *parent_factory{};
    IDXGISwapChain4 *swap{};
    ID3D11Texture2D *game_buffer{};
    DXGI_SWAP_CHAIN_DESC game_desc{};
    UINT swap_flags{};
    StreamlineBackend sl;
    RendererLog log{};
    float target{};
    SharedTexture color,depth,motion,hudless;
    bool frame_ready{},reset_requested{true},generation_enabled{},failed{},fullscreen_dirty{};
    float globals[192]{};
    Mat previous_vp{};
    bool previous_valid{};
    unsigned captured_frame{};
    unsigned fallback_frame{};
    unsigned frame_count{},generated_count{};
    ULONGLONG sample_tick{};
    unsigned sample_frames{},sample_generated{};
    SRWLOCK token_lock=SRWLOCK_INIT;
    sl::FrameToken *token{};
    unsigned token_frame{};
    std::atomic<bool> submit_started{false};
    std::atomic<bool> alive{false};

    bool wait_cpu(UINT64 value){
        if(!value||fence12->GetCompletedValue()>=value)return true;
        HRESULT hr=fence12->SetEventOnCompletion(value,fence_event);
        if(FAILED(hr))return false;
        DWORD result=WaitForSingleObject(fence_event,5000);
        if(result!=WAIT_OBJECT_0){log("FG GPU wait failed or timed out (%lu)",result);return false;}return true;
    }
    void transition(ID3D12Resource *resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=resource;barrier.Transition.StateBefore=before;barrier.Transition.StateAfter=after;
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands->ResourceBarrier(1,&barrier);
    }
    bool execute(){
        HRESULT hr=commands->Close();if(FAILED(hr))return false;
        ID3D12CommandList *lists[]={commands};queue->ExecuteCommandLists(1,lists);
        list_done=++fence_value;
        allocator_done[allocator_index]=list_done;
        allocator_index=(allocator_index+1)%3;
        return SUCCEEDED(queue->Signal(fence12,list_done));
    }
    bool reset_commands(){
        if(!wait_cpu(allocator_done[allocator_index]))return false;
        ID3D12CommandAllocator *alloc=allocators[allocator_index];
        return SUCCEEDED(alloc->Reset())&&SUCCEEDED(commands->Reset(alloc,nullptr));
    }
    bool shared(SharedTexture &out,UINT width,UINT height,DXGI_FORMAT format){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;
        d.DepthOrArraySize=1;d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        // D3D11 shared textures require a render-target-compatible description.
        d.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS|D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        HRESULT hr=device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,
            __uuidof(ID3D12Resource),reinterpret_cast<void **>(&out.d12));
        const char *stage="CreateCommittedResource";
        HANDLE handle=nullptr;
        if(SUCCEEDED(hr)){stage="CreateSharedHandle";hr=device12->CreateSharedHandle(out.d12,nullptr,GENERIC_ALL,nullptr,&handle);}
        if(SUCCEEDED(hr)){stage="OpenSharedResource1";hr=device11_5->OpenSharedResource1(handle,__uuidof(ID3D11Texture2D),reinterpret_cast<void **>(&out.d11));}
        if(handle)CloseHandle(handle);
        if(FAILED(hr)){log("FG shared texture %s failed 0x%08lx (%ux%u format=%u)",stage,static_cast<unsigned long>(hr),width,height,format);out.release();return false;}
        out.width=width;out.height=height;out.format=format;return true;
    }
    bool create_game_buffer(){
        D3D11_TEXTURE2D_DESC d{};d.Width=game_desc.BufferDesc.Width;d.Height=game_desc.BufferDesc.Height;
        d.MipLevels=1;d.ArraySize=1;d.Format=game_desc.BufferDesc.Format;d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        HRESULT hr=device11->CreateTexture2D(&d,nullptr,&game_buffer);
        if(FAILED(hr)){log("FG virtual backbuffer failed: 0x%08lx",static_cast<unsigned long>(hr));return false;}
        return shared(color,d.Width,d.Height,d.Format)&&shared(hudless,d.Width,d.Height,d.Format);
    }
    bool guide_size(UINT width,UINT height){
        if(depth.width==width&&depth.height==height)return true;
        if(reuse_value&&!wait_cpu(reuse_value))return false;
        depth.release();motion.release();previous_valid=false;reset_requested=true;
        return shared(depth,width,height,DXGI_FORMAT_R32_FLOAT)&&shared(motion,width,height,DXGI_FORMAT_R16G16_FLOAT);
    }
    bool constants(sl::Constants &c){
        Mat p{},view{},vp{},ivp{},ip{},to_prev{},from_prev{},lens{};
        for(int r=0;r<4;++r)for(int col=0;col<4;++col){p[r][col]=globals[80+col*4+r];view[r][col]=globals[48+col*4+r];}
        for(int col=0;col<3;++col)view[3][col]=-(double(globals[140])*view[0][col]+double(globals[141])*view[1][col]+double(globals[142])*view[2][col]);
        multiply(view,p,vp);
        if(!inverse(vp,ivp)||!inverse(p,ip))return false;
        if(previous_valid)multiply(ivp,previous_vp,to_prev);else identity(to_prev);
        if(!inverse(to_prev,from_prev))return false;
        identity(lens);
        sl_matrix(p,c.cameraViewToClip);sl_matrix(ip,c.clipToCameraView);sl_matrix(lens,c.clipToLensClip);
        sl_matrix(to_prev,c.clipToPrevClip);sl_matrix(from_prev,c.prevClipToClip);
        c.jitterOffset=sl::float2((globals[34]-globals[82])*globals[152]*.5f,-(globals[38]-globals[86])*globals[153]*.5f);
        c.mvecScale=sl::float2(-1,-1);c.cameraPinholeOffset=sl::float2(0,0);
        c.cameraPos=sl::float3(globals[140],globals[141],globals[142]);
        c.cameraRight=sl::float3(globals[48],globals[49],globals[50]);
        c.cameraUp=sl::float3(globals[52],globals[53],globals[54]);
        c.cameraFwd=sl::float3(globals[56],globals[57],globals[58]);
        c.cameraNear=globals[147];c.cameraFar=globals[139];c.cameraFOV=2.f*std::atan(1.f/globals[85]);
        c.cameraAspectRatio=float(game_desc.BufferDesc.Width)/game_desc.BufferDesc.Height;
        c.depthInverted=sl::eFalse;c.cameraMotionIncluded=sl::eTrue;c.motionVectors3D=sl::eFalse;
        c.motionVectorsJittered=sl::eFalse;c.motionVectorsDilated=sl::eFalse;
        c.reset=reset_requested||!previous_valid?sl::eTrue:sl::eFalse;
        std::memcpy(previous_vp,vp,sizeof(vp));previous_valid=true;
        return true;
    }
    void release_textures(){drop(game_buffer);color.release();hudless.release();depth.release();motion.release();frame_ready=false;}

public:
    HRESULT initialize(ID3D11Device *device,const DXGI_SWAP_CHAIN_DESC &desc,const wchar_t *runtime,
        const wchar_t *logs,RendererLog logger,float requested){
        log=logger;target=requested;game_desc=desc;
        if(desc.SampleDesc.Count!=1||desc.BufferDesc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||!desc.OutputWindow)return E_INVALIDARG;
        if(!game_desc.BufferDesc.Width||!game_desc.BufferDesc.Height){RECT r{};GetClientRect(desc.OutputWindow,&r);game_desc.BufferDesc.Width=r.right;game_desc.BufferDesc.Height=r.bottom;}
        device11=device;device11->AddRef();device11->GetImmediateContext(&context11);
        HRESULT hr=device11->QueryInterface(__uuidof(ID3D11Device5),reinterpret_cast<void **>(&device11_5));
        if(SUCCEEDED(hr))hr=context11->QueryInterface(__uuidof(ID3D11DeviceContext4),reinterpret_cast<void **>(&context11_4));
        if(FAILED(hr))return hr;
        if(!sl.open(runtime,logs,logger))return E_FAIL;
        IDXGIDevice *dxgi_device=nullptr;IDXGIAdapter *adapter=nullptr;
        hr=device11->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void **>(&dxgi_device));
        if(SUCCEEDED(hr))hr=dxgi_device->GetAdapter(&adapter);
        DXGI_ADAPTER_DESC adapter_desc{};if(SUCCEEDED(hr))hr=adapter->GetDesc(&adapter_desc);
        if(SUCCEEDED(hr))hr=adapter->GetParent(__uuidof(IDXGIFactory1),reinterpret_cast<void **>(&parent_factory));
        if(SUCCEEDED(hr))hr=D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),reinterpret_cast<void **>(&device12));
        drop(adapter);drop(dxgi_device);if(FAILED(hr))return hr;
        if(!sl.bind(device12,adapter_desc.AdapterLuid))return DXGI_ERROR_UNSUPPORTED;
        proxy_device12=device12;proxy_device12->AddRef();
        if(sl.upgrade(reinterpret_cast<void **>(&proxy_device12))!=sl::Result::eOk)return E_FAIL;
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr=proxy_device12->CreateCommandQueue(&q,__uuidof(ID3D12CommandQueue),reinterpret_cast<void **>(&queue));
        for(unsigned i=0;i<3&&SUCCEEDED(hr);++i)
            hr=device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),reinterpret_cast<void **>(&allocators[i]));
        if(SUCCEEDED(hr))hr=device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[0],nullptr,__uuidof(ID3D12GraphicsCommandList),reinterpret_cast<void **>(&commands));
        if(SUCCEEDED(hr))hr=commands->Close();
        if(SUCCEEDED(hr))hr=device12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,__uuidof(ID3D12Fence),reinterpret_cast<void **>(&fence12));
        HANDLE shared_fence=nullptr;
        if(SUCCEEDED(hr))hr=device12->CreateSharedHandle(fence12,nullptr,GENERIC_ALL,nullptr,&shared_fence);
        if(SUCCEEDED(hr))hr=device11_5->OpenSharedFence(shared_fence,__uuidof(ID3D11Fence),reinterpret_cast<void **>(&fence11));
        if(shared_fence)CloseHandle(shared_fence);
        fence_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(FAILED(hr)||!fence_event)return FAILED(hr)?hr:HRESULT_FROM_WIN32(GetLastError());
        IDXGIFactory2 *factory=nullptr;
        hr=CreateDXGIFactory2(0,__uuidof(IDXGIFactory2),reinterpret_cast<void **>(&factory));
        if(FAILED(hr))return hr;
        IDXGIFactory5 *factory5=nullptr;BOOL tearing=FALSE;
        if(SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5),reinterpret_cast<void **>(&factory5)))){
            factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&tearing,sizeof(tearing));drop(factory5);
        }
        swap_flags=tearing?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0;
        if(sl.upgrade(reinterpret_cast<void **>(&factory))!=sl::Result::eOk){drop(factory);return E_FAIL;}
        DXGI_SWAP_CHAIN_DESC1 d{};d.Width=game_desc.BufferDesc.Width;d.Height=game_desc.BufferDesc.Height;
        d.Format=desc.BufferDesc.Format;d.SampleDesc.Count=1;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount=3;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;d.Scaling=DXGI_SCALING_STRETCH;
        d.AlphaMode=DXGI_ALPHA_MODE_IGNORE;d.Flags=swap_flags;
        IDXGISwapChain1 *base=nullptr;
        hr=factory->CreateSwapChainForHwnd(queue,desc.OutputWindow,&d,nullptr,nullptr,&base);
        if(SUCCEEDED(hr))hr=base->QueryInterface(__uuidof(IDXGISwapChain4),reinterpret_cast<void **>(&swap));
        drop(base);drop(factory);if(FAILED(hr))return hr;
        if(!create_game_buffer())return E_FAIL;
        alive=true;
        sample_tick=GetTickCount64();
        log("D3D11/D3D12 native Dynamic MFG presentation bridge ready: %ux%u target=%.2f",d.Width,d.Height,target);
        return S_OK;
    }
    void simulation_start(unsigned frame){
        if(!alive)return;
        AcquireSRWLockExclusive(&token_lock);
        if(token&&token_frame==frame){ReleaseSRWLockExclusive(&token_lock);return;}
        ReleaseSRWLockExclusive(&token_lock);
        sl::FrameToken *next=nullptr;
        sl::Result result=sl.new_token(next,&frame);
        if(result==sl::Result::eOk&&next){
            // Reflex may wait for the previous present; never hold token_lock
            // across that wait, since the present thread also consumes it.
            sl.sleep(*next);sl.marker(sl::PCLMarker::eSimulationStart,*next);
            AcquireSRWLockExclusive(&token_lock);token=next;token_frame=frame;submit_started.store(false,std::memory_order_relaxed);
            ReleaseSRWLockExclusive(&token_lock);
        }
    }
    void render_start(){
        if(!alive.load(std::memory_order_relaxed)||submit_started.load(std::memory_order_relaxed))return;
        AcquireSRWLockExclusive(&token_lock);
        if(token&&!submit_started.load(std::memory_order_relaxed)){sl.marker(sl::PCLMarker::eSimulationEnd,*token);sl.marker(sl::PCLMarker::eRenderSubmitStart,*token);submit_started.store(true,std::memory_order_relaxed);}
        ReleaseSRWLockExclusive(&token_lock);
    }
    void capture(ID3D11DeviceContext *ctx,ID3D11Texture2D *d,ID3D11Texture2D *m,ID3D11Resource *h,const float *g,bool reset){
        if(!alive||failed||ctx!=context11||!d||!m||!h)return;
        D3D11_TEXTURE2D_DESC desc{};d->GetDesc(&desc);
        if(!guide_size(desc.Width,desc.Height)){failed=true;return;}
        if(reuse_value)context11_4->Wait(fence11,reuse_value);
        context11->CopyResource(depth.d11,d);context11->CopyResource(motion.d11,m);context11->CopyResource(hudless.d11,h);
        std::memcpy(globals,g,sizeof(globals));reset_requested=reset_requested||reset;frame_ready=true;
        AcquireSRWLockShared(&token_lock);captured_frame=token_frame;ReleaseSRWLockShared(&token_lock);
    }
    bool active()const{return alive&&!failed;}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IDXGIObject)||iid==__uuidof(IDXGIDeviceSubObject)||
           iid==__uuidof(IDXGISwapChain)||iid==__uuidof(IDXGISwapChain1)||iid==__uuidof(IDXGISwapChain2)||
           iid==__uuidof(IDXGISwapChain3)||iid==__uuidof(IDXGISwapChain4)){*out=static_cast<IDXGISwapChain4 *>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{ULONG n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID k,UINT n,const void *p) override{return swap->SetPrivateData(k,n,p);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID k,const IUnknown *p) override{return swap->SetPrivateDataInterface(k,p);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID k,UINT *n,void *p) override{return swap->GetPrivateData(k,n,p);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID iid,void **out) override{return parent_factory->QueryInterface(iid,out);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid,void **out) override{return device11->QueryInterface(iid,out);}
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index,REFIID iid,void **out) override{
        if(!out)return E_POINTER;*out=nullptr;if(index||!game_buffer)return DXGI_ERROR_INVALID_CALL;return game_buffer->QueryInterface(iid,out);
    }
    HRESULT STDMETHODCALLTYPE Present(UINT interval,UINT flags) override{
        if(flags&DXGI_PRESENT_TEST)return swap->Present(interval,flags);
        if(!alive)return DXGI_ERROR_DEVICE_REMOVED;
        if(fullscreen_dirty){
            // ELEX's blt swap chain did not require this resize after a mode
            // switch. The D3D12 flip swap chain does. Keep its D3D11 buffer.
            if(!wait_cpu(reuse_value))return E_FAIL;
            HRESULT resized=swap->ResizeBuffers(3,color.width,color.height,game_desc.BufferDesc.Format,swap_flags);
            log("FG fullscreen buffer refresh: 0x%08lx",static_cast<unsigned long>(resized));
            if(FAILED(resized))return resized;
            fullscreen_dirty=false;
        }
        if(!token)simulation_start(++fallback_frame);
        render_start();
        AcquireSRWLockShared(&token_lock);sl::FrameToken *current=token;unsigned frame=token_frame;ReleaseSRWLockShared(&token_lock);
        bool use_fg=frame_ready&&!failed&&current&&captured_frame==frame;
        if(use_fg!=generation_enabled){
            if(sl.options(use_fg,target,color.width,color.height,depth.width,depth.height))generation_enabled=use_fg;
            else {failed=true;use_fg=false;generation_enabled=false;sl.options(false,target,color.width,color.height,depth.width,depth.height);}
            reset_requested=true;log("Native Dynamic MFG %s target=%.2f",generation_enabled?"enabled":"disabled",target);
        }
        if(reuse_value)context11_4->Wait(fence11,reuse_value);
        context11->CopyResource(color.d11,game_buffer);
        UINT64 input_ready=++fence_value;
        HRESULT hr=context11_4->Signal(fence11,input_ready);context11->Flush();
        if(SUCCEEDED(hr))hr=queue->Wait(fence12,input_ready);
        if(FAILED(hr)||!reset_commands())return FAILED(hr)?hr:E_FAIL;
        UINT index=swap->GetCurrentBackBufferIndex();ID3D12Resource *backbuffer=nullptr;
        hr=swap->GetBuffer(index,__uuidof(ID3D12Resource),reinterpret_cast<void **>(&backbuffer));
        if(FAILED(hr)||!backbuffer){
            commands->Close();fullscreen_dirty=true;
            log("FG backbuffer unavailable: index=%u result=0x%08lx; scheduling buffer refresh",index,static_cast<unsigned long>(hr));
            return FAILED(hr)?hr:DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        }
        transition(color.d12,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(backbuffer,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST);
        commands->CopyResource(backbuffer,color.d12);
        transition(backbuffer,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);
        transition(color.d12,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);drop(backbuffer);
        if(use_fg){
            sl::Constants constants_data{};
            if(!constants(constants_data)||sl.set_constants(constants_data,*current,sl.viewport)!=sl::Result::eOk){failed=true;use_fg=false;}
            else {
                // Simultaneous-access shared textures decay to COMMON at
                // ExecuteCommandLists boundaries. Report their state at Present.
                sl::Resource depths(sl::ResourceType::eTex2d,depth.d12,D3D12_RESOURCE_STATE_COMMON);
                sl::Resource motions(sl::ResourceType::eTex2d,motion.d12,D3D12_RESOURCE_STATE_COMMON);
                sl::Resource clean(sl::ResourceType::eTex2d,hudless.d12,D3D12_RESOURCE_STATE_COMMON);
                sl::Extent render_extent{0,0,depth.width,depth.height},color_extent{0,0,color.width,color.height};
                sl::ResourceTag tags[]={
                    {&depths,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent,&render_extent},
                    {&motions,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent,&render_extent},
                    {&clean,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent,&color_extent}};
                sl::Result result=sl.tag_frame(*current,sl.viewport,tags,3,commands);
                if(result!=sl::Result::eOk){log("FG resource tagging failed: %u",static_cast<unsigned>(result));failed=true;use_fg=false;}
            }
        }
        if(!use_fg&&generation_enabled){
            sl.options(false,target,color.width,color.height,depth.width,depth.height);generation_enabled=false;
        }
        if(!execute())return E_FAIL;
        if(current){sl.marker(sl::PCLMarker::eRenderSubmitEnd,*current);sl.marker(sl::PCLMarker::ePresentStart,*current);}
        BOOL fullscreen=FALSE;swap->GetFullscreenState(&fullscreen,nullptr);
        UINT sync=target>0?0:interval;
        UINT present_flags=flags&~DXGI_PRESENT_DO_NOT_WAIT;
        if(!sync&&!fullscreen&&(swap_flags&DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING))present_flags|=DXGI_PRESENT_ALLOW_TEARING;
        hr=swap->Present(sync,present_flags);
        if(FAILED(hr)&&frame_count<4)log("FG Present error 0x%08lx sync=%u flags=0x%x fullscreen=%d",static_cast<unsigned long>(hr),sync,present_flags,fullscreen);
        if(current)sl.marker(sl::PCLMarker::ePresentEnd,*current);
        sl::DLSSGState state{};
        if(sl.state(state)){
            if(state.inputsProcessingCompletionFence&&state.lastPresentInputsProcessingCompletionFenceValue)
                queue->Wait(static_cast<ID3D12Fence *>(state.inputsProcessingCompletionFence),state.lastPresentInputsProcessingCompletionFenceValue);
            if(state.numFramesActuallyPresented>1)generated_count+=state.numFramesActuallyPresented-1;
            if(++frame_count==1||frame_count%240==0||state.status!=sl::DLSSGStatus::eOk){
                ULONGLONG now=GetTickCount64();double seconds=double(now-sample_tick)/1000.;
                log("FG PRESENT real=%u generated_total=%u last_presented=%u status=0x%x dynamic=%d target=%.2f",
                    frame_count,generated_count,state.numFramesActuallyPresented,static_cast<unsigned>(state.status),generation_enabled,target);
                if(seconds>0)log("FG measured: render=%.1f FPS presented=%.1f FPS (interval %.2fs)",
                    (frame_count-sample_frames)/seconds,(frame_count-sample_frames+generated_count-sample_generated)/seconds,seconds);
                sample_tick=now;sample_frames=frame_count;sample_generated=generated_count;
            }
        }
        // The queue wait above covers Streamline's asynchronous input readers.
        // Signal after it so D3D11 can safely overwrite every shared input.
        reuse_value=++fence_value;
        if(FAILED(queue->Signal(fence12,reuse_value)))return E_FAIL;
        frame_ready=false;reset_requested=!use_fg;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL value,IDXGIOutput *output) override{
        sl.options(false,target,color.width,color.height,depth.width,depth.height);generation_enabled=false;reset_requested=true;
        HRESULT hr=swap->SetFullscreenState(value,output);
        log("FG SetFullscreenState(%d): 0x%08lx",value,static_cast<unsigned long>(hr));
        // Streamline releases its proxy buffers before this call. Even a
        // temporary fullscreen failure (e.g. lost focus) requires rebuilding
        // them; otherwise its next GetBuffer returns a null proxy resource.
        fullscreen_dirty=true;
        BOOL actual=FALSE;
        if(SUCCEEDED(swap->GetFullscreenState(&actual,nullptr)))game_desc.Windowed=!actual;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL *v,IDXGIOutput **o) override{return swap->GetFullscreenState(v,o);}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC *out) override{if(!out)return E_POINTER;*out=game_desc;return S_OK;}
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) override{
        log("FG ResizeBuffers: %ux%u format=%u flags=0x%x",width,height,format,flags);
        (void)flags;sl.options(false,target,color.width,color.height,depth.width,depth.height);generation_enabled=false;
        if(!wait_cpu(reuse_value))return E_FAIL;
        release_textures();previous_valid=false;reset_requested=true;
        HRESULT hr=swap->ResizeBuffers(3,width,height,format,swap_flags);
        if(FAILED(hr))return hr;
        fullscreen_dirty=false;
        DXGI_SWAP_CHAIN_DESC1 actual{};swap->GetDesc1(&actual);
        game_desc.BufferDesc.Width=actual.Width;game_desc.BufferDesc.Height=actual.Height;game_desc.BufferDesc.Format=actual.Format;
        if(count)game_desc.BufferCount=count;
        return create_game_buffer()?S_OK:E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC *d) override{
        log("FG ResizeTarget: %ux%u refresh=%u/%u",d?d->Width:0,d?d->Height:0,d?d->RefreshRate.Numerator:0,d?d->RefreshRate.Denominator:0);
        return swap->ResizeTarget(d);
    }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput **o) override{return swap->GetContainingOutput(o);}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS *s) override{return swap->GetFrameStatistics(s);}
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT *n) override{return swap->GetLastPresentCount(n);}
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1 *d) override{if(!d)return E_POINTER;HRESULT hr=swap->GetDesc1(d);d->BufferCount=game_desc.BufferCount;d->SwapEffect=game_desc.SwapEffect;d->Flags=game_desc.Flags;return hr;}
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *d) override{return swap->GetFullscreenDesc(d);}
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND *out) override{if(!out)return E_POINTER;*out=game_desc.OutputWindow;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID iid,void **out) override{return swap->GetCoreWindow(iid,out);}
    HRESULT STDMETHODCALLTYPE Present1(UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS *params) override{(void)params;return Present(interval,flags);}
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override{return swap->IsTemporaryMonoSupported();}
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput **out) override{return swap->GetRestrictToOutput(out);}
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA *c) override{return swap->SetBackgroundColor(c);}
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA *c) override{return swap->GetBackgroundColor(c);}
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION r) override{return swap->SetRotation(r);}
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION *r) override{return swap->GetRotation(r);}
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT w,UINT h) override{return swap->SetSourceSize(w,h);}
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT *w,UINT *h) override{return swap->GetSourceSize(w,h);}
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT) override{return DXGI_ERROR_INVALID_CALL;}
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *out) override{if(!out)return E_POINTER;*out=1;return S_OK;}
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override{return nullptr;}
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F *m) override{return swap->SetMatrixTransform(m);}
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F *m) override{return swap->GetMatrixTransform(m);}
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override{return 0;}
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE t,UINT *v) override{return swap->CheckColorSpaceSupport(t,v);}
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE t) override{return swap->SetColorSpace1(t);}
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count,UINT w,UINT h,DXGI_FORMAT f,UINT flags,const UINT *,IUnknown *const *) override{return ResizeBuffers(count,w,h,f,flags);}
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE t,UINT n,void *p) override{return swap->SetHDRMetaData(t,n,p);}
    ~Bridge();
};

static std::atomic<Bridge *> current_bridge{};
static ID3D11Device *expected_device{};
static RendererLog logger{};
static wchar_t plugin_directory[MAX_PATH],log_directory[MAX_PATH];
static float requested_target=122;
static bool requested_dynamic{};
using CreateSwapFn=HRESULT (STDMETHODCALLTYPE *)(IDXGIFactory *,IUnknown *,DXGI_SWAP_CHAIN_DESC *,IDXGISwapChain **);
using CreateHwndFn=HRESULT (STDMETHODCALLTYPE *)(IDXGIFactory2 *,IUnknown *,HWND,const DXGI_SWAP_CHAIN_DESC1 *,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *,IDXGIOutput *,IDXGISwapChain1 **);
static CreateSwapFn native_create_swap;
static CreateHwndFn native_create_hwnd;

Bridge::~Bridge(){
    alive=false;Bridge *expected=this;current_bridge.compare_exchange_strong(expected,nullptr);
    if(queue&&fence12){if(sl.set_fg_options)sl.options(false,target,color.width,color.height,depth.width,depth.height);queue->Signal(fence12,++fence_value);wait_cpu(fence_value);}
    // Stop plugins before destroying graphics objects, but retain the interposer
    // module until all proxy vtables are no longer needed.
    sl.stop();
    release_textures();drop(swap);drop(commands);for(auto &a:allocators)drop(a);drop(fence11);drop(fence12);
    drop(queue);drop(proxy_device12);sl.close();drop(device12);drop(parent_factory);drop(context11_4);drop(context11);drop(device11_5);drop(device11);
    if(log)log("FG bridge closed: real=%u generated_total=%u",frame_count,generated_count);
    if(fence_event)CloseHandle(fence_event);
}

HRESULT make_bridge(ID3D11Device *device,const DXGI_SWAP_CHAIN_DESC &desc,IDXGISwapChain **out,
    const wchar_t *runtime,const wchar_t *logs,RendererLog log,float target){
    if(!out)return E_POINTER;*out=nullptr;
    Bridge *bridge=new(std::nothrow) Bridge;if(!bridge)return E_OUTOFMEMORY;
    HRESULT hr=bridge->initialize(device,desc,runtime,logs,log,target);
    if(FAILED(hr)){log("Native FG bridge creation failed: 0x%08lx",static_cast<unsigned long>(hr));bridge->Release();return hr;}
    current_bridge.store(bridge);*out=bridge;return S_OK;
}
bool is_game_device(IUnknown *object){
    ID3D11Device *device=nullptr;if(!object||FAILED(object->QueryInterface(__uuidof(ID3D11Device),reinterpret_cast<void **>(&device))))return false;
    bool same=device==expected_device;device->Release();return same;
}
HRESULT STDMETHODCALLTYPE create_swap(IDXGIFactory *factory,IUnknown *device,DXGI_SWAP_CHAIN_DESC *desc,IDXGISwapChain **out){
    if(requested_dynamic&&!current_bridge.load()&&desc&&is_game_device(device)) {
        HRESULT hr=make_bridge(expected_device,*desc,out,plugin_directory,log_directory,logger,requested_target);
        if(SUCCEEDED(hr))return hr;
        requested_dynamic=false;logger("Using original D3D11 presentation; native Dynamic MFG unavailable");
    }
    return native_create_swap(factory,device,desc,out);
}
HRESULT STDMETHODCALLTYPE create_hwnd(IDXGIFactory2 *factory,IUnknown *device,HWND hwnd,const DXGI_SWAP_CHAIN_DESC1 *desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,IDXGIOutput *restrict_output,IDXGISwapChain1 **out){
    if(requested_dynamic&&!current_bridge.load()&&desc&&is_game_device(device)) {
        DXGI_SWAP_CHAIN_DESC old{};old.BufferDesc.Width=desc->Width;old.BufferDesc.Height=desc->Height;old.BufferDesc.Format=desc->Format;
        old.SampleDesc=desc->SampleDesc;old.BufferUsage=desc->BufferUsage;old.BufferCount=desc->BufferCount;
        old.OutputWindow=hwnd;old.Windowed=fullscreen?fullscreen->Windowed:TRUE;old.SwapEffect=desc->SwapEffect;old.Flags=desc->Flags;
        IDXGISwapChain *base=nullptr;HRESULT hr=make_bridge(expected_device,old,&base,plugin_directory,log_directory,logger,requested_target);
        if(SUCCEEDED(hr)){hr=base->QueryInterface(__uuidof(IDXGISwapChain1),reinterpret_cast<void **>(out));base->Release();return hr;}
        requested_dynamic=false;logger("Using original D3D11 presentation; native Dynamic MFG unavailable");
    }
    return native_create_hwnd(factory,device,hwnd,desc,fullscreen,restrict_output,out);
}
}

extern "C" HRESULT elex_fg_install(ID3D11Device *device,RendererLog log,HMODULE module){
    if(expected_device)return S_OK;
    logger=log;
    wchar_t ini[MAX_PATH],mode[32],fps[32];
    DWORD n=GetModuleFileNameW(module,ini,MAX_PATH);if(!n||n>=MAX_PATH)return E_FAIL;
    wchar_t *slash=wcsrchr(ini,L'\\');if(!slash||(slash-ini)+25>=MAX_PATH)return E_FAIL;*slash=0;
    wcscpy(plugin_directory,ini);wcscpy(log_directory,ini);wcscat(log_directory,L"\\elex-dlss-fg");CreateDirectoryW(log_directory,nullptr);
    wcscat(ini,L"\\elex-dlss.ini");
    GetPrivateProfileStringW(L"FrameGeneration",L"Mode",L"Off",mode,32,ini);
    requested_dynamic=_wcsicmp(mode,L"Dynamic")==0;
    if(!requested_dynamic){log("Frame generation disabled in configuration");return S_FALSE;}
    GetPrivateProfileStringW(L"FrameGeneration",L"TargetFPS",L"122",fps,32,ini);
    wchar_t *end=nullptr;requested_target=static_cast<float>(wcstod(fps,&end));
    if(!end||*end||!std::isfinite(requested_target)||requested_target<0||requested_target>1000){log("Invalid FrameGeneration TargetFPS");return E_INVALIDARG;}
    IDXGIDevice *dxgi=nullptr;IDXGIAdapter *adapter=nullptr;IDXGIFactory2 *factory=nullptr;
    HRESULT hr=device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void **>(&dxgi));
    if(SUCCEEDED(hr))hr=dxgi->GetAdapter(&adapter);
    if(SUCCEEDED(hr))hr=adapter->GetParent(__uuidof(IDXGIFactory2),reinterpret_cast<void **>(&factory));
    if(SUCCEEDED(hr)){
        void **vtable=*reinterpret_cast<void ***>(factory);
        MH_STATUS a=MH_CreateHook(vtable[10],reinterpret_cast<void *>(create_swap),reinterpret_cast<void **>(&native_create_swap));
        MH_STATUS b=MH_CreateHook(vtable[15],reinterpret_cast<void *>(create_hwnd),reinterpret_cast<void **>(&native_create_hwnd));
        if(a!=MH_OK||b!=MH_OK){hr=E_FAIL;log("FG factory hooks failed: %s/%s",MH_StatusToString(a),MH_StatusToString(b));}
        else{expected_device=device;expected_device->AddRef();MH_QueueEnableHook(vtable[10]);MH_QueueEnableHook(vtable[15]);hr=MH_ApplyQueued()==MH_OK?S_OK:E_FAIL;}
    }
    drop(factory);drop(adapter);drop(dxgi);return hr;
}
extern "C" void elex_fg_simulation_start(unsigned frame){if(auto *b=current_bridge.load())b->simulation_start(frame);}
extern "C" void elex_fg_render_start(){if(auto *b=current_bridge.load())b->render_start();}
extern "C" void elex_fg_capture(ID3D11DeviceContext *context,ID3D11Texture2D *depth,ID3D11Texture2D *motion,ID3D11Resource *hudless,const float globals[192],bool reset){
    if(auto *b=current_bridge.load())b->capture(context,depth,motion,hudless,globals,reset);
}
extern "C" bool elex_fg_is_active(){auto *b=current_bridge.load();return b&&b->active();}
extern "C" HRESULT elex_fg_create_test(ID3D11Device *device,const DXGI_SWAP_CHAIN_DESC *desc,IDXGISwapChain **out,
    const wchar_t *runtime,const wchar_t *logs,RendererLog logger,float target){
    return desc?make_bridge(device,*desc,out,runtime,logs,logger,target):E_INVALIDARG;
}
