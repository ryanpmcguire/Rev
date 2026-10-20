"""Reproduce Rev's HLSL ports from its authoritative OpenGL primitive shaders.

This is deliberately a small, audited adapter for THESE shaders, not a general
GLSL compiler. Algorithm bodies remain in source order. Only language syntax,
stage interfaces, matrix operators and the final clip-depth convention change.
Run with --check to reject stale ports after an OpenGL shader edit.
"""
from pathlib import Path
import argparse
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1] / "source/Graphics/Primitives"
IO = re.compile(r"(?m)^(flat )?(in|out) (\w+) (\w+);[^\n]*")


def balanced(text, start):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '(': depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0: return i
    raise ValueError('Unbalanced expression')


def adapt_expressions(text):
    # HLSL file-scope const alone is an externally supplied $Globals uniform.
    # GLSL constants must remain compile-time values, including the join angle PI.
    text = re.sub(r'(?m)^const ', 'static const ', text)
    # HLSL '*' is component-wise for matrices; GLSL '*' is algebraic.
    replacements = {
        'uViewProj * uWorld * uModel': 'mul(mul(uViewProj, uWorld), uModel)',
        'uViewProj * world * vec4(aPos, 1.0)': 'mul(mul(uViewProj, world), vec4(aPos, 1.0))',
        'uWorld * uModel': 'mul(uWorld, uModel)',
        'mat3(world) * aNormal': 'mul((float3x3)world, aNormal)',
        'R * pos': 'mul(R, pos)',
    }
    for old, new in replacements.items(): text = text.replace(old, new)
    # Matrix * constructor: preserve GLSL's original left-to-right grouping.
    pattern = re.compile(r'(uProjection|mvp|mul\(mul\(uViewProj, uWorld\), uModel\)) \* vec4\(')
    while match := pattern.search(text):
        opening = text.index('vec4(', match.start()) + 4
        end = balanced(text, opening)
        text = text[:match.start()] + 'mul(' + match[1] + ', ' + text[opening-4:end+1] + ')' + text[end+1:]
    # GLSL matrix constructors enumerate columns; HLSL constructors enumerate rows.
    text = re.sub(r'mat2\(c,\s*-s,\s*s,\s*c\)', 'float2x2(c, s, -s, c)', text)
    text = re.sub(r'\bmix\b', 'lerp', text)
    text = re.sub(r'\batan\(', 'atan2(', text)  # all current uses are atan(y,x)
    text = re.sub(r'texelFetch\((\w+), ([^\n;]+?)\)\.xy', r'\1.Load(\2).xy', text)
    text = re.sub(r'texture\((\w+), (\w+)\)', r'\1.Sample(\1Sampler, \2)', text)
    for n in (2,3,4):
        text = re.sub(rf'\bvec{n}\b', f'float{n}', text)
        text = re.sub(rf'\bmat{n}\b', f'float{n}x{n}', text)
    # HLSL scalar-to-vector splats are casts, rather than one-argument constructors.
    for match in reversed(list(re.finditer(r'float[234]\(', text))):
        opening = match.end()-1
        end = balanced(text, opening)
        expression = text[opening+1:end]
        depth = 0; commas = 0
        for c in expression:
            if c == '(': depth += 1
            if c == ')': depth -= 1
            if c == ',' and depth == 0: commas += 1
        if not commas:
            text = text[:match.start()] + '((' + match[0][:-1] + ')(' + expression + '))' + text[end+1:]
    return text


