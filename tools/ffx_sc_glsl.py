#!/usr/bin/env python3
"""FidelityFX-SC's GLSL path outside Windows: compiles a FidelityFX SDK (1.1.x) pass shader for every permutation with
glslangValidator and writes the headers the SDK's Vulkan backend includes (<name>_permutations.h, one header per unique
SPIR-V, a gcc depfile), in the same layout as sdk/tools/ffx_shader_compiler (glsl_compiler.cpp, ffx_sc.cpp).
  ffx_sc_glsl.py --glslang EXE --output DIR --name N [--name N2 ...] [--perm DEF=v0,v1 ...] [-DX=Y ...] [-IDIR ...]
                 [--arg A ...] SHADER
Every --name gets the same SPIR-V (the GLSL wave64 build is the wave32 one: FidelityFX-SC passes it no extra args)."""
import argparse, concurrent.futures, hashlib, os, pathlib, struct, subprocess, sys, tempfile

# --- SPIR-V reflection: the descriptor bindings, as SPIRV-Reflect reports them (sorted by binding, then id) ---
OP_NAME, OP_DECORATE, OP_MEMBER_DECORATE = 5, 71, 72
OP_TYPE_IMAGE, OP_TYPE_SAMPLER, OP_TYPE_SAMPLED_IMAGE = 25, 26, 27
OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER = 28, 29, 30, 32
OP_CONSTANT, OP_VARIABLE, OP_TYPE_ACCEL = 43, 59, 5341
DEC_BLOCK, DEC_BUFFER_BLOCK, DEC_NON_WRITABLE, DEC_BINDING, DEC_SET = 2, 3, 24, 33, 34
SC_UNIFORM_CONSTANT, SC_UNIFORM, SC_STORAGE_BUFFER = 0, 2, 12
DIM_BUFFER = 5
KINDS = ['CBV', 'TextureSRV', 'TextureUAV', 'BufferSRV', 'BufferUAV', 'Sampler', 'RTAccelerationStructure']


def spirv_string(words):
    raw = struct.pack(f'<{len(words)}I', *words)
    return raw[:raw.index(b'\0')].decode()


def reflect(spv):
    words = struct.unpack(f'<{len(spv) // 4}I', spv)
    names, decos, member_nonwritable, types, consts, variables = {}, {}, {}, {}, {}, []
    i = 5
    while i < len(words):
        op, n = words[i] & 0xFFFF, words[i] >> 16
        a = words[i + 1:i + n]
        if op == OP_NAME:
            names[a[0]] = spirv_string(a[1:])
        elif op == OP_DECORATE:
            decos.setdefault(a[0], {})[a[1]] = a[2] if len(a) > 2 else True
        elif op == OP_MEMBER_DECORATE and a[2] == DEC_NON_WRITABLE:
            member_nonwritable.setdefault(a[0], set()).add(a[1])
        elif op in (OP_TYPE_IMAGE, OP_TYPE_SAMPLER, OP_TYPE_SAMPLED_IMAGE, OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY,
                    OP_TYPE_STRUCT, OP_TYPE_POINTER, OP_TYPE_ACCEL):
            types[a[0]] = (op, a[1:])
        elif op == OP_CONSTANT:
            consts[a[1]] = a[2]
        elif op == OP_VARIABLE:
            variables.append((a[1], a[0], a[2]))  # id, pointer type, storage class
        i += n

    out = {k: [] for k in KINDS}
    found = []
    for vid, ptype, sc in variables:
        d = decos.get(vid, {})
        if DEC_BINDING not in d or sc not in (SC_UNIFORM_CONSTANT, SC_UNIFORM, SC_STORAGE_BUFFER):
            continue
        tid = types[ptype][1][1]
        count = 1
        while types.get(tid, (None,))[0] in (OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY):
            op, ops = types[tid]
            count *= consts[ops[1]] if op == OP_TYPE_ARRAY else 0
            tid = ops[0]
        op, ops = types[tid]
        td = decos.get(tid, {})
        if op == OP_TYPE_STRUCT:
            if sc == SC_UNIFORM and DEC_BLOCK in td:
                kind = 'CBV'
            else:  # storage buffer: an SRV when nothing in it is writable
                ro = DEC_NON_WRITABLE in d or len(member_nonwritable.get(tid, ())) == len(ops)
                kind = 'BufferSRV' if ro else 'BufferUAV'
        elif op == OP_TYPE_IMAGE:
            if ops[1] == DIM_BUFFER:
                raise SystemExit('ffx_sc_glsl: texel buffers are not supported')
            kind = 'TextureSRV' if ops[5] == 1 else 'TextureUAV'
        elif op == OP_TYPE_SAMPLER:
            kind = 'Sampler'
        elif op == OP_TYPE_ACCEL:
            kind = 'RTAccelerationStructure'
        else:
            raise SystemExit('ffx_sc_glsl: shader uses an unsupported resource type')
        found.append((d.get(DEC_SET, 0), d[DEC_BINDING], vid, kind, names.get(vid, ''), count))
    for s, b, vid, kind, name, count in sorted(found, key=lambda f: (f[0], f[1], f[2])):
        out[kind].append((name, b, count, s))
    return out


