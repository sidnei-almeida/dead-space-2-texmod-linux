#!/usr/bin/env python3
"""Confere o texmod/_cache gerado no Windows (DS2TexInject-Setup.exe) e o do Linux (ds2tex.py).

Sempre: os dois tem os mesmos arquivos e o mesmo index.txt; todo DDS tem todos os niveis completos
(nada truncado) e a cadeia inteira de mipmaps; os cabecalhos sao iguais; e o nivel 0, a imagem em
resolucao cheia, e igual nos dois (com tolerancia de 3 tons so para o que veio de JPG, porque
decodificadores diferentes arredondam diferente). Os mipmaps menores podem diferir (Pillow e stb
reduzem com filtros diferentes).
Com --sintetico: confere tambem cada textura contra a origem em test/make_packs.py, para pegar um
erro que os dois instaladores tivessem igual (o Setup.exe e uma versao em C++ do ds2tex.py).
Uso: compare_caches.py <cache windows> <cache linux> [--sintetico]
"""
import importlib.util, os, struct, sys

BLOCK = {b'DXT1': 8, b'ATI1': 8, b'DXT2': 16, b'DXT3': 16, b'DXT4': 16, b'DXT5': 16, b'ATI2': 16}
fail = False


def ok(msg):
    print('  ok     ' + msg)


def bad(msg):
    global fail
    fail = True
    print('  FALHOU ' + msg)


