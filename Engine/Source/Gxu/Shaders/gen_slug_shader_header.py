"""Regenerate GxuFontSlugShader.h from the compiled shader blobs.

The .ps / .cso blobs are produced with fxc (/O3) before running this script,
from this directory:

    fxc /T ps_5_0 /E main /O3 /D RIM=0 /D SOLID_RED=0 /Fo FontSlugSM5.cso FontSlug11.hlsl
    fxc /T ps_5_0 /E main /O3 /D RIM=1 /D SOLID_RED=0 /Fo FontSlugRimSM5.cso FontSlug11.hlsl
    fxc /T vs_5_0 /E main /O3 /Fo FontSlugVSSM5.cso FontSlugVS11.hlsl

The SM3 blobs come from FontSlug.hlsl / FontSlugVS.hlsl with ps_3_0 / vs_3_0.
Run this script from the Shaders directory; it writes the header next to Gxu.
"""

from pathlib import Path


def dump(path, name, comment):
    data = Path(path).read_bytes()
    assert len(data) % 4 == 0, len(data)
    words = [int.from_bytes(data[i:i + 4], 'little') for i in range(0, len(data), 4)]
    lines = [comment, '', 'static const uint32_t %s[] = {' % name]
    for i in range(0, len(words), 4):
        lines.append('    ' + ', '.join('0x%08Xu' % w for w in words[i:i + 4]) + ',')
    lines += ['};', '', 'static const uint32_t %sSize = %d;' % (name, len(data)), '']
    return '\n'.join(lines)


header = [
    '#ifndef ENGINE_SOURCE_GXU_GXUFONTSLUGSHADER_H',
    '#define ENGINE_SOURCE_GXU_GXUFONTSLUGSHADER_H',
    '',
    '#include <cstdint>',
    '',
    '// Slug text shaders, compiled from Shaders/*.hlsl with fxc (/O3).',
    '//',
    '// Two flavours per stage: SM3 bytecode from FontSlug.hlsl / FontSlugVS.hlsl',
    '// for the D3D9 backends, SM5 DXBC from FontSlug11.hlsl / FontSlugVS11.hlsl',
    '// for D3D11. The C++ side picks the matching array via WantsSM5(), and the',
    '// D3D11 device takes the DXBC as-is instead of running it through the SM3',
    '// translator. Generated file: edit the .hlsl and rerun, never edit by hand.',
    '',
]
header.append(dump('FontSlugSM3.ps', 'g_gxuFontSlugPixelShader', '// FontSlug.hlsl, ps_3_0, RIM=0'))
header.append(dump('FontSlugSM5.cso', 'g_gxuFontSlugPixelShaderSM5', '// FontSlug11.hlsl, ps_5_0, RIM=0'))
header.append(dump('FontSlugVSSM3.ps', 'g_gxuFontSlugVertexShader', '// FontSlugVS.hlsl, vs_3_0'))
header.append(dump('FontSlugVSSM5.cso', 'g_gxuFontSlugVertexShaderSM5', '// FontSlugVS11.hlsl, vs_5_0'))
header.append(dump('FontSlugRimSM3.ps', 'g_gxuFontSlugRimShader', '// FontSlug.hlsl, ps_3_0, RIM=1'))
header.append(dump('FontSlugRimSM5.cso', 'g_gxuFontSlugRimShaderSM5', '// FontSlug11.hlsl, ps_5_0, RIM=1'))
header += ['#endif', '']

out = Path(__file__).resolve().parent.parent / 'GxuFontSlugShader.h'
with open(out, 'w', newline='\n') as f:
    f.write('\n'.join(header))
print('ok')
