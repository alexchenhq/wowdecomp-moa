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
header.append('// Slug text shaders, compiled from Shaders/FontSlug.hlsl and')
header.append('// Shaders/FontSlugVS.hlsl with fxc (/T ps_3_0, /T vs_3_0, /O3).')
header.append('//')
header.append('// Pixel shader: 456 slots, four scanline slices plus a per-curve y range cull.')
header.append('// Vertex shader: 8 slots, project and pass TEXCOORD0/TEXCOORD1/COLOR0 through.')
header.append('')
header.append(dump('FontSlug.ps', 'g_gxuFontSlugPixelShader', None,
                   '// FontSlug.ps'))
header.append(dump('FontSlugVS.ps', 'g_gxuFontSlugVertexShader', None,
                   '// FontSlugVS.ps'))
header.append('#endif')
header.append('')

open('C:/Users/alexc/WorkBuddy/2026-10-24-22-17-35/wowdecomp-src/Engine/Source/Gxu/GxuFontSlugShader.h', 'w', newline='\n').write('\n'.join(header))
print('ok')
