#ifndef ELEX_PIPELINE_STATE_H
#define ELEX_PIPELINE_STATE_H
#include <d3d11_1.h>

typedef struct {
    IUnknown *shader;
    ID3D11ClassInstance *instances[256];UINT instance_count;
    ID3D11ShaderResourceView *resources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D11SamplerState *samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
    ID3D11Buffer *constants[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT first[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT],counts[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
} ElexShaderState;
typedef struct {
    ElexShaderState vs,hs,ds,gs,ps,cs;
    ID3D11InputLayout *layout;
    ID3D11Buffer *vertices[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT],*indices;
    UINT strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT],offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT],index_offset;
    DXGI_FORMAT index_format;D3D11_PRIMITIVE_TOPOLOGY topology;
    ID3D11Buffer *streamout[D3D11_SO_BUFFER_SLOT_COUNT];
    ID3D11RasterizerState *rasterizer;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];UINT viewport_count;
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];UINT scissor_count;
    ID3D11RenderTargetView *targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ID3D11DepthStencilView *depth;
    ID3D11BlendState *blend;FLOAT blend_factor[4];UINT sample_mask;
    ID3D11DepthStencilState *depth_state;UINT stencil_ref;
    ID3D11UnorderedAccessView *cs_uavs[D3D11_1_UAV_SLOT_COUNT],*ps_uavs[D3D11_1_UAV_SLOT_COUNT];UINT uav_count;
    ID3D11Predicate *predicate;BOOL predicate_value;
} ElexPipelineState;
void elex_pipeline_save(ID3D11DeviceContext *,ID3D11DeviceContext1 *,UINT uav_count,ElexPipelineState *);
void elex_pipeline_restore(ID3D11DeviceContext *,ID3D11DeviceContext1 *,ElexPipelineState *);
#endif
