"""Compile the primitive HLSL and compare reflection with original GLSL std140.

Usage: python scripts/ValidateDirectXShaders.py path/to/CompileShaders.exe
The adapter's --check and this independent layout check should both pass.
"""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1] / 'source/Graphics/Primitives'


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def layout(body, types):
    offset = 0
    fields = {}
    for declaration in body.split(';'):
        if not declaration.strip():
            continue
        typ, names = declaration.strip().split(None, 1)
        alignment, size = types[typ]  # Reject new, unaudited types.
        for name in names.split(','):
            match = re.fullmatch(r'\s*(\w+)(?:\[([\d\s*]+)\])?\s*', name)
            if not match:
                raise ValueError(f'Unsupported uniform declaration: {declaration}')
            field_alignment, field_size = alignment, size
            if match[2]:
                count = 1
                for factor in match[2].split('*'):
                    count *= int(factor)
                field_alignment = align(alignment, 16)
                field_size = align(size, field_alignment) * count
            offset = align(offset, field_alignment)
            fields[match[1]] = (offset, field_size)
            offset += field_size
    return fields, align(offset, 16)


def expected_layouts():
    expected = {}
    for path in sorted(ROOT.rglob('*')):
        if path.suffix not in ('.vert', '.frag'):
            continue
        text = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
        types = {'float': (4, 4), 'uint': (4, 4), 'int': (4, 4),
                 'vec2': (8, 8), 'vec3': (16, 12), 'vec4': (16, 16), 'mat4': (16, 64)}
        for name, body in re.findall(r'struct\s+(\w+)\s*\{([^}]+)\};', text):
            _, size = layout(body, types)
            types[name] = (16, size)
        for slot, name, body in re.findall(
                r'layout\(std140, binding = (\d+)\) uniform (\w+)\s*\{([^}]+)\};', text):
            fields, _ = layout(body, types)
            expected[(path.name + '.hlsl', name)] = (int(slot), fields)
    return expected


def main():
    expected = expected_layouts()
    result = subprocess.run([sys.argv[1], str(ROOT)], check=True, capture_output=True, text=True)
    checked = 0
    for line in result.stdout.splitlines():
        if line.startswith('Compiled '):
            print(line)
            continue
        file, variant, buffer, slot, name, offset, size = line.split('|')
        key = (file, buffer)
        if key not in expected:
            raise AssertionError(f'Unexpected cbuffer (e.g. accidental global uniform): {line}')
        expected_slot, fields = expected[key]
        actual = (int(offset), int(size))
        if int(slot) != expected_slot or actual != fields[name]:
            raise AssertionError(f'GLSL/HLSL layout mismatch: {line}; GLSL b{expected_slot} {fields[name]}')
        checked += 1
    if checked == 0:
        raise AssertionError('No reflected uniforms checked')
    print(f'Checked {checked} reflected uniform offsets, sizes and bindings against GLSL std140.')


if __name__ == '__main__':
    main()
