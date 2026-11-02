"""Read-only string-reference/nearby-disassembly helper; output stays private.

Requires capstone in the development environment. Never modifies ELEX.exe.
"""
import argparse
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

parser = argparse.ArgumentParser()
parser.add_argument('binary', type=Path)
parser.add_argument('--term')
parser.add_argument('--address', type=lambda s: int(s, 0))
parser.add_argument('--length', type=lambda s: int(s, 0), default=512)
parser.add_argument('--pointer', type=lambda s: int(s, 0))
parser.add_argument('--xref', type=lambda s: int(s, 0))
parser.add_argument('--calls', type=lambda s: int(s, 0))
parser.add_argument('--table', type=lambda s: int(s, 0))
args = parser.parse_args()
data = args.binary.read_bytes()
nt = struct.unpack_from('<I', data, 60)[0]
opt = nt + 24
image = struct.unpack_from('<Q', data, opt+24)[0]
count = struct.unpack_from('<H', data, nt+6)[0]
size = struct.unpack_from('<H', data, nt+20)[0]
sections = []
for i in range(count):
    p = opt + size + 40*i
    vsize, rva, rawsize, raw = struct.unpack_from('<IIII', data, p+8)
    sections.append((data[p:p+8].rstrip(b'\0'), image+rva, rawsize, raw))
def address(offset):
    for _, va, length, raw in sections:
        if raw <= offset < raw+length:
            return va+offset-raw
    raise ValueError(offset)
def offset(va):
    for _, start, length, raw in sections:
        if start <= va < start+length:
            return raw+va-start
    raise ValueError(va)
md=Cs(CS_ARCH_X86,CS_MODE_64)
def disasm(va, length):
    off=offset(va)
    for a,n,mn,op in md.disasm_lite(data[off:off+length],va):
        print('%016x  %-9s %s'%(a,mn,op))
if args.address:
    disasm(args.address,args.length)
if args.pointer:
    for m in re.finditer(re.escape(struct.pack('<Q',args.pointer)),data):
        print('POINTER',hex(address(m.start())))
        at=m.start()
        print([hex(v) for v in struct.unpack_from('<12Q',data,at)])
if args.table:
    off=offset(args.table)
    for i in range(args.length//8):
        ptr=struct.unpack_from('<Q',data,off+i*8)[0]
        print('SLOT',i,hex(ptr))
        if 0x140001000<=ptr<0x140b00000:
            disasm(ptr,24)
if args.calls:
    for name,va,length,raw in sections:
        if name!=b'.text':continue
        code=data[raw:raw+length]
        for m in re.finditer(b'\xe8',code):
            i=m.start()
            if i+5<=length and va+i+5+struct.unpack_from('<i',code,i+1)[0]==args.calls:
                print('CALLER',hex(va+i));disasm(va+i,args.length)
if args.xref:
    for name,va,length,raw in sections:
        if name!=b'.text':continue
        code=data[raw:raw+length]
        for m in re.finditer(rb'[\x48\x4c][\x8d\x8b][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',code):
            i=m.start();disp=struct.unpack_from('<i',code,i+3)[0]
            if va+i+7+disp==args.xref:
                print('XREF',hex(va+i));disasm(va+i,args.length)
if args.term:
    locations = [m.start() for m in re.finditer(re.escape(args.term.encode()),data)]
    targets = {address(pos):pos for pos in locations}
    print('strings',[(hex(k),hex(v)) for k,v in targets.items()])
    for name,va,length,raw in sections:
        if name!=b'.text':continue
        code=data[raw:raw+length]
        for m in re.finditer(rb'[\x48\x4c][\x8d\x8b][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',code):
            i=m.start();disp=struct.unpack_from('<i',code,i+3)[0]
            target=va+i+7+disp
            if target in targets:
                print('REFERENCE',hex(va+i),'target',hex(target))
                disasm(va+i,args.length)
