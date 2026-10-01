#!/usr/bin/env python3
"""
ds2tex.py - prepara pacotes TexMod (.tpf/.zip) para o DS2TexInject.

Le todos os .tpf da pasta texmod/, decodifica (XOR 0x3FA4 + ZipCrypto do TexMod),
valida cada textura e copia para texmod/_cache/0xHASH.<ext>, que e o que a
DLL (plugins/DS2TexInject.asi) carrega dentro do jogo.

Prioridade: se dois pacotes trazem o mesmo hash, vence o que vem PRIMEIRO na
ordem (texmod/load_order.txt se existir, senao ordem alfabetica dos nomes).

Uso:  python3 ds2tex.py /caminho/do/Dead Space 2/texmod
      (sem argumento: usa ../texmod relativo a este script)
"""
import os, sys, struct, shutil, subprocess, tempfile, re

TEXMOD_PW = bytes([0x73,0x2A,0x63,0x7D,0x5F,0x0A,0xA6,0xBD,0x7D,0x65,0x7E,0x67,0x61,0x2A,
                   0x7F,0x7F,0x74,0x61,0x67,0x5B,0x60,0x70,0x45,0x74,0x5C,0x22,0x74,0x5D,
                   0x6E,0x6A,0x73,0x41,0x77,0x6E,0x46,0x47,0x77,0x49,0x0C,0x4B,0x46,0x6F])

D3D9_FOURCC = {b'DXT1', b'DXT2', b'DXT3', b'DXT4', b'DXT5', b'ATI1', b'ATI2'}
BLOCK_BYTES = {b'DXT1': 8, b'ATI1': 8, b'DXT2': 16, b'DXT3': 16, b'DXT4': 16, b'DXT5': 16, b'ATI2': 16}


