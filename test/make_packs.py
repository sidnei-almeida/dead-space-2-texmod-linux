#!/usr/bin/env python3
"""Gera os pacotes de teste do test.sh numa pasta texmod/:

  a_sintetico.tpf  zip com ZipCrypto (senha do TexMod) + XOR 0xA4/0x3F, como um .tpf de verdade
  b_sintetico.zip  zip comum, como o pacote do remaster

Juntos cobrem PNG, BMP 32-bit com alfa, JPG, TGA, DDS DXT1/DXT5/A8R8G8B8 sem mipmaps, DDS ja com
mipmaps, caminhos com barra invertida e CRLF no texmod.def, um hash repetido nos dois pacotes (o
primeiro em ordem alfabetica tem que vencer) e um DDS truncado (tem que ser recusado).
Precisa do Pillow. Uso: python3 make_packs.py <pasta texmod>
"""
import io, os, struct, sys, zlib

from PIL import Image

TEXMOD_PW = bytes([0x73, 0x2A, 0x63, 0x7D, 0x5F, 0x0A, 0xA6, 0xBD, 0x7D, 0x65, 0x7E, 0x67, 0x61, 0x2A,
                   0x7F, 0x7F, 0x74, 0x61, 0x67, 0x5B, 0x60, 0x70, 0x45, 0x74, 0x5C, 0x22, 0x74, 0x5D,
                   0x6E, 0x6A, 0x73, 0x41, 0x77, 0x6E, 0x46, 0x47, 0x77, 0x49, 0x0C, 0x4B, 0x46, 0x6F])

CRC_TAB = []
for i in range(256):
    c = i
    for _ in range(8):
        c = (c >> 1) ^ (0xEDB88320 if c & 1 else 0)
    CRC_TAB.append(c)


class ZipCrypto:
    def __init__(self, pw):
        self.k = [0x12345678, 0x23456789, 0x34567890]
        for b in pw:
            self.update(b)

    def update(self, b):
        k = self.k
        k[0] = CRC_TAB[(k[0] ^ b) & 0xFF] ^ (k[0] >> 8)
        k[1] = ((k[1] + (k[0] & 0xFF)) * 134775813 + 1) & 0xFFFFFFFF
        k[2] = CRC_TAB[(k[2] ^ (k[1] >> 24)) & 0xFF] ^ (k[2] >> 8)

    def encrypt(self, data):
        out = bytearray()
        for b in data:
            t = (self.k[2] | 2) & 0xFFFF
            out.append(b ^ (((t * (t ^ 1)) >> 8) & 0xFF))
            self.update(b)
        return bytes(out)


def make_zip(entries, password=None):
    """entries: [(nome, bytes)] -> zip (deflate), com ZipCrypto se houver senha."""
    out, central = io.BytesIO(), io.BytesIO()
    for i, (name, data) in enumerate(entries):
        nb = name.encode('latin1')
        crc = zlib.crc32(data) & 0xFFFFFFFF
        co = zlib.compressobj(9, zlib.DEFLATED, -15)
        comp = co.compress(data) + co.flush()
        flags = 0
        if password is not None:
            flags = 1
            zc = ZipCrypto(password)
            head = bytes((i * 37 + j * 11) & 0xFF for j in range(11)) + bytes([crc >> 24])
            comp = zc.encrypt(head) + zc.encrypt(comp)
        off = out.tell()
        out.write(struct.pack('<IHHHHHIIIHH', 0x04034B50, 20, flags, 8, 0, 0x21, crc, len(comp), len(data), len(nb), 0))
        out.write(nb)
        out.write(comp)
        central.write(struct.pack('<IHHHHHHIIIHHHHHII', 0x02014B50, 20, 20, flags, 8, 0, 0x21, crc, len(comp), len(data),
                                  len(nb), 0, 0, 0, 0, 0, off))
        central.write(nb)
    cd = central.getvalue()
    cd_off = out.tell()
    out.write(cd)
    out.write(struct.pack('<IHHHHIIH', 0x06054B50, 0, 0, len(entries), len(entries), len(cd), cd_off, 0))
    return out.getvalue()


