"""Run with RenderDoc's qrenderdoc --python; all outputs stay in a private lab.

Set ELEX_CAPTURE and ELEX_LAB explicitly. Does not launch or alter the game.
Records shader metadata and selected pipeline bindings, never ships game code.
"""
import hashlib
import json
import os
from pathlib import Path
import traceback

import renderdoc as rd

capture = Path(os.environ['ELEX_CAPTURE']).resolve(strict=True)
lab = Path(os.environ['ELEX_LAB']).resolve()
lab.mkdir(parents=True, exist_ok=True)


def inspect(controller):
    resources = controller.GetResources()
    ids = {str(r.resourceId): r.resourceId for r in resources}
    if os.environ.get('ELEX_SUMMARY', '0') == '1':
        actions = []
        structured = controller.GetStructuredFile()
        def walk(items):
            for a in items:
                actions.append({'event': a.eventId, 'name': a.GetName(structured),
                                'flags': str(a.flags), 'count': a.numIndices,
                                'outputs': [str(r) for r in a.outputs], 'depth': str(a.depthOut)})
                walk(a.children)
        walk(controller.GetRootActions())
        (lab / 'actions.json').write_text(json.dumps(actions, indent=2), encoding='utf-8')
        (lab / 'textures.json').write_text(json.dumps([
            {'id': str(t.resourceId), 'width': t.width, 'height': t.height,
             'format': t.format.Name(), 'mips': t.mips, 'arraySize': t.arraysize}
            for t in controller.GetTextures()], indent=2), encoding='utf-8')
    index = []
    for resource in (resources if os.environ.get('ELEX_INDEX', '1') == '1' else []):
        if 'Shader' not in resource.name or 'View' in resource.name:
            continue
        for entry in controller.GetShaderEntryPoints(resource.resourceId):
            reflection = controller.GetShader(rd.ResourceId.Null(), resource.resourceId, entry)
            if not reflection:
                continue
            row = {
                'id': str(resource.resourceId), 'name': resource.name,
                'stage': str(entry.stage), 'sha256': hashlib.sha256(bytes(reflection.rawBytes)).hexdigest(),
                'size': len(reflection.rawBytes),
                'resources': [{'name': r.name, 'slot': r.fixedBindNumber} for r in reflection.readOnlyResources],
                'constants': [{'name': b.name, 'slot': b.fixedBindNumber, 'size': b.byteSize,
                               'variables': [{'name': v.name, 'offset': v.byteOffset} for v in b.variables]}
                              for b in reflection.constantBlocks],
            }
            names = ' '.join([r['name'] for r in row['resources']] +
                             [v['name'] for b in row['constants'] for v in b['variables']])
            if any(n.lower() in names.lower() for n in ['velocity', 'prev', 'jitter', 'reproject', 'history']):
                row['temporal_candidate'] = True
                private = lab / 'shaders'
                private.mkdir(exist_ok=True)
                (private / (str(resource.resourceId).replace('::', '_') + '.txt')).write_text(
                    controller.DisassembleShader(rd.ResourceId.Null(), reflection, ''), encoding='utf-8')
                row['usage'] = [{'event': u.eventId, 'usage': str(u.usage)}
                                for u in controller.GetUsage(resource.resourceId)]
            index.append(row)
    if index:
        (lab / 'shader-metadata.json').write_text(json.dumps(index, indent=2), encoding='utf-8')
    events = [int(x) for x in os.environ.get('ELEX_EVENTS', '7241').split(',') if x]
    if events == [0]:
        events = []
    if os.environ.get('ELEX_TRACE_VELOCITY', ''):
        velocity_id = ids['ResourceId::' + os.environ['ELEX_TRACE_VELOCITY']]
        events = sorted(set(events + [u.eventId for u in controller.GetUsage(velocity_id)
                                     if u.usage == rd.ResourceUsage.ColorTarget]))
    pipelines = []
    def variable(value):
        row = {'name': value.name, 'rows': value.rows, 'cols': value.columns,
               'type': str(value.type), 'value': list(value.value.f32v)[:value.rows*value.columns],
               'uint': list(value.value.u32v)[:value.rows*value.columns]}
        if value.members:
            row['members'] = [variable(m) for m in value.members[:64]]
        return row
    for eid in events:
        controller.SetFrameEvent(eid, True)
        pipe = controller.GetPipelineState()
        native = controller.GetD3D11PipelineState()
        reflection = pipe.GetShaderReflection(rd.ShaderStage.Pixel)
        row = {'event': eid, 'pixel_shader': str(native.pixelShader.resourceId),
               'resources': [{'slot': reflection.readOnlyResources[v.access.index].fixedBindNumber,
                              'name': reflection.readOnlyResources[v.access.index].name,
                              'resource': str(v.descriptor.resource), 'format': v.descriptor.format.Name()}
                             for v in pipe.GetReadOnlyResources(rd.ShaderStage.Pixel)
                             if v.descriptor.resource != rd.ResourceId.Null()],
               'targets': [{'resource': str(v.resource), 'format': v.format.Name()}
                           for v in native.outputMerger.renderTargets],
               'depth': str(native.outputMerger.depthTarget.resource)}
        row['constant_values'] = []
        refl = pipe.GetShaderReflection(rd.ShaderStage.Pixel)
        if refl:
            for i, block in enumerate(refl.constantBlocks):
                binding = pipe.GetConstantBlock(rd.ShaderStage.Pixel, i, 0).descriptor
                variables = controller.GetCBufferVariableContents(
                    pipe.GetGraphicsPipelineObject(), refl.resourceId, rd.ShaderStage.Pixel,
                    refl.entryPoint, i, binding.resource, binding.byteOffset, binding.byteSize)
                row['constant_values'].append({'block': block.name, 'variables': [
                    variable(v) for v in variables]})
        row['stages'] = {}
        for stage in [rd.ShaderStage.Vertex, rd.ShaderStage.Pixel, rd.ShaderStage.Compute]:
            refl = pipe.GetShaderReflection(stage)
            if not refl:
                continue
            data = {'shader': str(refl.resourceId), 'resources': [], 'uavs': [], 'constants': []}
            private = lab / 'shaders'
            private.mkdir(exist_ok=True)
            (private / (str(refl.resourceId).replace('::', '_') + '.txt')).write_text(
                controller.DisassembleShader(rd.ResourceId.Null(), refl, ''), encoding='utf-8')
            for field, descriptors in [('resources', pipe.GetReadOnlyResources(stage)),
                                       ('uavs', pipe.GetReadWriteResources(stage))]:
                for used in descriptors:
                    desc = used.descriptor
                    if desc.resource == rd.ResourceId.Null():
                        continue
                    binding = (refl.readOnlyResources if field == 'resources' else refl.readWriteResources)[used.access.index]
                    data[field].append({'slot': binding.fixedBindNumber, 'name': binding.name,
                                        'resource': str(desc.resource), 'format': desc.format.Name()})
            for i, block in enumerate(refl.constantBlocks):
                if block.name == 'cb_InstanceData':
                    continue
                binding = pipe.GetConstantBlock(stage, i, 0).descriptor
                values = controller.GetCBufferVariableContents(rd.ResourceId.Null(), refl.resourceId, stage,
                    refl.entryPoint, i, binding.resource, binding.byteOffset, binding.byteSize)
                data['constants'].append({'slot': block.fixedBindNumber, 'block': block.name,
                    'buffer': str(binding.resource), 'size': binding.byteSize, 'offset': binding.byteOffset,
                    'variables': [variable(v) for v in values]})
                if block.name == 'cb_Globals':
                    def layout(v, offset=0):
                        return {'name': v.name, 'offset': offset+v.byteOffset,
                                'rows': v.type.rows, 'columns': v.type.columns,
                                'members': [layout(m, offset+v.byteOffset) for m in v.type.members]}
                    data['constant_layout'] = [layout(v) for v in block.variables]
                    raw = controller.GetBufferData(binding.resource, binding.byteOffset, min(binding.byteSize, 1024))
                    (lab / ('globals-%d-%s.bin' % (eid, stage))).write_bytes(bytes(raw))
            row['stages'][str(stage)] = data
        pipelines.append(row)
    (lab / 'pipelines.json').write_text(json.dumps(pipelines, indent=2), encoding='utf-8')
    if os.environ.get('ELEX_IMAGE', ''):
        key = os.environ['ELEX_IMAGE']
        actions = controller.GetRootActions()
        controller.SetFrameEvent(actions[-1].eventId, True)
        save = rd.TextureSave()
        save.resourceId = ids['ResourceId::' + key]
        save.destType = rd.FileType.JPG
        save.comp.blackPoint = 0.0; save.comp.whitePoint = 1.0
        controller.SaveTexture(save, str(lab / 'capture.jpg'))
    if os.environ.get('ELEX_USAGE', ''):
        usage = {}
        for key in os.environ['ELEX_USAGE'].split(','):
            rid = ids['ResourceId::' + key]
            usage[key] = [{'event': u.eventId, 'usage': str(u.usage)} for u in controller.GetUsage(rid)]
        (lab / 'usage.json').write_text(json.dumps(usage, indent=2), encoding='utf-8')


with (lab / 'inspect.log').open('w', encoding='utf-8') as log:
    cap = controller = None
    try:
        cap = rd.OpenCaptureFile()
        status = cap.OpenFile(str(capture), '', None)
        log.write('OpenFile: %s\n' % status)
        log.flush()
        if status != rd.ResultCode.Succeeded:
            raise RuntimeError(str(status))
        status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
        log.write('OpenCapture: %s\n' % status)
        log.flush()
        if status != rd.ResultCode.Succeeded:
            raise RuntimeError(str(status))
        inspect(controller)
        log.write('SUCCESS\n')
    except Exception:
        log.write(traceback.format_exc())
    finally:
        if controller:
            controller.Shutdown()
        if cap:
            cap.Shutdown()
raise SystemExit
