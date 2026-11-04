#define COBJMACROS
#include "pipeline_state.h"
#include <string.h>
#define DROP(p) do{if(p){IUnknown_Release((IUnknown *)(p));(p)=NULL;}}while(0)
#define SRVS D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT
#define SAMPLERS D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT
#define CBS D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
#define VBS D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT
#define RTS D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT
#define SO D3D11_SO_BUFFER_SLOT_COUNT

void elex_pipeline_save(ID3D11DeviceContext *ctx,ID3D11DeviceContext1 *ctx1,UINT uav_count,ElexPipelineState *s)
{
    memset(s,0,sizeof(*s));s->uav_count=uav_count;
#define SAVE(stage,lower,type) \
    s->lower.instance_count=256; \
    ID3D11DeviceContext_##stage##GetShader(ctx,(ID3D11##type##Shader **)&s->lower.shader,s->lower.instances,&s->lower.instance_count); \
    ID3D11DeviceContext_##stage##GetShaderResources(ctx,0,SRVS,s->lower.resources); \
    ID3D11DeviceContext_##stage##GetSamplers(ctx,0,SAMPLERS,s->lower.samplers); \
    ID3D11DeviceContext1_##stage##GetConstantBuffers1(ctx1,0,CBS,s->lower.constants,s->lower.first,s->lower.counts)
    SAVE(VS,vs,Vertex);SAVE(HS,hs,Hull);SAVE(DS,ds,Domain);
    SAVE(GS,gs,Geometry);SAVE(PS,ps,Pixel);SAVE(CS,cs,Compute);
#undef SAVE
    ID3D11DeviceContext_IAGetInputLayout(ctx,&s->layout);
    ID3D11DeviceContext_IAGetVertexBuffers(ctx,0,VBS,s->vertices,s->strides,s->offsets);
    ID3D11DeviceContext_IAGetIndexBuffer(ctx,&s->indices,&s->index_format,&s->index_offset);
    ID3D11DeviceContext_IAGetPrimitiveTopology(ctx,&s->topology);
    ID3D11DeviceContext_SOGetTargets(ctx,SO,s->streamout);
    ID3D11DeviceContext_RSGetState(ctx,&s->rasterizer);
    s->viewport_count=s->scissor_count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11DeviceContext_RSGetViewports(ctx,&s->viewport_count,s->viewports);
    ID3D11DeviceContext_RSGetScissorRects(ctx,&s->scissor_count,s->scissors);
    ID3D11DeviceContext_OMGetRenderTargets(ctx,RTS,s->targets,&s->depth);
    ID3D11DeviceContext_OMGetBlendState(ctx,&s->blend,s->blend_factor,&s->sample_mask);
    ID3D11DeviceContext_OMGetDepthStencilState(ctx,&s->depth_state,&s->stencil_ref);
    ID3D11DeviceContext_OMGetRenderTargetsAndUnorderedAccessViews(ctx,0,NULL,NULL,0,uav_count,s->ps_uavs);
    ID3D11DeviceContext_CSGetUnorderedAccessViews(ctx,0,uav_count,s->cs_uavs);
    ID3D11DeviceContext_GetPredication(ctx,&s->predicate,&s->predicate_value);
}

void elex_pipeline_restore(ID3D11DeviceContext *ctx,ID3D11DeviceContext1 *ctx1,ElexPipelineState *s)
{
    // Explicit restoration avoids activating D3D10/11 emulation states, which
    // can make overlays choose an incompatible API. Getters own one reference
    // per returned binding, released after restoring the corresponding slot.
    ID3D11DeviceContext_ClearState(ctx);
    UINT preserve[D3D11_1_UAV_SLOT_COUNT];for(UINT i=0;i<D3D11_1_UAV_SLOT_COUNT;++i)preserve[i]=~0u;
    UINT rt_count=0;for(UINT i=0;i<RTS;++i)if(s->targets[i])rt_count=i+1;
    ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews(ctx,rt_count,s->targets,s->depth,rt_count,s->uav_count-rt_count,s->ps_uavs+rt_count,preserve);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx,0,s->uav_count,s->cs_uavs,preserve);
#define RESTORE(stage,lower,type) \
    ID3D11DeviceContext_##stage##SetShader(ctx,(ID3D11##type##Shader *)s->lower.shader,s->lower.instances,s->lower.instance_count); \
    ID3D11DeviceContext_##stage##SetShaderResources(ctx,0,SRVS,s->lower.resources); \
    ID3D11DeviceContext_##stage##SetSamplers(ctx,0,SAMPLERS,s->lower.samplers); \
    ID3D11DeviceContext1_##stage##SetConstantBuffers1(ctx1,0,CBS,s->lower.constants,s->lower.first,s->lower.counts)
    RESTORE(VS,vs,Vertex);RESTORE(HS,hs,Hull);RESTORE(DS,ds,Domain);
    RESTORE(GS,gs,Geometry);RESTORE(PS,ps,Pixel);RESTORE(CS,cs,Compute);
#undef RESTORE
    ID3D11DeviceContext_IASetInputLayout(ctx,s->layout);
    ID3D11DeviceContext_IASetVertexBuffers(ctx,0,VBS,s->vertices,s->strides,s->offsets);
    ID3D11DeviceContext_IASetIndexBuffer(ctx,s->indices,s->index_format,s->index_offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx,s->topology);
    // -1 preserves each stream-output buffer's append position.
    ID3D11DeviceContext_SOSetTargets(ctx,SO,s->streamout,preserve);
    ID3D11DeviceContext_RSSetState(ctx,s->rasterizer);
    ID3D11DeviceContext_RSSetViewports(ctx,s->viewport_count,s->viewports);
    ID3D11DeviceContext_RSSetScissorRects(ctx,s->scissor_count,s->scissors);
    ID3D11DeviceContext_OMSetBlendState(ctx,s->blend,s->blend_factor,s->sample_mask);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx,s->depth_state,s->stencil_ref);
    ID3D11DeviceContext_SetPredication(ctx,s->predicate,s->predicate_value);
    ElexShaderState *stages[]={&s->vs,&s->hs,&s->ds,&s->gs,&s->ps,&s->cs};
    for(UINT j=0;j<6;++j){ElexShaderState *stage=stages[j];
        DROP(stage->shader);for(UINT i=0;i<stage->instance_count;++i)DROP(stage->instances[i]);
        for(UINT i=0;i<SRVS;++i)DROP(stage->resources[i]);
        for(UINT i=0;i<SAMPLERS;++i)DROP(stage->samplers[i]);
        for(UINT i=0;i<CBS;++i)DROP(stage->constants[i]);
    }
    DROP(s->layout);DROP(s->indices);for(UINT i=0;i<VBS;++i)DROP(s->vertices[i]);
    for(UINT i=0;i<SO;++i)DROP(s->streamout[i]);
    DROP(s->rasterizer);DROP(s->blend);DROP(s->depth_state);DROP(s->depth);DROP(s->predicate);
    for(UINT i=0;i<RTS;++i)DROP(s->targets[i]);
    for(UINT i=0;i<s->uav_count;++i){DROP(s->cs_uavs[i]);DROP(s->ps_uavs[i]);}
}