def port(path, vertex_outputs):
    original = path.read_text(encoding='utf-8')
    vertex = path.suffix == '.vert'
    text = re.sub(r'^#version[^\n]*\n', '', original)
    attributes = re.findall(r'layout\(location = (\d+)\) in (\w+) (\w+);', text)
    text = re.sub(r'(?m)^layout\(location = \d+\) in \w+ \w+;[^\n]*', '', text)
    fields = IO.findall(text)
    text = IO.sub('', text)
    text = re.sub(r'layout\(std140, binding = (\d+)\) uniform (\w+)\s*\{',
                  lambda m: f'cbuffer {m[2]} : register(b{m[1]}) {{', text)
    text = re.sub(r'layout\(binding = (\d+)\) uniform sampler2D (\w+);',
                  lambda m: f'Texture2D {m[2]} : register(t{m[1]});\nSamplerState {m[2]}Sampler : register(s{m[1]});', text)
    text = re.sub(r'layout\(binding = (\d+)\) uniform samplerBuffer (\w+);',
                  lambda m: f'Buffer<float2> {m[2]} : register(t{m[1]});', text)
    # GLSL array construction differs only in delimiters in these lookup tables.
    text = re.sub(r'(const vec[234] \w+\[\d+\] = )vec[234]\[\]\((.*?)\);', r'\1{\2};', text, flags=re.S)
    text = adapt_expressions(text)
    signature = []
    for flat, _, typ, name in vertex_outputs:
        # fragColor in Rectangle.vert is never assigned OR consumed in OpenGL.
        if name == 'fragColor': continue
        semantic = list(x[3] for x in vertex_outputs).index(name)
        signature.append(f'    {"nointerpolation " if flat else ""}{typ.replace("vec", "float")} {name} : TEXCOORD{semantic};')
    interface = 'struct RevVaryings {\n    float4 position : SV_Position;\n' + '\n'.join(signature) + '\n};\n'
    if vertex:
        # These procedural quads never read the dummy GL attribute. Do not make
        # debug HLSL require a vertex stream merely to supply an unused value.
        arguments = [f'{typ.replace("vec", "float")} {name} : ATTR{slot}' for slot,typ,name in attributes
                     if name != 'dummyVertexID']
        arguments += ['uint gl_VertexID : SV_VertexID', 'uint gl_InstanceID : SV_InstanceID']
        text = re.sub(r'void main\(\)\s*\{', 'RevVaryings main(' + ', '.join(arguments) + ') {\n    RevVaryings result = (RevVaryings)0;', text)
        for _,_,_,name in vertex_outputs:
            if name != 'fragColor': text = re.sub(rf'\b{name}\b', 'result.'+name, text)
        text = re.sub(r'\bgl_Position\b', 'result.position', text)
        closing = text.rfind('}')
        text = text[:closing] + ('    // Only the API boundary changes: GL clip Z [-W,W] -> D3D [0,W].\n'
                                '    result.position.z = (result.position.z + result.position.w) * 0.5;\n'
                                '    return result;\n') + text[closing:]
    else:
        color = next(name for _, direction, typ, name in fields if direction == 'out')
        depth = 'gl_FragDepth' in re.sub(r'//[^\n]*', '', original)
        interface += 'struct RevFragment {\n    float4 color : SV_Target;\n'
        if depth: interface += '    float depth : SV_Depth;\n'
        interface += '};\n'
        text = re.sub(r'void main\(\)\s*\{', 'RevFragment main(RevVaryings input) {\n    RevFragment result = (RevFragment)0;', text)
        for _,direction,_,name in fields:
            if direction == 'in': text = re.sub(rf'\b{name}\b', 'input.'+name, text)
        text = re.sub(rf'\b{color}\b', 'result.color', text)
        text = re.sub(r'\bgl_FragDepth\b', 'result.depth', text)
        text = re.sub(r'\breturn;', 'return result;', text)
        closing = text.rfind('}')
        text = text[:closing] + '    return result;\n' + text[closing:]
    digest = hashlib.sha256(original.encode()).hexdigest()
    header = (f'// Port of {path.name}. Generated by scripts/PortDirectXShaders.py.\n'
              f'// OpenGL source SHA256: {digest}\n'
              '// Keep the OpenGL algorithm authoritative; do not independently tune this shader.\n')
    if re.search(r'layout\(|\b(?:vec[234]|mat[234]|sampler2D|samplerBuffer)\b', re.sub(r'//[^\n]*','',text)):
        raise ValueError(f'Untranslated GLSL in {path}')
    return header + interface + text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    count = 0
    for vertex in sorted(ROOT.rglob('*.vert')):
        outputs = [x for x in IO.findall(vertex.read_text()) if x[1] == 'out']
        for path in (vertex, vertex.with_suffix('.frag')):
            destination = path.with_suffix(path.suffix + '.hlsl')
            hlsl = port(path, outputs)
            if args.check:
                if not destination.exists() or destination.read_text(encoding='utf-8') != hlsl:
                    raise SystemExit(f'Stale HLSL: {destination}')
            else: destination.write_text(hlsl, encoding='utf-8', newline='\n')
            count += 1
    print(f'{"Verified" if args.check else "Wrote"} {count} HLSL shader ports.')

if __name__ == '__main__': main()