# --- permutations, as FidelityFX-SC enumerates them ---
def find_include(name, paths):
    for p in paths:
        f = pathlib.Path(p) / name
        if f.is_file():
            return f.resolve()
    return None


def scan_includes(shader, include_paths):
    """the shader and every file it includes (recursively), in visit order"""
    seen, todo, files = set(), [pathlib.Path(shader).resolve()], []
    while todo:
        f = todo.pop()
        if f in seen:
            continue
        seen.add(f)
        files.append(f)
        for line in f.read_text(errors='replace').splitlines():
            t = line.strip()
            if t.startswith('#include'):
                q = t[8:].strip()
                if q[:1] in '"<':
                    end = q.find('>' if q[0] == '<' else '"', 1)
                    if end > 0 and (inc := find_include(q[1:end], [f.parent] + list(include_paths))):
                        todo.append(inc)
    return files


def permutations(options):
    """[(key, defines, identical_to)] with the options in key order (FidelityFX-SC's recursion)"""
    result = []

    def rec(i, bit, key, defines, identical):
        if i == len(options):
            result.append((key, defines, identical))
            return
        name, values, found = options[i]
        nbits = max(1, (len(values) - 1).bit_length()) if len(values) > 1 else 0
        for j, v in enumerate(values):
            ident = identical
            if not found and ident is None and j != 0:
                ident = key  # the option is not in the source: the same SPIR-V as value 0
            rec(i + 1, bit + nbits, key | (j << bit), defines + [f'-D{name}={v}'], ident)

    rec(0, 0, 0, [], None)
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--glslang', required=True)
    ap.add_argument('--output', required=True)
    ap.add_argument('--name', action='append', required=True)
    ap.add_argument('--perm', action='append', default=[])
    ap.add_argument('--arg', action='append', default=[])
    ap.add_argument('-D', action='append', default=[], dest='defines')
    ap.add_argument('-I', action='append', default=[], dest='includes')
    ap.add_argument('shader')
    o = ap.parse_args()
    out_dir = pathlib.Path(o.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    files = scan_includes(o.shader, o.includes)
    text = ''.join(f.read_text(errors='replace') for f in files)
    options = []
    for p in o.perm:
        name, vals = p.split('=', 1)
        options.append((name, vals.split(','), name in text))
    options.sort(key=lambda opt: not opt[2])  # FidelityFX-SC: the options found in the source first (stable)
    perms = permutations(options)

    base = [o.glslang] + o.arg + [f'-D{d}' for d in o.defines] + [f'-I{i}' for i in o.includes]

    def compile_one(defines):
        with tempfile.TemporaryDirectory() as td:
            spv = pathlib.Path(td) / 'out.spv'
            r = subprocess.run(base + defines + ['-o', str(spv), o.shader], capture_output=True, text=True)
            if r.returncode != 0:
                sys.stderr.write(f'{o.shader} {" ".join(defines)}\n{r.stdout}{r.stderr}')
                return None
            return spv.read_bytes()

    unique_keys = [k for k, _, ident in perms if ident is None]
    by_key = {k: d for k, d, _ in perms}
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
        blobs = dict(zip(unique_keys, ex.map(lambda k: compile_one(by_key[k]), unique_keys)))
    if any(b is None for b in blobs.values()):
        return 1

    hash_index, uniques, key_index = {}, [], {}
    for k in unique_keys:
        h = hashlib.md5(blobs[k]).hexdigest()
        if h not in hash_index:
            hash_index[h] = len(uniques)
            uniques.append((h, blobs[k], reflect(blobs[k])))
        key_index[k] = hash_index[h]
    for k, _, ident in perms:
        if ident is not None:
            key_index[k] = key_index[ident]
    used_bits = sum(max(1, (len(v) - 1).bit_length()) if len(v) > 1 else 0 for _, v, _ in options)

    for shader_name in o.name:
        for h, blob, refl in uniques:
            pn = f'{shader_name}_{h}'
            lines = [f'// {pn}.h.', '// Auto generated by FidelityFX-SC (tools/ffx_sc_glsl.py).', '']
            for kind in KINDS:
                res = refl[kind]
                if not res:
                    continue
                lines.append(f'static const char* g_{pn}_{kind}ResourceNames[] = {{ ' +
                             ''.join(f' "{r[0]}",' for r in res) + ' };')
                for idx, field in ((1, 'Bindings'), (2, 'Counts'), (3, 'Sets')):
                    lines.append(f'static const uint32_t g_{pn}_{kind}Resource{field}[] = {{ ' +
                                 ''.join(f' {r[idx]},' for r in res) + ' };')
                lines.append('')
            lines.append(f'static const uint32_t g_{pn}_size = {len(blob)};\n')
            data = ',\n'.join(','.join(f'0x{b:02x}' for b in blob[i:i + 16]) for i in range(0, len(blob), 16))
            lines.append(f'static const unsigned char g_{pn}_data[] = {{\n{data}\n}};\n')
            (out_dir / f'{pn}.h').write_text('\n'.join(lines) + '\n')

        L = [f'#include "{shader_name}_{h}.h"' for h, _, _ in uniques] + ['']
        L.append(f'typedef union {shader_name}_PermutationKey {{')
        L.append('    struct {')
        for name, values, _ in options:
            nbits = max(1, (len(values) - 1).bit_length()) if len(values) > 1 else 0
            L.append(f'        uint32_t {name} : {nbits};')
        L += ['    };', '    uint32_t index;', f'}} {shader_name}_PermutationKey;', '']
        L += [f'typedef struct {shader_name}_PermutationInfo {{', '    const uint32_t       blobSize;',
              '    const unsigned char* blobData;', '']
        for member in ('constantBuffer', 'srvTexture', 'uavTexture', 'srvBuffer', 'uavBuffer', 'sampler',
                       'rtAccelerationStructure'):
            num = {'constantBuffer': 'ConstantBuffers', 'srvTexture': 'SRVTextures', 'uavTexture': 'UAVTextures',
                   'srvBuffer': 'SRVBuffers', 'uavBuffer': 'UAVBuffers', 'sampler': 'Samplers',
                   'rtAccelerationStructure': 'RTAccelerationStructures'}[member]
            L += [f'    const uint32_t  num{num};', f'    const char**    {member}Names;',
                  f'    const uint32_t* {member}Bindings;', f'    const uint32_t* {member}Counts;',
                  f'    const uint32_t* {member}Spaces;', '']
        L += [f'}} {shader_name}_PermutationInfo;', '']
        L.append(f'static const uint32_t g_{shader_name}_IndirectionTable[] = {{')
        L += [f'    {key_index.get(i, 0)},' for i in range(1 << used_bits)]
        L += ['};', '']
        L.append(f'static const {shader_name}_PermutationInfo g_{shader_name}_PermutationInfo[] = {{')
        for h, _, refl in uniques:
            pn = f'{shader_name}_{h}'
            row = f'    {{ g_{pn}_size, g_{pn}_data, '
            for kind in KINDS:
                n = len(refl[kind])
                row += (f'{n}, g_{pn}_{kind}ResourceNames, g_{pn}_{kind}ResourceBindings, '
                        f'g_{pn}_{kind}ResourceCounts, g_{pn}_{kind}ResourceSets, ') if n else '0, 0, 0, 0, 0, '
            L.append(row + '},')
        L += ['};', '']
        header = out_dir / f'{shader_name}_permutations.h'
        header.write_text('\n'.join(L) + '\n')
        deps = ' '.join(str(f).replace(' ', '\\ ') for f in files)
        pathlib.Path(f'{header}.d').write_text(f'{header.resolve()}: {deps}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
