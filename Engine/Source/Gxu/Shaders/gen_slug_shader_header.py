import sys

def dump(path, name, out, comment):
    data = open(path, 'rb').read()
    assert len(data) % 4 == 0, len(data)
    words = [int.from_bytes(data[i:i+4], 'little') for i in range(0, len(data), 4)]
    lines = []
    lines.append(comment)
    lines.append('')
    lines.append('static const uint32_t %s[] = {' % name)
    for i in range(0, len(words), 4):
        chunk = ', '.join('0x%08Xu' % w for w in words[i:i+4])
        lines.append('    ' + chunk + ',')
    lines.append('};')
    lines.append('')
    lines.append('static const uint32_t %sSize = %d;' % (name, len(data)))
    lines.append('')
    return '\n'.join(lines)

header = []
header.append('#ifndef ENGINE_SOURCE_GXU_GXUFONTSLUGSHADER_H')
header.append('#define ENGINE_SOURCE_GXU_GXUFONTSLUGSHADER_H')
header.append('')
header.append('#include <cstdint>')
header.append('')
header.append('// Slug text shaders, compiled from Shaders/*.hlsl with fxc (/O3).')
header.append('//')
header.append('// Two flavours per stage: SM3 bytecode for the D3D9 backends, SM5 DXBC for')
header.append('// D3D11. The source picks between them with SLUG_SM5; the C++ side picks the')
header.append('// matching array from the device API, and the D3D11 device takes the DXBC')
header.append('// as-is instead of running it through the SM3 translator.')
header.append('')
header.append(dump('FontSlugSM3.ps', 'g_gxuFontSlugPixelShader', None, '// FontSlug.hlsl, ps_3_0, RIM=0'))
header.append(dump('FontSlugSM5.cso', 'g_gxuFontSlugPixelShaderSM5', None, '// FontSlug.hlsl, ps_5_0, RIM=0'))
header.append(dump('FontSlugVSSM3.ps', 'g_gxuFontSlugVertexShader', None, '// FontSlugVS.hlsl, vs_3_0'))
header.append(dump('FontSlugVSSM5.cso', 'g_gxuFontSlugVertexShaderSM5', None, '// FontSlugVS.hlsl, vs_5_0'))
header.append(dump('FontSlugRimSM3.ps', 'g_gxuFontSlugRimShader', None, '// FontSlug.hlsl, ps_3_0, RIM=1'))
header.append(dump('FontSlugRimSM5.cso', 'g_gxuFontSlugRimShaderSM5', None, '// FontSlug.hlsl, ps_5_0, RIM=1'))
header.append('#endif')
header.append('')

open('C:/Users/alexc/WorkBuddy/2026-10-24-22-17-35/wowdecomp-src/Engine/Source/Gxu/GxuFontSlugShader.h', 'w', newline='\n').write('\n'.join(header))
print('ok')
