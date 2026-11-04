#!/usr/bin/env python3
"""Rebuild the shaders this port adds or changes (ppsx/glsl/prebuilt/textured.tag*.inc, seams.comp.inc;
the textured ones: vUV is highp), with glslang (deps/glslang-*/bin/glslang.exe, from github.com/KhronosGroup/glslang releases).
Only needed after changing ppsx/glsl; the build uses the .inc files.

usage: python runtime/vk/build_shaders.py"""
import glob, os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GLSL = os.path.join(HERE, 'ppsx', 'glsl')
ROOT = os.path.dirname(os.path.dirname(HERE))
# output .inc: source, defines (as in ppsx/glsl/rebuild_shaders.sh)
SHADERS = {
    'ppsx/glsl/prebuilt/textured.tag.frag.inc': ('ppsx/glsl/primitive.frag', ['TEXTURED', 'TAG']),
    'ppsx/glsl/prebuilt/textured.tag.unscaled.frag.inc': ('ppsx/glsl/primitive.frag', ['TEXTURED', 'UNSCALED', 'TAG']),
    'seams.comp.inc': ('seams.comp', []),
    'ppsx/glsl/prebuilt/textured.vert.inc': ('ppsx/glsl/primitive.vert', ['TEXTURED']),
    'ppsx/glsl/prebuilt/textured.unscaled.vert.inc': ('ppsx/glsl/primitive.vert', ['TEXTURED', 'UNSCALED']),
    'ppsx/glsl/prebuilt/textured.frag.inc': ('ppsx/glsl/primitive.frag', ['TEXTURED']),
    'ppsx/glsl/prebuilt/textured.unscaled.frag.inc': ('ppsx/glsl/primitive.frag', ['TEXTURED', 'UNSCALED']),
    'ppsx/glsl/prebuilt/textured.msaa.frag.inc': ('ppsx/glsl/primitive.frag', ['TEXTURED', 'MSAA']),
    'ppsx/glsl/prebuilt/textured.msaa.unscaled.frag.inc': ('ppsx/glsl/primitive.frag', ['TEXTURED', 'MSAA', 'UNSCALED']),
    'ppsx/glsl/prebuilt/feedback.frag.inc': ('ppsx/glsl/primitive_feedback.frag', ['TEXTURED']),
    'ppsx/glsl/prebuilt/feedback.unscaled.frag.inc': ('ppsx/glsl/primitive_feedback.frag', ['TEXTURED', 'UNSCALED']),
    'ppsx/glsl/prebuilt/feedback.msaa.frag.inc': ('ppsx/glsl/primitive_feedback.frag', ['TEXTURED', 'MSAA']),
    'ppsx/glsl/prebuilt/feedback.msaa.unscaled.frag.inc': ('ppsx/glsl/primitive_feedback.frag', ['TEXTURED', 'MSAA', 'UNSCALED']),
}


def main():
    tools = sorted(glob.glob(os.path.join(ROOT, 'deps', 'glslang-*', 'bin', 'glslang.exe')))
    if not tools:
        sys.exit('glslang not found in deps/glslang-*/bin')
    for out, (src, defines) in SHADERS.items():
        text = open(os.path.join(HERE, src)).read()
        # glslc allows #include by default, glslang wants the extension named
        text = text.replace('#version 450\n', '#version 450\n#extension GL_GOOGLE_include_directive : require\n', 1)
        with tempfile.TemporaryDirectory() as tmp:
            stage = os.path.join(tmp, os.path.basename(src))
            spv = os.path.join(tmp, 'out.spv')
            open(stage, 'w').write(text)
            cmd = [tools[-1], '-V', '--target-env', 'vulkan1.0', '-I' + GLSL, '-o', spv, stage]
            cmd += ['-D' + d for d in defines]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode:
                sys.exit(r.stdout + r.stderr)
            words = struct.unpack('<%dI' % (os.path.getsize(spv) // 4), open(spv, 'rb').read())
        lines = [','.join('0x%08x' % w for w in words[i:i + 4]) for i in range(0, len(words), 4)]
        with open(os.path.join(HERE, out), 'w', newline='\n') as f:
            f.write('{' + ',\n'.join(lines) + '}\n')
        print(out, len(words) * 4, 'bytes')


if __name__ == '__main__':
    main()