def decode_tpf(path):
    """Retorna bytes de um zip normal a partir de um .tpf (ou .zip)."""
    data = open(path, 'rb').read()
    if data[:4] == b'PK\x03\x04':
        return data
    # TPF = zip com cada par de bytes XOR 0xA4 0x3F (feito com inteiros grandes: rapido, sem numpy)
    n = len(data)
    key = (b'\xA4\x3F' * (n // 2 + 1))[:n]
    out = (int.from_bytes(data, 'little') ^ int.from_bytes(key, 'little')).to_bytes(n, 'little')
    if out[:4] != b'PK\x03\x04':
        raise ValueError('nao parece um TPF valido (cabecalho errado)')
    return out


def check_dds(data):
    """Retorna (descricao, problema_ou_None)."""
    if len(data) < 128 or data[:4] != b'DDS ':
        return 'dds?', 'cabecalho DDS invalido'
    h = struct.unpack('<31I', data[4:128])
    hgt, wid, mips = h[2], h[3], h[6]
    pf_flags, fourcc, bits = h[19], struct.pack('<I', h[20]), h[21]
    caps2 = h[27]
    if pf_flags & 4:
        fmt = fourcc.decode('latin1')
        if fourcc == b'DX10':
            return fmt, 'DDS formato DX10 (BC7/etc) - D3D9 nao suporta'
        if fourcc not in D3D9_FOURCC and h[20] not in (36, 113, 114, 115, 116, 111, 112):
            return fmt, 'FourCC desconhecido para D3D9'
    else:
        fmt = 'RGB%d%s' % (bits, 'A' if pf_flags & 1 else '')
    desc = '%s %dx%d mips=%d%s' % (fmt, wid, hgt, mips, ' CUBE' if caps2 & 0x200 else '')
    if wid == 0 or hgt == 0:
        return desc, 'dimensoes zero'
    if fourcc in BLOCK_BYTES and pf_flags & 4 and not caps2 & 0x200:
        need, w, hh = 0, wid, hgt
        for _ in range(max(mips, 1)):
            need += max(1, (w + 3) // 4) * max(1, (hh + 3) // 4) * BLOCK_BYTES[fourcc]
            w, hh = max(1, w // 2), max(1, hh // 2)
        if len(data) - 128 < need:
            return desc, 'arquivo truncado (%d < %d bytes)' % (len(data) - 128, need)
    return desc, None


def check_other(ext, data):
    if ext == 'png' and data[:8] != b'\x89PNG\r\n\x1a\n':
        return 'png', 'PNG invalido'
    if ext == 'bmp' and data[:2] != b'BM':
        return 'bmp', 'BMP invalido'
    if ext in ('tga', 'jpg', 'jpeg'):
        return ext, None
    return ext, None


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    texdir = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(here, '..', 'texmod')
    texdir = os.path.normpath(texdir)
    cache = os.path.join(texdir, '_cache')
    if not shutil.which('unzip'):
        sys.exit('precisa do comando "unzip" (sudo pacman -S unzip)')

    packs = sorted(f for f in os.listdir(texdir) if f.lower().endswith(('.tpf', '.zip')))
    order_file = os.path.join(texdir, 'load_order.txt')
    if os.path.exists(order_file):
        wanted = [l.strip() for l in open(order_file, encoding='utf-8', errors='ignore')
                  if l.strip() and not l.startswith('#')]
        packs = [p for p in wanted if p in packs] + [p for p in packs if p not in wanted]
    if not packs:
        sys.exit('nenhum .tpf encontrado em ' + texdir)

    if os.path.isdir(cache):
        shutil.rmtree(cache)
    os.makedirs(cache)

    index, problems, overridden = {}, [], 0
    for pack in packs:
        print('>> %s' % pack, flush=True)
        with tempfile.TemporaryDirectory(dir=texdir, prefix='.tmp_') as tmp:
            try:
                zbytes = decode_tpf(os.path.join(texdir, pack))
            except Exception as e:
                problems.append('%s: %s' % (pack, e)); continue
            zpath = os.path.join(tmp, 'p.zip')
            open(zpath, 'wb').write(zbytes)
            del zbytes
            out = os.path.join(tmp, 'x')
            r = subprocess.run([b'unzip', b'-o', b'-qq', b'-P', TEXMOD_PW, zpath.encode(), b'-d', out.encode()],
                               capture_output=True)
            if r.returncode not in (0, 1):
                # senha diferente? tenta sem senha
                r = subprocess.run(['unzip', '-o', '-qq', zpath, '-d', out], capture_output=True)
            if r.returncode not in (0, 1):
                problems.append('%s: unzip falhou: %s' % (pack, r.stderr.decode(errors='ignore').strip()[:300]))
                continue
            deff = None
            for root, _, files in os.walk(out):
                for f in files:
                    if f.lower() == 'texmod.def':
                        deff = os.path.join(root, f)
            if not deff:
                problems.append('%s: sem texmod.def' % pack); continue
            n_ok = 0
            for line in open(deff, 'rb').read().replace(b'\x00', b'').decode('latin1').splitlines():
                line = line.strip()
                if not line or '|' not in line:
                    continue
                hs, fn = line.split('|', 1)
                fn = fn.strip().replace('\\', '/')
                try:
                    h = int(hs.strip(), 16) & 0xFFFFFFFF
                except ValueError:
                    problems.append('%s: linha invalida no def: %r' % (pack, line)); continue
                if h in index:
                    overridden += index[h][0] != pack
                    continue
                src = os.path.join(out, fn)
                if not os.path.isfile(src):
                    problems.append('%s: %s listado mas ausente' % (pack, fn)); continue
                data = open(src, 'rb').read()
                ext = fn.rsplit('.', 1)[-1].lower()
                desc, err = check_dds(data) if ext == 'dds' else check_other(ext, data)
                if err:
                    problems.append('%s: %s (0x%08X): %s [%s]' % (pack, fn, h, err, desc)); continue
                dst = os.path.join(cache, '0x%08X.%s' % (h, ext))
                shutil.move(src, dst) if not os.path.exists(dst) else None
                index[h] = (pack, fn, desc, len(data))
                n_ok += 1
            print('   %d texturas' % n_ok, flush=True)

    with open(os.path.join(cache, 'index.txt'), 'w') as f:
        f.write('# hash | pacote | arquivo original | formato | bytes\n')
        for h in sorted(index):
            p, fn, d, sz = index[h]
            f.write('0x%08X|%s|%s|%s|%d\n' % (h, p, fn, d, sz))

    total = sum(v[3] for v in index.values())
    print('\n%d texturas unicas (%.0f MB) em %s' % (len(index), total / 1048576, cache))
    print('%d duplicadas ignoradas (pacote anterior tem prioridade)' % overridden)
    if problems:
        print('\nPROBLEMAS (%d):' % len(problems))
        for p in problems:
            print('  - ' + p)
    else:
        print('Nenhuma textura quebrada encontrada.')


if __name__ == '__main__':
    main()
