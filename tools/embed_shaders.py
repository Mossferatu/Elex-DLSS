"""Embed only original HLSL text; game shader bytecode is never included."""
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
out = root / 'build' / 'shaders.h'
lines = ['/* Generated from src/shaders by tools/embed_shaders.py. */']
for name in ['prepare', 'output', 'object_motion']:
    shader = (root / 'src' / 'shaders' / (name + '.hlsl')).read_text(encoding='utf-8')
    lines.append('static const char shader_' + name + '[] =')
    lines.extend(json.dumps(line + '\n') for line in shader.splitlines())
    lines.append(';')
out.parent.mkdir(exist_ok=True)
out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