def parse(data):
    """DDS -> dict com os niveis; 'truncado' se faltar byte em algum nivel."""
    if data[:4] != b'DDS ' or len(data) < 128:
        return None
    h = struct.unpack_from('<31I', data, 4)
    H, W, M = h[2], h[3], h[6] or 1
    fcc = data[84:88] if h[19] & 4 else None
    bpp = h[21]
    if (fcc and fcc not in BLOCK) or (not fcc and bpp not in (8, 16, 24, 32)):
        return None
    off, levels, truncated = 128, [], False
    for lv in range(M):
        w, hh = max(1, W >> lv), max(1, H >> lv)
        n = ((w + 3) // 4) * ((hh + 3) // 4) * BLOCK[fcc] if fcc else w * hh * bpp // 8
        lvl = data[off:off + n]
        truncated |= len(lvl) != n
        levels.append(lvl)
        off += n
    full, s = 1, max(W, H)
    while s > 1:
        s >>= 1
        full += 1
    return {'w': W, 'h': H, 'mips': M, 'full': full, 'fmt': fcc or ('%dbpp' % bpp).encode(), 'size': len(data),
            'levels': levels, 'truncado': truncated}


def read_index(d):
    p = os.path.join(d, 'index.txt')
    if not os.path.isfile(p):
        return None
    lines = open(p, encoding='utf-8', errors='replace').read().replace('\r', '').splitlines()
    return {l.split('|', 1)[0]: l for l in lines if l and not l.startswith('#')}


def maxdiff(a, b):
    return max((abs(x - y) for x, y in zip(a, b)), default=0) if len(a) == len(b) else None


def main():
    wdir, ldir = sys.argv[1], sys.argv[2]
    synthetic = '--sintetico' in sys.argv[3:]
    names = lambda d: sorted(f for f in os.listdir(d) if f.startswith('0x')) if os.path.isdir(d) else []
    wn, ln = names(wdir), names(ldir)
    if wn == ln and wn:
        ok('mesmas %d texturas nos dois _cache' % len(wn))
    else:
        bad('arquivos diferentes: so no Windows %s, so no Linux %s' % (sorted(set(wn) - set(ln)), sorted(set(ln) - set(wn))))

    wi, li = read_index(wdir), read_index(ldir)
    if wi is None or li is None:
        bad('index.txt ausente (Windows %s, Linux %s)' % (wi is not None, li is not None))
        wi, li = wi or {}, li or {}
    elif wi == li:
        ok('index.txt igual nos dois (mesmo pacote, arquivo de origem e formato para cada hash)')
    else:
        diff = sorted(set(wi.items()) ^ set(li.items()))[:4]
        bad('index.txt diferente: %s' % diff)

    common = sorted(set(wn) & set(ln))
    problems_before = fail
    for f in common:
        a = parse(open(os.path.join(wdir, f), 'rb').read())
        b = parse(open(os.path.join(ldir, f), 'rb').read())
        if not a or not b:
            bad('%s: nao e um DDS valido (Windows %s, Linux %s)' % (f, bool(a), bool(b)))
            continue
        for side, x in (('Windows', a), ('Linux', b)):
            if x['truncado']:
                bad('%s (%s): algum nivel esta incompleto' % (f, side))
            if x['mips'] != x['full']:
                bad('%s (%s): so %d de %d mipmaps' % (f, side, x['mips'], x['full']))
        ha = {k: a[k] for k in ('w', 'h', 'mips', 'fmt', 'size')}
        hb = {k: b[k] for k in ('w', 'h', 'mips', 'fmt', 'size')}
        if ha != hb:
            bad('%s: cabecalho diferente: Windows %s, Linux %s' % (f, ha, hb))
            continue
        src = (wi.get(f.split('.')[0]) or '').split('|')
        from_jpg = len(src) > 2 and src[2].lower().endswith(('.jpg', '.jpeg'))
        d = maxdiff(a['levels'][0], b['levels'][0])
        if d is None or d > (3 if from_jpg and not a['fmt'] in BLOCK else 0):
            bad('%s: imagem em resolucao cheia diferente entre Windows e Linux (maior diferenca %s)' % (f, d))
    if common and fail == problems_before:
        ok('niveis completos, mipmaps inteiros, mesmos cabecalhos e a mesma imagem em resolucao cheia')

    if synthetic:
        check_synthetic(wdir, ldir, wn, ln)
    sys.exit(1 if fail else 0)


def check_synthetic(wdir, ldir, wn, ln):
    spec = importlib.util.spec_from_file_location('make_packs', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_packs.py'))
    mp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mp)
    exp = mp.expected()
    want = sorted('0x%08X.dds' % h for h in exp)
    for side, d, got in (('Windows', wdir, wn), ('Linux', ldir, ln)):
        if got != want:
            bad('%s: esperado %s, veio %s (a truncada tem que ficar de fora)' % (side, want, got))
            continue
        before = fail
        for h, e in sorted(exp.items()):
            f = '0x%08X.dds' % h
            x = parse(open(os.path.join(d, f), 'rb').read())
            if not x:
                bad('%s %s: DDS invalido' % (side, f))
                continue
            if e[0] == 'imagem':
                img, tol = e[1], e[2]
                want0 = img.tobytes('raw', 'BGRA')
                if (x['w'], x['h'], x['fmt']) != (img.width, img.height, b'32bpp'):
                    bad('%s %s: esperado %dx%d sem compressao, veio %dx%d %s' % (side, f, img.width, img.height, x['w'], x['h'], x['fmt']))
                    continue
                md = maxdiff(x['levels'][0], want0)
                if md is None or md > tol:
                    bad('%s %s: imagem diferente da origem (maior diferenca %s, tolerancia %d)' % (side, f, md, tol))
            else:
                s = parse(e[1])
                if (x['w'], x['h'], x['fmt']) != (s['w'], s['h'], s['fmt']):
                    bad('%s %s: tamanho/formato diferente da origem' % (side, f))
                    continue
                keep = s['mips']  # niveis que ja existiam na origem tem que vir iguais
                if x['levels'][:keep] != s['levels'][:keep]:
                    bad('%s %s: %s diferente da origem' % (side, f, 'nivel 0' if keep == 1 else 'mipmaps que ja vinham prontos'))
        if fail == before:
            ok('%s: as 9 texturas batem com a origem (a truncada ficou de fora; no hash repetido venceu o .tpf)' % side)


if __name__ == '__main__':
    main()