def tpf_xor(data):
    key = (b'\xA4\x3F' * (len(data) // 2 + 1))[:len(data)]
    return bytes(a ^ b for a, b in zip(data, key))


def dds(w, h, fourcc=None, mips=1, data=b''):
    flags = 0x1007 | (0x20000 if mips > 1 else 0) | (0x80000 if fourcc else 0x8)
    if fourcc:
        pf = struct.pack('<II4sIIIII', 32, 0x4, fourcc, 0, 0, 0, 0, 0)
        linear = ((w + 3) // 4) * ((h + 3) // 4) * (8 if fourcc == b'DXT1' else 16)
    else:
        pf = struct.pack('<II4sIIIII', 32, 0x41, b'\0\0\0\0', 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
        linear = w * 4
    caps = 0x1000 | (0x400008 if mips > 1 else 0)
    hdr = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, h, w, linear, 0, mips) + b'\0' * 44 + pf
    hdr += struct.pack('<IIIII', caps, 0, 0, 0, 0)
    return hdr + data


def dxt_blocks(w, h, block, seed):
    n = ((w + 3) // 4) * ((h + 3) // 4)
    out = bytearray()
    for i in range(n):
        c0 = (seed * 2654435761 + i * 40503) & 0xFFFF
        c1 = (c0 * 31 + 7) & 0xFFFF
        if block == 16:  # DXT5: bloco de alfa antes da cor
            out += bytes([255 - (i & 0x7F), i & 0x7F]) + struct.pack('<IH', (i * 2246822519) & 0xFFFFFFFF, i & 0xFFFF)
        out += struct.pack('<HHI', max(c0, c1), min(c0, c1), (i * 2654435761) & 0xFFFFFFFF)
    return bytes(out)


def mip_chain_dxt1(w, h, seed):
    data, lw, lh, n = b'', w, h, 0
    while True:
        data += dxt_blocks(lw, lh, 8, seed + n)
        n += 1
        if lw == 1 and lh == 1:
            return data, n
        lw, lh = max(1, lw // 2), max(1, lh // 2)


def img_bytes(img, fmt):
    b = io.BytesIO()
    img.save(b, fmt)
    return b.getvalue()


def gradient(w, h, alpha):
    img = Image.new('RGBA' if alpha else 'RGB', (w, h))
    img.putdata([((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), (x * y) & 0xFF, (x + y * 3) & 0xFF)[:4 if alpha else 3]
                 for y in range(h) for x in range(w)])
    return img


def packs():
    """Conteudo dos dois pacotes: ([(nome, bytes)] do .tpf, [(nome, bytes)] do .zip)."""
    full, nm = mip_chain_dxt1(16, 16, 9)
    argb = bytes((i * 13) & 0xFF for i in range(16 * 16 * 4))
    tpf_files = [
        ('tex/png_rgba.png', img_bytes(gradient(64, 64, True), 'PNG')),
        ('tex/bmp32.bmp', img_bytes(gradient(32, 32, True), 'BMP')),
        ('tex/foto.jpg', img_bytes(gradient(64, 32, False), 'JPEG')),
        ('tex/alvo.tga', img_bytes(gradient(32, 32, True), 'TGA')),
        ('tex/dxt1_sem_mips.dds', dds(64, 64, b'DXT1', 1, dxt_blocks(64, 64, 8, 1))),
        ('tex/repetida.dds', dds(16, 16, b'DXT1', nm, full)),  # vence: pacote "a_" vem antes
    ]
    tpf_def = ('0x0000A001|tex\\png_rgba.png\r\n0x0000A002|tex\\bmp32.bmp\r\n0x0000A003|tex\\foto.jpg\r\n'
               '0x0000A004|tex\\alvo.tga\r\n0x0000A005|tex\\dxt1_sem_mips.dds\r\n0x0000C001|tex\\repetida.dds\r\n')
    tpf_files.append(('texmod.def', tpf_def.encode('latin1')))
    zip_files = [
        ('dds/dxt5_sem_mips.dds', dds(32, 32, b'DXT5', 1, dxt_blocks(32, 32, 16, 2))),
        ('dds/argb_sem_mips.dds', dds(16, 16, None, 1, argb)),
        ('dds/dxt1_pronta.dds', dds(16, 16, b'DXT1', nm, full)),
        ('dds/repetida.dds', dds(8, 8, b'DXT1', 1, dxt_blocks(8, 8, 8, 5))),     # perde para o .tpf
        ('dds/truncada.dds', dds(64, 64, b'DXT1', 1, dxt_blocks(64, 64, 8, 3))[:200]),  # tem que ser recusada
        ('texmod.def', b'0x0000B001|dds/dxt5_sem_mips.dds\n0x0000B002|dds/argb_sem_mips.dds\n'
                       b'0x0000B003|dds/dxt1_pronta.dds\n0x0000C001|dds/repetida.dds\n0x0000B004|dds/truncada.dds\n'),
    ]
    return tpf_files, zip_files


def expected():
    """O que cada textura do _cache tem que conter, por hash:
    ('imagem', Image RGBA, tolerancia) para PNG/BMP/TGA/JPG (vira DDS sem compressao) ou
    ('dds', bytes do DDS de origem) para DDS (o nivel 0 e copiado; mipmaps que ja existiam tambem)."""
    tpf_files, zip_files = packs()
    f = dict(tpf_files + zip_files)
    jpg = Image.open(io.BytesIO(f['tex/foto.jpg'])).convert('RGBA')
    return {
        0x0000A001: ('imagem', gradient(64, 64, True), 0),
        0x0000A002: ('imagem', gradient(32, 32, True), 0),  # BMP 32-bit: o alfa tem que ser mantido
        0x0000A003: ('imagem', jpg, 3),  # decodificadores de JPG diferentes arredondam diferente
        0x0000A004: ('imagem', gradient(32, 32, True), 0),
        0x0000A005: ('dds', f['tex/dxt1_sem_mips.dds']),
        0x0000B001: ('dds', f['dds/dxt5_sem_mips.dds']),
        0x0000B002: ('dds', f['dds/argb_sem_mips.dds']),
        0x0000B003: ('dds', f['dds/dxt1_pronta.dds']),
        0x0000C001: ('dds', f['tex/repetida.dds']),
    }


def main():
    texdir = sys.argv[1]
    os.makedirs(texdir, exist_ok=True)
    tpf_files, zip_files = packs()
    open(os.path.join(texdir, 'a_sintetico.tpf'), 'wb').write(tpf_xor(make_zip(tpf_files, TEXMOD_PW)))
    open(os.path.join(texdir, 'b_sintetico.zip'), 'wb').write(make_zip(zip_files))


if __name__ == '__main__':
    main()
