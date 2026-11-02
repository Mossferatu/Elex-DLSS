"""Test the compiled mod on a private RenderDoc capture, not in the game.

Set ELEX_CAPTURE, ELEX_LAB and ELEX_REPLAY_DLL. A native texture-creation
observer attaches the mod to RenderDoc's replay device before game shaders load.
Only the local test build exports elex_replay_attach.
"""
import ctypes as C
import os
from pathlib import Path
import traceback
import json
import renderdoc as rd

lab = Path(os.environ['ELEX_LAB']); lab.mkdir(parents=True, exist_ok=True)
P=C.c_void_p; U=C.c_uint; H=C.c_long
keep=[]

def table(o):
    return C.cast(o,C.POINTER(C.POINTER(P))).contents

def release(o):
    if o:
        C.WINFUNCTYPE(U,P)(table(o)[2])(o)

with (lab/'replay-mod.log').open('w',encoding='utf-8') as log:
    cap=controller=None
    try:
        mod=C.WinDLL(os.environ['ELEX_REPLAY_DLL'])
        mod.elex_replay_attach.argtypes=[P];mod.elex_replay_attach.restype=H
        d3d=C.WinDLL('d3d11.dll')
        create=d3d.D3D11CreateDevice
        create.argtypes=[P,U,P,U,P,U,U,C.POINTER(P),P,C.POINTER(P)];create.restype=H
        dummy=P();context=P()
        hr=create(None,1,None,0,None,0,7,C.byref(dummy),None,C.byref(context))
        if hr<0:raise RuntimeError('dummy device 0x%08x'%(hr&0xffffffff))
        address=C.addressof(table(dummy).contents)+5*C.sizeof(P)
        F=C.WINFUNCTYPE(H,P,P,P,C.POINTER(P))
        original=F(table(dummy)[5]); attached=[]
        @F
        def texture(device,desc,data,out):
            dimensions=C.cast(desc,C.POINTER(U))
            if not attached and dimensions[0]>=640 and dimensions[1]>=360:
                attached.append(device)
                result=mod.elex_replay_attach(device)
                log.write('Attached replay device 0x%x: 0x%08x\n'%(device,result&0xffffffff));log.flush()
            return original(device,desc,data,out)
        keep.extend([original,texture])
        kernel=C.WinDLL('kernel32');kernel.VirtualProtect.argtypes=[P,C.c_size_t,U,C.POINTER(U)]
        old=U()
        if not kernel.VirtualProtect(address,8,4,C.byref(old)):raise RuntimeError('VirtualProtect failed')
        table(dummy)[5]=C.cast(texture,P).value
        kernel.VirtualProtect(address,8,old.value,C.byref(old))
        cap=rd.OpenCaptureFile()
        status=cap.OpenFile(os.environ['ELEX_CAPTURE'],'',None)
        log.write('Open: %s\n'%status);log.flush()
        status,controller=cap.OpenCapture(rd.ReplayOptions(),None)
        log.write('Replay: %s\n'%status);log.flush()
        if status!=rd.ResultCode.Succeeded:raise RuntimeError(str(status))
        if not attached:raise RuntimeError('No replay device attached')
        ids={str(r.resourceId):r.resourceId for r in controller.GetResources()}
        end=controller.GetRootActions()[-1].eventId
        for i in range(3):
            controller.SetFrameEvent(end,True)
            values=[]
            for x,y in [(1800,1200),(2300,800),(1000,500)]:
                value=controller.PickPixel(ids['ResourceId::50'],x,y,rd.Subresource(),rd.CompType.UNorm)
                values.append(list(value.floatValue))
            log.write('frame %d %s\n'%(i,json.dumps(values)));log.flush()
        save=rd.TextureSave();save.resourceId=ids['ResourceId::50'];save.destType=rd.FileType.JPG
        save.alpha=rd.AlphaMapping.Discard
        controller.SaveTexture(save,str(lab/'dlss-replay.jpg'))
        log.write('SUCCESS: check NGX evaluation log and image\n');log.flush()
        kernel.VirtualProtect(address,8,4,C.byref(old));table(dummy)[5]=C.cast(original,P).value
        kernel.VirtualProtect(address,8,old.value,C.byref(old))
        release(context);release(dummy)
    except Exception:
        log.write(traceback.format_exc());log.flush()
    finally:
        if controller:controller.Shutdown()
        if cap:cap.Shutdown()
raise SystemExit
