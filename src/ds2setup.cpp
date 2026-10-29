// DS2TexInject-Setup - instalador do DS2TexInject para Windows.
//
// Faz no Windows o que install.sh + ds2tex.py fazem no Linux, sem precisar de Python:
//   1. acha a pasta do Dead Space 2 (Steam, EA app/Origin, registro) ou pede para o usuario apontar
//      o deadspace2.exe;
//   2. oferece baixar o MarkerPatch (ASI loader) se nao houver dinput8.dll;
//   3. instala plugins\DS2TexInject.asi e DS2TexInject.ini (embutidos neste exe);
//   4. le todos os .tpf/.zip de texmod\ e monta texmod\_cache\0xHASH.dds, com mipmaps e PNG/BMP/TGA/JPG
//      convertidos para DDS - a mesma regra do ds2tex.py (prioridade, load_order.txt, index.txt).
//
// Uso: DS2TexInject-Setup.exe ["C:\caminho\Dead Space 2"] [--yes] [--rebuild] [--no-pause]

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <urlmon.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// O llvm-mingw usa _WIN32_WINNT=0x601 por padrao e so define estas flags a partir de 0xA00
#ifndef RRF_SUBKEY_WOW6464KEY
#define RRF_SUBKEY_WOW6464KEY 0x00010000
#endif
#ifndef RRF_SUBKEY_WOW6432KEY
#define RRF_SUBKEY_WOW6432KEY 0x00020000
#endif

#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_PSD
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#define STB_DXT_IMPLEMENTATION
#include "third_party/stb_dxt.h"

#define DS2SETUP_VERSION "1.4.1"
#define MARKERPATCH_URL L"https://github.com/Wemino/MarkerPatch/releases/latest/download/MarkerPatch.zip"
#define RES_ASI 101
#define RES_INI 102

typedef std::vector<uint8_t> Bytes;

// ---------------------------------------------------------------- saida / idioma

static bool g_pt, g_yes;
#define T(en, pt) (g_pt ? L"" pt : L"" en)

static void Out(const wchar_t *fmt, ...)
{
    wchar_t buf[4096];
    va_list ap; va_start(ap, fmt); vswprintf(buf, 4096, fmt, ap); va_end(ap);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n, mode;
    if (GetConsoleMode(h, &mode)) { WriteConsoleW(h, buf, (DWORD)wcslen(buf), &n, nullptr); return; }
    char u8[16384];
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, u8, sizeof u8, nullptr, nullptr);
    if (len > 1) WriteFile(h, u8, len - 1, &n, nullptr);
}

static bool Ask(const wchar_t *msg)
{
    if (g_yes) return true;
    return MessageBoxW(GetConsoleWindow(), msg, L"DS2TexInject", MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) == IDYES;
}

static std::wstring Widen(const std::string &s, UINT cp = CP_UTF8)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0 && cp == CP_UTF8) return Widen(s, 28591); // nao e UTF-8 valido: le como latin1
    std::wstring w(n, 0);
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string Narrow(const std::wstring &w)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring Fmt(const wchar_t *fmt, ...)
{
    wchar_t buf[4096];
    va_list ap; va_start(ap, fmt); vswprintf(buf, 4096, fmt, ap); va_end(ap);
    return buf;
}

// ---------------------------------------------------------------- arquivos

static bool IsFile(const std::wstring &p)
{
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool IsDir(const std::wstring &p)
{
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ReadAll(const std::wstring &path, Bytes &out, size_t limit = (size_t)-1)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(f, &sz) && (uint64_t)sz.QuadPart < 0x7FFFFFFF;
    if (ok) {
        out.resize(std::min((size_t)sz.QuadPart, limit));
        DWORD rd = 0;
        ok = out.empty() || (ReadFile(f, out.data(), (DWORD)out.size(), &rd, nullptr) && rd == out.size());
    }
    CloseHandle(f);
    return ok;
}

static bool WriteAll(const std::wstring &path, const void *data, size_t n)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    const uint8_t *p = (const uint8_t *)data;
    bool ok = true;
    while (ok && n) {
        DWORD chunk = (DWORD)std::min(n, (size_t)64 << 20), wr = 0;
        ok = WriteFile(f, p, chunk, &wr, nullptr) && wr == chunk;
        p += chunk; n -= chunk;
    }
    ok = CloseHandle(f) && ok;
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

// grava em .tmp e troca, para nunca deixar um arquivo pela metade
static bool WriteReplace(const std::wstring &path, const Bytes &data)
{
    std::wstring tmp = path + L".tmp";
    return WriteAll(tmp, data.data(), data.size()) && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Apaga a pasta inteira. Juncoes/links sao removidos sem entrar neles.
static void RemoveTree(const std::wstring &dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            std::wstring p = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW(p.c_str());
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) RemoveDirectoryW(p.c_str());
            else RemoveTree(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

static std::wstring ParentDir(std::wstring p)
{
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"" : p.substr(0, s);
}

static std::wstring FullPath(const std::wstring &p)
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 2, buf, nullptr);
    std::wstring r = (n && n < MAX_PATH * 2) ? buf : p;
    while (r.size() > 3 && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    return r;
}

static uint64_t FreeSpace(const std::wstring &dir)
{
    ULARGE_INTEGER avail;
    return GetDiskFreeSpaceExW(dir.c_str(), &avail, nullptr, nullptr) ? avail.QuadPart : ~0ull;
}

// ---------------------------------------------------------------- CRC32 (zip) e ZipCrypto

static uint32_t g_crc[256];
static void CrcInit()
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
        g_crc[i] = c;
    }
}
static uint32_t Crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = (c >> 8) ^ g_crc[(c ^ *p++) & 0xFF];
    return ~c;
}

static const uint8_t TEXMOD_PW[] = {0x73, 0x2A, 0x63, 0x7D, 0x5F, 0x0A, 0xA6, 0xBD, 0x7D, 0x65, 0x7E, 0x67, 0x61, 0x2A,
                                    0x7F, 0x7F, 0x74, 0x61, 0x67, 0x5B, 0x60, 0x70, 0x45, 0x74, 0x5C, 0x22, 0x74, 0x5D,
                                    0x6E, 0x6A, 0x73, 0x41, 0x77, 0x6E, 0x46, 0x47, 0x77, 0x49, 0x0C, 0x4B, 0x46, 0x6F};

struct ZipCrypto {
    uint32_t k0 = 0x12345678, k1 = 0x23456789, k2 = 0x34567890;
    ZipCrypto(const uint8_t *pw, size_t n) { while (n--) Update(*pw++); }
    void Update(uint8_t c)
    {
        k0 = (k0 >> 8) ^ g_crc[(k0 ^ c) & 0xFF];
        k1 = (k1 + (k0 & 0xFF)) * 134775813u + 1;
        k2 = (k2 >> 8) ^ g_crc[(k2 ^ (k1 >> 24)) & 0xFF];
    }
    void Decrypt(uint8_t *p, size_t n)
    {
        while (n--) {
            uint16_t t = (uint16_t)(k2 | 2);
            *p ^= (uint8_t)((t * (t ^ 1)) >> 8);
            Update(*p++);
        }
    }
};

// ---------------------------------------------------------------- leitor de zip (TPF = zip com XOR 0xA4 0x3F)

struct ZipEntry {
    std::string name;
    uint16_t flags, method;
    uint32_t crc;
    uint64_t csize, usize, off;
};

struct Archive {
    HANDLE f = INVALID_HANDLE_VALUE;
    bool xored = false; // .tpf: cada byte par XOR 0xA4, impar XOR 0x3F (decodificado na leitura)
    uint64_t size = 0;
    std::vector<ZipEntry> entries;

    ~Archive() { if (f != INVALID_HANDLE_VALUE) CloseHandle(f); }

    bool Read(uint64_t off, void *dst, size_t n)
    {
        if (off > size || n > size - off) return false;
        uint8_t *p = (uint8_t *)dst;
        for (size_t done = 0; done < n;) {
            OVERLAPPED ov = {};
            uint64_t o = off + done;
            ov.Offset = (DWORD)o; ov.OffsetHigh = (DWORD)(o >> 32);
            DWORD chunk = (DWORD)std::min(n - done, (size_t)64 << 20), rd = 0;
            if (!ReadFile(f, p + done, chunk, &rd, &ov) || rd != chunk) return false;
            done += chunk;
        }
        if (xored)
            for (size_t i = 0; i < n; i++) p[i] ^= ((off + i) & 1) ? 0x3F : 0xA4;
        return true;
    }

    bool Open(const std::wstring &path, std::wstring &err)
    {
        f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
        LARGE_INTEGER sz;
        if (f == INVALID_HANDLE_VALUE || !GetFileSizeEx(f, &sz)) { err = T("could not open the file", "nao deu para abrir o arquivo"); return false; }
        size = sz.QuadPart;
        uint8_t sig[4];
        if (!Read(0, sig, 4)) { err = T("file too small", "arquivo pequeno demais"); return false; }
        if (memcmp(sig, "PK\x03\x04", 4)) {
            xored = true;
            if (!Read(0, sig, 4) || memcmp(sig, "PK\x03\x04", 4)) { err = T("not a valid TPF (bad header)", "nao parece um TPF valido (cabecalho errado)"); return false; }
        }
        return ReadCentral(err);
    }

    bool ReadCentral(std::wstring &err)
    {
        err = T("damaged zip (central directory not found)", "zip danificado (diretorio central nao encontrado)");
        size_t tail = (size_t)std::min<uint64_t>(size, 65557);
        Bytes t(tail);
        if (!Read(size - tail, t.data(), tail)) return false;
        size_t e = std::string::npos;
        for (size_t i = tail - 22 + 1; i-- > 0;)
            if (!memcmp(&t[i], "PK\x05\x06", 4)) { e = i; break; }
        if (e == std::string::npos) return false;
        uint64_t count = *(uint16_t *)&t[e + 10], cdSize = *(uint32_t *)&t[e + 12], cdOff = *(uint32_t *)&t[e + 16];
        uint64_t eocdPos = size - tail + e;
        if (count == 0xFFFF || cdSize == 0xFFFFFFFF || cdOff == 0xFFFFFFFF) {
            uint8_t loc[20], z64[56];
            if (eocdPos < 20 || !Read(eocdPos - 20, loc, 20) || memcmp(loc, "PK\x06\x07", 4)) return false;
            if (!Read(*(uint64_t *)&loc[8], z64, 56) || memcmp(z64, "PK\x06\x06", 4)) return false;
            count = *(uint64_t *)&z64[32]; cdSize = *(uint64_t *)&z64[40]; cdOff = *(uint64_t *)&z64[48];
        }
        if (cdSize > (256u << 20)) return false;
        Bytes cd((size_t)cdSize);
        if (!Read(cdOff, cd.data(), cd.size())) return false;
        for (size_t p = 0; entries.size() < count;) {
            if (p + 46 > cd.size() || memcmp(&cd[p], "PK\x01\x02", 4)) return false;
            ZipEntry z;
            z.flags = *(uint16_t *)&cd[p + 8];
            z.method = *(uint16_t *)&cd[p + 10];
            z.crc = *(uint32_t *)&cd[p + 16];
            z.csize = *(uint32_t *)&cd[p + 20];
            z.usize = *(uint32_t *)&cd[p + 24];
            size_t nl = *(uint16_t *)&cd[p + 28], xl = *(uint16_t *)&cd[p + 30], cl = *(uint16_t *)&cd[p + 32];
            z.off = *(uint32_t *)&cd[p + 42];
            if (p + 46 + nl + xl + cl > cd.size()) return false;
            z.name.assign((char *)&cd[p + 46], nl);
            for (size_t x = p + 46 + nl, xe = x + xl; x + 4 <= xe;) {
                uint16_t id = *(uint16_t *)&cd[x], len = *(uint16_t *)&cd[x + 2];
                if (id == 1) { // zip64: so os campos que estavam em 0xFFFFFFFF, nesta ordem
                    size_t q = x + 4, qe = std::min(x + 4 + len, xe);
                    if (z.usize == 0xFFFFFFFF && q + 8 <= qe) { z.usize = *(uint64_t *)&cd[q]; q += 8; }
                    if (z.csize == 0xFFFFFFFF && q + 8 <= qe) { z.csize = *(uint64_t *)&cd[q]; q += 8; }
                    if (z.off == 0xFFFFFFFF && q + 8 <= qe) { z.off = *(uint64_t *)&cd[q]; q += 8; }
                }
                x += 4 + len;
            }
            entries.push_back(z);
            p += 46 + nl + xl + cl;
        }
        err.clear();
        return true;
    }

    bool Extract(const ZipEntry &z, Bytes &out, std::wstring &err)
    {
        uint8_t lh[30];
        if (!Read(z.off, lh, 30) || memcmp(lh, "PK\x03\x04", 4)) { err = T("damaged entry", "entrada danificada"); return false; }
        if (z.usize > 0x7FFFFFF0 || z.csize > 0x7FFFFFF0) { err = T("file too large", "arquivo grande demais"); return false; }
        if (z.flags & 0x40) { err = T("unsupported encryption", "criptografia nao suportada"); return false; }
        uint64_t data = z.off + 30 + *(uint16_t *)&lh[26] + *(uint16_t *)&lh[28];
        Bytes c((size_t)z.csize);
        if (!Read(data, c.data(), c.size())) { err = T("truncated archive", "pacote truncado"); return false; }
        size_t skip = 0;
        if (z.flags & 1) {
            if (c.size() < 12) { err = T("damaged entry", "entrada danificada"); return false; }
            ZipCrypto(TEXMOD_PW, sizeof TEXMOD_PW).Decrypt(c.data(), c.size());
            skip = 12;
        }
        if (z.method == 0) {
            out.assign(c.begin() + skip, c.end());
        } else if (z.method == 8) {
            out.resize((size_t)z.usize);
            int n = stbi_zlib_decode_noheader_buffer((char *)out.data(), (int)out.size(), (char *)c.data() + skip, (int)(c.size() - skip));
            if (n != (int)out.size()) { err = T("could not decompress (wrong password?)", "nao deu para descompactar (senha diferente?)"); return false; }
        } else {
            err = Fmt(T("unsupported zip method %d", "metodo de compactacao %d nao suportado"), z.method);
            return false;
        }
        if (out.size() != z.usize || Crc32(out.data(), out.size()) != z.crc) {
            err = T("corrupted data (CRC does not match)", "dados corrompidos (CRC nao bate)");
            return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------- validacao (igual ao ds2tex.py)

static uint32_t U32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint32_t FourCC(const char *s) { return U32((const uint8_t *)s); }

static int BlockBytes(uint32_t fc)
{
    if (fc == FourCC("DXT1") || fc == FourCC("ATI1")) return 8;
    if (fc == FourCC("DXT2") || fc == FourCC("DXT3") || fc == FourCC("DXT4") || fc == FourCC("DXT5") || fc == FourCC("ATI2")) return 16;
    return 0;
}

// h[i] = i-esimo uint32 depois de "DDS " (mesma numeracao do ds2tex.py)
struct DdsHeader {
    uint32_t h[31];
    explicit DdsHeader(const uint8_t *p) { memcpy(h, p + 4, sizeof h); }
    uint32_t height() const { return h[2]; }
    uint32_t width() const { return h[3]; }
    uint32_t mips() const { return h[6]; }
    uint32_t pfFlags() const { return h[19]; }
    uint32_t fourcc() const { return h[20]; }
    uint32_t bits() const { return h[21]; }
    uint32_t caps2() const { return h[27]; }
};

static std::wstring FourCCStr(uint32_t fc)
{
    wchar_t s[5] = {};
    for (int i = 0; i < 4; i++) s[i] = (wchar_t)((fc >> (8 * i)) & 0xFF);
    return s;
}

// Retorna a descricao; err vazio = ok.
static std::wstring CheckDds(const Bytes &d, std::wstring &err)
{
    err.clear();
    if (d.size() < 128 || memcmp(d.data(), "DDS ", 4)) { err = T("invalid DDS header", "cabecalho DDS invalido"); return L"dds?"; }
    DdsHeader h(d.data());
    std::wstring fmt;
    if (h.pfFlags() & 4) {
        fmt = FourCCStr(h.fourcc());
        if (h.fourcc() == FourCC("DX10")) { err = T("DX10 DDS (BC7/etc) - D3D9 does not support it", "DDS formato DX10 (BC7/etc) - D3D9 nao suporta"); return fmt; }
        static const uint32_t numeric[] = {36, 113, 114, 115, 116, 111, 112};
        bool known = BlockBytes(h.fourcc()) || std::find(std::begin(numeric), std::end(numeric), h.fourcc()) != std::end(numeric);
        if (!known) { err = T("unknown FourCC for D3D9", "FourCC desconhecido para D3D9"); return fmt; }
    } else {
        fmt = Fmt(L"RGB%u%ls", h.bits(), (h.pfFlags() & 1) ? L"A" : L"");
    }
    std::wstring desc = Fmt(L"%ls %ux%u mips=%u%ls", fmt.c_str(), h.width(), h.height(), h.mips(), (h.caps2() & 0x200) ? L" CUBE" : L"");
    if (!h.width() || !h.height()) { err = T("zero size", "dimensoes zero"); return desc; }
    int bb = BlockBytes(h.fourcc());
    if (bb && (h.pfFlags() & 4) && !(h.caps2() & 0x200)) {
        uint64_t need = 0, w = h.width(), hh = h.height();
        for (uint32_t i = 0; i < std::max(h.mips(), 1u); i++) {
            need += std::max<uint64_t>(1, (w + 3) / 4) * std::max<uint64_t>(1, (hh + 3) / 4) * bb;
            w = std::max<uint64_t>(1, w / 2); hh = std::max<uint64_t>(1, hh / 2);
        }
        if (d.size() - 128 < need)
            err = Fmt(T("truncated file (%llu < %llu bytes)", "arquivo truncado (%llu < %llu bytes)"), (unsigned long long)(d.size() - 128), (unsigned long long)need);
    }
    return desc;
}

static std::wstring CheckOther(const std::wstring &ext, const Bytes &d, std::wstring &err)
{
    err.clear();
    if (ext == L"png" && (d.size() < 8 || memcmp(d.data(), "\x89PNG\r\n\x1a\n", 8))) err = T("invalid PNG", "PNG invalido");
    if (ext == L"bmp" && (d.size() < 2 || memcmp(d.data(), "BM", 2))) err = T("invalid BMP", "BMP invalido");
    return ext;
}

// ---------------------------------------------------------------- preparacao das texturas (mipmaps / DDS)

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px; // RGBA
};

static Bytes DdsHeaderBytes(uint32_t w, uint32_t h, uint32_t mips, uint32_t fourcc, uint32_t linear)
{
    uint32_t hd[31] = {};
    hd[0] = 124; hd[2] = h; hd[3] = w; hd[18] = 32; hd[26] = 0x1000;
    if (mips > 1) { hd[1] |= 0x20000; hd[6] = mips; hd[26] |= 0x400008; }
    if (fourcc) {
        hd[1] |= 0x81007; hd[4] = linear; hd[19] = 4; hd[20] = fourcc;
    } else {
        hd[1] |= 0x100F; hd[4] = w * 4; hd[19] = 0x41; hd[21] = 32;
        hd[22] = 0x00FF0000; hd[23] = 0x0000FF00; hd[24] = 0x000000FF; hd[25] = 0xFF000000;
    }
    Bytes b(128);
    memcpy(b.data(), "DDS ", 4);
    memcpy(b.data() + 4, hd, sizeof hd);
    return b;
}

// Reducao por media de area (filtro BOX do Pillow), com alpha pre-multiplicado como o Pillow faz.
static Image Downscale(const Image &s, int dw, int dh)
{
    std::vector<float> pre((size_t)s.w * s.h * 4);
    for (size_t i = 0; i < (size_t)s.w * s.h; i++) {
        float a = s.px[i * 4 + 3] / 255.f;
        pre[i * 4 + 0] = s.px[i * 4 + 0] * a;
        pre[i * 4 + 1] = s.px[i * 4 + 1] * a;
        pre[i * 4 + 2] = s.px[i * 4 + 2] * a;
        pre[i * 4 + 3] = s.px[i * 4 + 3];
    }
    // pesos de cada pixel de origem para cada pixel de destino num eixo
    auto weights = [](int sn, int dn, std::vector<std::vector<std::pair<int, float>>> &out) {
        out.assign(dn, {});
        double scale = (double)sn / dn;
        for (int d = 0; d < dn; d++) {
            double a = d * scale, b = (d + 1) * scale;
            for (int x = (int)a; x < sn && x < b; x++) {
                double cover = std::min<double>(x + 1, b) - std::max<double>(x, a);
                if (cover > 0) out[d].push_back({x, (float)(cover / scale)});
            }
        }
    };
    std::vector<std::vector<std::pair<int, float>>> wx, wy;
    weights(s.w, dw, wx);
    weights(s.h, dh, wy);
    std::vector<float> mid((size_t)dw * s.h * 4, 0.f);
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < dw; x++)
            for (auto &k : wx[x])
                for (int c = 0; c < 4; c++) mid[((size_t)y * dw + x) * 4 + c] += pre[((size_t)y * s.w + k.first) * 4 + c] * k.second;
    Image d;
    d.w = dw; d.h = dh; d.px.resize((size_t)dw * dh * 4);
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++) {
            float acc[4] = {};
            for (auto &k : wy[y])
                for (int c = 0; c < 4; c++) acc[c] += mid[((size_t)k.first * dw + x) * 4 + c] * k.second;
            uint8_t *o = &d.px[((size_t)y * dw + x) * 4];
            int a = (int)(acc[3] + 0.5f);
            o[3] = (uint8_t)std::min(255, a);
            for (int c = 0; c < 3; c++) o[c] = a ? (uint8_t)std::min(255.f, acc[c] * 255.f / acc[3] + 0.5f) : 0;
        }
    return d;
}

static std::vector<Image> MipsBelow(const Image &img, bool binaryAlpha)
{
    std::vector<Image> out;
    while (std::max(out.empty() ? img.w : out.back().w, out.empty() ? img.h : out.back().h) > 1) {
        const Image &cur = out.empty() ? img : out.back();
        Image next = Downscale(cur, std::max(1, cur.w / 2), std::max(1, cur.h / 2));
        if (binaryAlpha)
            for (size_t i = 3; i < next.px.size(); i += 4) next.px[i] = next.px[i] >= 128 ? 255 : 0;
        out.push_back(std::move(next));
    }
    return out;
}

static void Rgb565(uint16_t c, int *rgb)
{
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    rgb[0] = (r << 3) | (r >> 2); rgb[1] = (g << 2) | (g >> 4); rgb[2] = (b << 3) | (b >> 2);
}
static uint16_t To565(const uint8_t *p) { return (uint16_t)(((p[0] * 31 + 127) / 255) << 11 | ((p[1] * 63 + 127) / 255) << 5 | ((p[2] * 31 + 127) / 255)); }

// DXT1 com pixels transparentes: modo de 3 cores (color0 <= color1, indice 3 = transparente).
// O stb_dxt so faz o modo de 4 cores, que perderia o recorte de alpha nos mipmaps.
static void CompressDxt1Alpha(uint8_t *dst, const uint8_t *blk)
{
    int best0 = -1, best1 = -1, bestD = -1;
    for (int i = 0; i < 16; i++) {
        if (blk[i * 4 + 3] < 128) continue;
        if (best0 < 0) best0 = best1 = i;
        for (int j = i; j < 16; j++) {
            if (blk[j * 4 + 3] < 128) continue;
            int d = 0;
            for (int c = 0; c < 3; c++) d += (blk[i * 4 + c] - blk[j * 4 + c]) * (blk[i * 4 + c] - blk[j * 4 + c]);
            if (d > bestD) { bestD = d; best0 = i; best1 = j; }
        }
    }
    uint16_t c0 = best0 < 0 ? 0 : To565(&blk[best0 * 4]), c1 = best1 < 0 ? 0 : To565(&blk[best1 * 4]);
    if (c0 > c1) std::swap(c0, c1);
    int pal[3][3];
    Rgb565(c0, pal[0]); Rgb565(c1, pal[1]);
    for (int c = 0; c < 3; c++) pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
    uint32_t mask = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t idx = 3;
        if (blk[i * 4 + 3] >= 128) {
            int bd = 1 << 30;
            for (int k = 0; k < 3; k++) {
                int d = 0;
                for (int c = 0; c < 3; c++) d += (blk[i * 4 + c] - pal[k][c]) * (blk[i * 4 + c] - pal[k][c]);
                if (d < bd) { bd = d; idx = k; }
            }
        }
        mask |= idx << (2 * i);
    }
    memcpy(dst, &c0, 2); memcpy(dst + 2, &c1, 2); memcpy(dst + 4, &mask, 4);
}

static void EncodeLevel(const Image &img, uint32_t fourcc, Bytes &out)
{
    if (!fourcc) { // A8R8G8B8: bytes BGRA
        size_t o = out.size();
        out.resize(o + img.px.size());
        for (size_t i = 0; i < img.px.size(); i += 4) {
            out[o + i] = img.px[i + 2]; out[o + i + 1] = img.px[i + 1];
            out[o + i + 2] = img.px[i]; out[o + i + 3] = img.px[i + 3];
        }
        return;
    }
    bool dxt5 = fourcc == FourCC("DXT5");
    for (int by = 0; by < (img.h + 3) / 4; by++)
        for (int bx = 0; bx < (img.w + 3) / 4; bx++) {
            uint8_t blk[64];
            bool transparent = false;
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++) {
                    int sx = std::min(bx * 4 + x, img.w - 1), sy = std::min(by * 4 + y, img.h - 1);
                    memcpy(&blk[(y * 4 + x) * 4], &img.px[((size_t)sy * img.w + sx) * 4], 4);
                    transparent |= blk[(y * 4 + x) * 4 + 3] < 128;
                }
            uint8_t enc[16];
            if (dxt5) stb_compress_dxt_block(enc, blk, 1, STB_DXT_HIGHQUAL);
            else if (transparent) CompressDxt1Alpha(enc, blk);
            else stb_compress_dxt_block(enc, blk, 0, STB_DXT_HIGHQUAL);
            out.insert(out.end(), enc, enc + (dxt5 ? 16 : 8));
        }
}

// Decodifica o nivel 0 de um DDS DXT1/DXT5 ou 32 bits para RGBA.
static bool DecodeDdsTop(const Bytes &raw, const DdsHeader &h, Image &img)
{
    img.w = (int)h.width(); img.h = (int)h.height();
    img.px.assign((size_t)img.w * img.h * 4, 255);
    const uint8_t *p = raw.data() + 128;
    if (!(h.pfFlags() & 4)) {
        if (raw.size() < 128 + img.px.size()) return false;
        bool alpha = h.pfFlags() & 1;
        for (size_t i = 0; i < img.px.size(); i += 4) {
            img.px[i] = p[i + 2]; img.px[i + 1] = p[i + 1]; img.px[i + 2] = p[i];
            img.px[i + 3] = alpha ? p[i + 3] : 255;
        }
        return true;
    }
    bool dxt5 = h.fourcc() == FourCC("DXT5");
    size_t bw = (img.w + 3) / 4, bh = (img.h + 3) / 4;
    if (raw.size() < 128 + bw * bh * (dxt5 ? 16 : 8)) return false;
    for (size_t by = 0; by < bh; by++)
        for (size_t bx = 0; bx < bw; bx++, p += dxt5 ? 16 : 8) {
            uint8_t alpha[16];
            const uint8_t *cb = p;
            if (dxt5) {
                int a[8] = {p[0], p[1]};
                if (a[0] > a[1]) for (int i = 1; i < 7; i++) a[i + 1] = ((7 - i) * a[0] + i * a[1]) / 7;
                else { for (int i = 1; i < 5; i++) a[i + 1] = ((5 - i) * a[0] + i * a[1]) / 5; a[6] = 0; a[7] = 255; }
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= (uint64_t)p[2 + i] << (8 * i);
                for (int i = 0; i < 16; i++) alpha[i] = (uint8_t)a[(bits >> (3 * i)) & 7];
                cb = p + 8;
            }
            uint16_t c0 = (uint16_t)(cb[0] | cb[1] << 8), c1 = (uint16_t)(cb[2] | cb[3] << 8);
            int pal[4][4];
            Rgb565(c0, pal[0]); Rgb565(c1, pal[1]);
            pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
            for (int c = 0; c < 3; c++) {
                if (c0 > c1 || dxt5) {
                    pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
                    pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
                } else {
                    pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
                    pal[3][c] = 0;
                }
            }
            if (!(c0 > c1 || dxt5)) pal[3][3] = 0;
            uint32_t idx = U32(cb + 4);
            for (int i = 0; i < 16; i++) {
                size_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= (size_t)img.w || y >= (size_t)img.h) continue;
                uint8_t *o = &img.px[(y * img.w + x) * 4];
                const int *c = pal[(idx >> (2 * i)) & 3];
                o[0] = (uint8_t)c[0]; o[1] = (uint8_t)c[1]; o[2] = (uint8_t)c[2];
                o[3] = dxt5 ? alpha[i] : (uint8_t)c[3];
            }
        }
    return true;
}

static std::wstring Ext(const std::wstring &p)
{
    size_t d = p.find_last_of(L'.');
    std::wstring e = d == std::wstring::npos ? L"" : p.substr(d + 1);
    for (auto &c : e) c = towlower(c);
    return e;
}

// Deixa a textura pronta para o jogo. Retorna false se nada mudou; senao newPath/desc.
static bool Optimize(const std::wstring &path, std::wstring &newPath, std::wstring &desc, std::wstring &err)
{
    std::wstring ext = Ext(path);
    Bytes data;
    if (ext == L"png" || ext == L"bmp" || ext == L"tga" || ext == L"jpg" || ext == L"jpeg") {
        Bytes src;
        if (!ReadAll(path, src)) { err = T("could not read", "nao deu para ler"); return false; }
        int w, h, n;
        uint8_t *px = stbi_load_from_memory(src.data(), (int)src.size(), &w, &h, &n, 4);
        if (!px) { err = Widen(stbi_failure_reason()); return false; }
        Image img;
        img.w = w; img.h = h; img.px.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
        std::vector<Image> levels = MipsBelow(img, false);
        data = DdsHeaderBytes(w, h, (uint32_t)levels.size() + 1, 0, 0);
        EncodeLevel(img, 0, data);
        for (auto &lv : levels) EncodeLevel(lv, 0, data);
        newPath = path.substr(0, path.size() - ext.size()) + L"dds";
    } else if (ext == L"dds") {
        Bytes raw;
        if (!ReadAll(path, raw, 128) || raw.size() < 128) return false;
        DdsHeader h(raw.data());
        if (h.mips() > 1 || (h.caps2() & 0x200) || std::max(h.width(), h.height()) <= 1) return false;
        uint32_t fc = 0;
        size_t top;
        if ((h.pfFlags() & 4) && (h.fourcc() == FourCC("DXT1") || h.fourcc() == FourCC("DXT5"))) {
            fc = h.fourcc();
            top = (size_t)std::max(1u, (h.width() + 3) / 4) * std::max(1u, (h.height() + 3) / 4) * (fc == FourCC("DXT1") ? 8 : 16);
        } else if (!(h.pfFlags() & 4) && h.bits() == 32 && h.h[22] == 0x00FF0000) {
            top = (size_t)h.width() * h.height() * 4;
        } else {
            return false; // formato que o plugin trata sozinho
        }
        if (!ReadAll(path, raw)) { err = T("could not read", "nao deu para ler"); return false; }
        Image img;
        if (!DecodeDdsTop(raw, h, img)) { err = T("truncated file", "arquivo truncado"); return false; }
        std::vector<Image> levels = MipsBelow(img, fc == FourCC("DXT1"));
        data = DdsHeaderBytes(h.width(), h.height(), (uint32_t)levels.size() + 1, fc, fc ? (uint32_t)top : 0);
        if (!fc) memcpy(&data[4 + 18 * 4], &h.h[18], 8 * 4); // mantem o formato de pixel original (com ou sem alpha)
        data.insert(data.end(), raw.begin() + 128, raw.begin() + 128 + top);
        for (auto &lv : levels) EncodeLevel(lv, fc, data);
        newPath = path;
    } else {
        return false;
    }
    if (!WriteReplace(newPath, data)) { err = T("could not write (disk full?)", "nao deu para gravar (disco cheio?)"); return false; }
    if (newPath != path) DeleteFileW(path.c_str());
    std::wstring e2;
    desc = CheckDds(data, e2);
    return true;
}

// ---------------------------------------------------------------- texmod\ -> texmod\_cache (port do ds2tex.py)

struct IndexEntry {
    std::wstring pack, desc;
    std::string fn;
    std::wstring ext;
    uint64_t size;
};

static std::string NormName(std::string s)
{
    for (auto &c : s) c = c == '\\' ? '/' : (char)tolower((unsigned char)c);
    while (s.compare(0, 2, "./") == 0) s.erase(0, 2);
    while (!s.empty() && s[0] == '/') s.erase(0, 1);
    return s;
}

static std::vector<std::wstring> ListPacks(const std::wstring &texdir)
{
    std::vector<std::wstring> packs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((texdir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring e = Ext(fd.cFileName);
            if (e == L"tpf" || e == L"zip") packs.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(packs.begin(), packs.end());
    Bytes lo;
    if (ReadAll(texdir + L"\\load_order.txt", lo)) {
        std::vector<std::wstring> ordered;
        std::string all(lo.begin(), lo.end());
        if (all.compare(0, 3, "\xEF\xBB\xBF") == 0) all.erase(0, 3);
        size_t p = 0;
        while (p <= all.size()) {
            size_t e = all.find('\n', p);
            std::string line = all.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? all.size() + 1 : e + 1;
            if (!line.empty() && line[0] == '#') continue;
            while (!line.empty() && isspace((unsigned char)line.back())) line.pop_back();
            while (!line.empty() && isspace((unsigned char)line[0])) line.erase(0, 1);
            std::wstring w = Widen(line);
            auto it = std::find(packs.begin(), packs.end(), w);
            if (!w.empty() && it != packs.end() && std::find(ordered.begin(), ordered.end(), w) == ordered.end()) ordered.push_back(w);
        }
        for (auto &pk : packs)
            if (std::find(ordered.begin(), ordered.end(), pk) == ordered.end()) ordered.push_back(pk);
        packs = ordered;
    }
    return packs;
}

// Assinatura dos pacotes (nome, tamanho, data): se nao mudou, o cache ja esta pronto.
static std::string PackStamp(const std::wstring &texdir, const std::vector<std::wstring> &packs)
{
    std::string s = "ds2setup " DS2SETUP_VERSION "\n";
    for (auto &p : packs) {
        WIN32_FILE_ATTRIBUTE_DATA a = {};
        GetFileAttributesExW((texdir + L"\\" + p).c_str(), GetFileExInfoStandard, &a);
        char line[128];
        snprintf(line, sizeof line, "|%lu%010lu|%lu%010lu\n", a.nFileSizeHigh, a.nFileSizeLow, a.ftLastWriteTime.dwHighDateTime, a.ftLastWriteTime.dwLowDateTime);
        s += Narrow(p) + line;
    }
    return s;
}

static bool BuildCache(const std::wstring &texdir, bool force)
{
    std::wstring cache = texdir + L"\\_cache";
    std::vector<std::wstring> packs = ListPacks(texdir);
    if (packs.empty()) return false;

    std::string stamp = PackStamp(texdir, packs);
    Bytes old;
    if (!force && IsFile(cache + L"\\index.txt") && ReadAll(cache + L"\\sources.txt", old) && std::string(old.begin(), old.end()) == stamp) {
        Out(T("Textures already prepared and the packs did not change (use --rebuild to redo).\n",
              "Texturas ja preparadas e os pacotes nao mudaram (use --rebuild para refazer).\n"));
        return true;
    }

    // 1a passada: le o diretorio de cada pacote (rapido) para estimar o espaco em disco
    std::vector<std::unique_ptr<Archive>> arcs;
    std::vector<std::wstring> problems;
    uint64_t need = 0;
    for (auto &p : packs) {
        arcs.emplace_back(new Archive);
        std::wstring err;
        if (!arcs.back()->Open(texdir + L"\\" + p, err)) {
            problems.push_back(p + L": " + err);
            arcs.back().reset();
            continue;
        }
        for (auto &z : arcs.back()->entries) need += z.usize;
    }
    if (IsDir(cache)) RemoveTree(cache);
    CreateDirectoryW(cache.c_str(), nullptr);
    uint64_t freeB = FreeSpace(cache);
    if (need > freeB) {
        Out(T("\nWARNING: the textures need up to %.1f GB and the disk has %.1f GB free.\n",
              "\nAVISO: as texturas precisam de ate %.1f GB e o disco tem %.1f GB livres.\n"), need / 1073741824.0, freeB / 1073741824.0);
        if (!Ask(T("There may not be enough disk space for the textures. Continue anyway?",
                   "Pode faltar espaco em disco para as texturas. Continuar mesmo assim?"))) return true;
    }

    std::map<uint32_t, IndexEntry> index;
    int overridden = 0;
    for (size_t pi = 0; pi < packs.size(); pi++) {
        const std::wstring &pack = packs[pi];
        Archive *a = arcs[pi].get();
        if (!a) continue;
        Out(L">> %ls\n", pack.c_str());
        std::unordered_map<std::string, const ZipEntry *> byName;
        const ZipEntry *def = nullptr;
        for (auto &z : a->entries) {
            std::string n = NormName(z.name);
            byName.emplace(n, &z);
            size_t s = n.find_last_of('/');
            if ((s == std::string::npos ? n : n.substr(s + 1)) == "texmod.def") def = &z;
        }
        Bytes defData;
        std::wstring err;
        if (!def) { problems.push_back(pack + T(": no texmod.def", ": sem texmod.def")); continue; }
        if (!a->Extract(*def, defData, err)) { problems.push_back(pack + L": texmod.def: " + err); continue; }

        std::string text;
        for (uint8_t c : defData) if (c) text += (char)c; // UTF-16 vira ASCII
        int nOk = 0, nLine = 0, nLines = (int)std::count(text.begin(), text.end(), '\n') + 1;
        size_t p = 0;
        while (p <= text.size()) {
            size_t e = text.find_first_of("\r\n", p);
            std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? text.size() + 1 : e + 1;
            size_t bar = line.find('|');
            if (bar == std::string::npos) continue;
            if (++nLine % 2000 == 0 && nLines > 4000) Out(L"   %d/%d\n", nLine, nLines);
            std::string hs = line.substr(0, bar), fn = line.substr(bar + 1);
            auto trim = [](std::string &s) {
                while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
                while (!s.empty() && isspace((unsigned char)s[0])) s.erase(0, 1);
            };
            trim(hs); trim(fn);
            for (auto &c : fn) if (c == '\\') c = '/';
            char *end;
            unsigned long long hv = strtoull(hs.c_str(), &end, 16);
            if (hs.empty() || *end) { problems.push_back(pack + T(": invalid line in def: ", ": linha invalida no def: ") + Widen(line, 28591)); continue; }
            uint32_t hash = (uint32_t)hv;
            auto prev = index.find(hash);
            if (prev != index.end()) { overridden += prev->second.pack != pack; continue; }
            std::wstring wfn = Widen(fn, 28591), ext = Ext(wfn);
            auto it = byName.find(NormName(fn));
            if (it == byName.end()) { problems.push_back(pack + L": " + wfn + T(" listed but missing", " listado mas ausente")); continue; }
            static const wchar_t *exts[] = {L"dds", L"png", L"bmp", L"tga", L"jpg", L"jpeg"};
            if (std::find_if(std::begin(exts), std::end(exts), [&](const wchar_t *x) { return ext == x; }) == std::end(exts)) {
                problems.push_back(pack + L": " + wfn + T(": unsupported format", ": formato nao suportado"));
                continue;
            }
            Bytes data;
            if (!a->Extract(*it->second, data, err)) { problems.push_back(Fmt(L"%ls: %ls (0x%08X): %ls", pack.c_str(), wfn.c_str(), hash, err.c_str())); continue; }
            std::wstring desc = ext == L"dds" ? CheckDds(data, err) : CheckOther(ext, data, err);
            if (!err.empty()) { problems.push_back(Fmt(L"%ls: %ls (0x%08X): %ls [%ls]", pack.c_str(), wfn.c_str(), hash, err.c_str(), desc.c_str())); continue; }
            std::wstring dst = Fmt(L"%ls\\0x%08X.%ls", cache.c_str(), hash, ext.c_str());
            if (!IsFile(dst) && !WriteAll(dst, data.data(), data.size())) {
                Out(T("\nERROR: could not write %ls (disk full?)\n", "\nERRO: nao deu para gravar %ls (disco cheio?)\n"), dst.c_str());
                return true;
            }
            index[hash] = {pack, desc, fn, ext, data.size()};
            nOk++;
        }
        Out(T("   %d textures\n", "   %d texturas\n"), nOk);
        arcs[pi].reset();
    }

    Out(T("\nPreparing textures (mipmaps and PNG to DDS conversion)...\n", "\nPreparando texturas (mipmaps e conversao de PNG para DDS)...\n"));
    int nOpt = 0, i = 0;
    for (auto &kv : index) {
        IndexEntry &e = kv.second;
        std::wstring path = Fmt(L"%ls\\0x%08X.%ls", cache.c_str(), kv.first, e.ext.c_str()), np, desc, err;
        if (Optimize(path, np, desc, err)) {
            e.desc = desc;
            WIN32_FILE_ATTRIBUTE_DATA fa;
            if (GetFileAttributesExW(np.c_str(), GetFileExInfoStandard, &fa)) e.size = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
            nOpt++;
        } else if (!err.empty()) {
            problems.push_back(Fmt(T("%ls: %ls (0x%08X): could not optimize: %ls", "%ls: %ls (0x%08X): nao deu para otimizar: %ls"),
                                   e.pack.c_str(), Widen(e.fn, 28591).c_str(), kv.first, err.c_str()));
        }
        if (++i % 500 == 0) Out(L"   %d/%d\n", i, (int)index.size());
    }
    Out(T("   %d textures prepared\n", "   %d texturas preparadas\n"), nOpt);

    std::string idx = "# hash | pacote | arquivo original | formato | bytes\n";
    uint64_t total = 0;
    for (auto &kv : index) {
        char h[16];
        snprintf(h, sizeof h, "0x%08X|", kv.first);
        idx += h + Narrow(kv.second.pack) + "|" + kv.second.fn + "|" + Narrow(kv.second.desc) + "|" + std::to_string(kv.second.size) + "\n";
        total += kv.second.size;
    }
    WriteAll(cache + L"\\index.txt", idx.data(), idx.size());

    Out(T("\n%d unique textures (%.0f MB) in %ls\n", "\n%d texturas unicas (%.0f MB) em %ls\n"), (int)index.size(), total / 1048576.0, cache.c_str());
    Out(T("%d duplicates ignored (earlier pack has priority)\n", "%d duplicadas ignoradas (pacote anterior tem prioridade)\n"), overridden);
    if (problems.empty()) {
        Out(T("No broken textures found.\n", "Nenhuma textura quebrada encontrada.\n"));
        WriteAll(cache + L"\\sources.txt", stamp.data(), stamp.size());
    } else {
        Out(T("\nPROBLEMS (%d):\n", "\nPROBLEMAS (%d):\n"), (int)problems.size());
        for (auto &p : problems) Out(L"  - %ls\n", p.c_str());
    }
    return true;
}

// ---------------------------------------------------------------- achar o jogo

// RRF_RT_REG_SZ tambem le REG_EXPAND_SZ (ja expandido); pedir RRF_RT_REG_EXPAND_SZ sem RRF_NOEXPAND falha
static std::wstring RegStr(HKEY root, const wchar_t *key, const wchar_t *val, REGSAM view)
{
    wchar_t buf[MAX_PATH * 2];
    DWORD sz = sizeof buf;
    if (RegGetValueW(root, key, val, RRF_RT_REG_SZ | view, nullptr, buf, &sz) != ERROR_SUCCESS) return L"";
    std::wstring s = buf;
    for (auto &c : s) if (c == L'/') c = L'\\';
    return s;
}

static bool IsGameDir(const std::wstring &d) { return !d.empty() && IsFile(d + L"\\deadspace2.exe"); }

// Le os valores de uma chave num .vdf/.acf da Steam: "chave"   "valor"
static std::vector<std::wstring> VdfValues(const std::wstring &file, const char *key)
{
    std::vector<std::wstring> out;
    Bytes b;
    if (!ReadAll(file, b)) return out;
    std::string s(b.begin(), b.end()), k = std::string("\"") + key + "\"";
    for (size_t p = 0; (p = s.find(k, p)) != std::string::npos;) {
        p += k.size();
        size_t q1 = s.find('"', p), nl = s.find('\n', p);
        if (q1 == std::string::npos || q1 > nl) continue;
        std::string v;
        size_t q = q1 + 1;
        for (; q < s.size() && s[q] != '"'; q++) {
            if (s[q] == '\\' && q + 1 < s.size()) q++;
            v += s[q];
        }
        out.push_back(Widen(v));
        p = q;
    }
    return out;
}

static std::wstring FindGame(const std::wstring &self)
{
    std::vector<std::wstring> steam;
    for (REGSAM v : {RRF_SUBKEY_WOW6432KEY, RRF_SUBKEY_WOW6464KEY}) {
        steam.push_back(RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", v));
    }
    steam.insert(steam.begin(), RegStr(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", 0));
    std::vector<std::wstring> libs;
    for (auto &s : steam) {
        if (s.empty()) continue;
        libs.push_back(s);
        for (auto &p : VdfValues(s + L"\\steamapps\\libraryfolders.vdf", "path")) libs.push_back(p);
    }
    for (auto &lib : libs) {
        std::wstring dir = L"Dead Space 2";
        auto inst = VdfValues(lib + L"\\steamapps\\appmanifest_47780.acf", "installdir");
        if (!inst.empty()) dir = inst[0];
        std::wstring g = lib + L"\\steamapps\\common\\" + dir;
        if (IsGameDir(g)) return g;
    }

    // EA app / Origin / qualquer instalador que registre o jogo em "Adicionar ou remover programas"
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
        for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
            const wchar_t *base = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
            HKEY h;
            if (RegOpenKeyExW(root, base, 0, KEY_READ | view, &h) != ERROR_SUCCESS) continue;
            wchar_t name[256];
            for (DWORD i = 0, n = 256; RegEnumKeyExW(h, i, name, &n, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS; i++, n = 256) {
                std::wstring sub = std::wstring(base) + L"\\" + name;
                REGSAM rv = view == KEY_WOW64_32KEY ? RRF_SUBKEY_WOW6432KEY : RRF_SUBKEY_WOW6464KEY;
                std::wstring dn = RegStr(root, sub.c_str(), L"DisplayName", rv);
                // "Dead Space 2", "Dead Space™ 2"...: compara so letras e numeros
                std::wstring key;
                for (wchar_t c : dn) if (iswalnum(c) && c < 128) key += towlower(c);
                if (key.find(L"deadspace2") == std::wstring::npos && wcscmp(name, L"Steam App 47780")) continue;
                std::wstring loc = RegStr(root, sub.c_str(), L"InstallLocation", rv);
                if (!loc.empty() && loc.front() == L'"') loc = loc.substr(1, loc.find(L'"', 1) - 1);
                while (!loc.empty() && loc.back() == L'\\') loc.pop_back();
                if (IsGameDir(loc)) { RegCloseKey(h); return loc; }
            }
            RegCloseKey(h);
        }
    for (const wchar_t *k : {L"SOFTWARE\\EA Games\\Dead Space 2", L"SOFTWARE\\Electronic Arts\\Dead Space 2"}) {
        std::wstring g = RegStr(HKEY_LOCAL_MACHINE, k, L"Install Dir", RRF_SUBKEY_WOW6432KEY);
        while (!g.empty() && g.back() == L'\\') g.pop_back();
        if (IsGameDir(g)) return g;
    }

    // instalador colocado dentro da pasta do jogo (ou de texmod\)
    std::wstring here = ParentDir(self);
    if (IsGameDir(here)) return here;
    if (IsGameDir(ParentDir(here))) return ParentDir(here);

    // pastas comuns em todos os discos
    DWORD drives = GetLogicalDrives();
    for (int d = 0; d < 26; d++) {
        if (!(drives & (1u << d))) continue;
        std::wstring r = Fmt(L"%lc:\\", L'A' + d);
        if (GetDriveTypeW(r.c_str()) != DRIVE_FIXED) continue;
        for (const wchar_t *sub : {L"SteamLibrary\\steamapps\\common\\Dead Space 2", L"Steam\\steamapps\\common\\Dead Space 2",
                                   L"Program Files (x86)\\Steam\\steamapps\\common\\Dead Space 2", L"Games\\Steam\\steamapps\\common\\Dead Space 2",
                                   L"Program Files\\EA Games\\Dead Space 2", L"Program Files (x86)\\Origin Games\\Dead Space 2",
                                   L"Games\\Dead Space 2"})
            if (IsGameDir(r + sub)) return r + sub;
    }
    return L"";
}

static std::wstring AskGameExe()
{
    wchar_t file[MAX_PATH * 2] = L"deadspace2.exe";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = GetConsoleWindow();
    ofn.lpstrFilter = L"Dead Space 2 (deadspace2.exe)\0deadspace2.exe\0*.exe\0*.exe\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrTitle = g_pt ? L"Onde esta o deadspace2.exe?" : L"Where is deadspace2.exe?";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return L"";
    return ParentDir(file);
}

// ---------------------------------------------------------------- instalar

static bool Elevated()
{
    HANDLE tok;
    TOKEN_ELEVATION e = {};
    DWORD n;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    bool r = GetTokenInformation(tok, TokenElevation, &e, sizeof e, &n) && e.TokenIsElevated;
    CloseHandle(tok);
    return r;
}

static bool CanWrite(const std::wstring &dir)
{
    HANDLE f = CreateFileW((dir + L"\\.ds2texinject_test").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    CloseHandle(f);
    return true;
}

static bool WriteResource(int id, const std::wstring &path)
{
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
    return g && WriteAll(path, LockResource(g), SizeofResource(nullptr, r));
}

static bool InstallMarkerPatch(const std::wstring &game)
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring zip = std::wstring(tmp) + L"MarkerPatch-ds2texinject.zip";
    Out(T("Downloading MarkerPatch...\n", "Baixando o MarkerPatch...\n"));
    if (FAILED(URLDownloadToFileW(nullptr, MARKERPATCH_URL, zip.c_str(), 0, nullptr))) {
        Out(T("  download failed (no internet?)\n", "  falha no download (sem internet?)\n"));
        return false;
    }
    Archive a;
    std::wstring err;
    bool ok = a.Open(zip, err);
    int n = 0;
    for (size_t i = 0; ok && i < a.entries.size(); i++) {
        const ZipEntry &z = a.entries[i];
        std::string name = z.name;
        for (auto &c : name) if (c == '/') c = '\\';
        if (name.empty() || name.back() == '\\') continue;
        // nunca escreve fora da pasta do jogo
        if (name.find("..") != std::string::npos || name.find(':') != std::string::npos || name[0] == '\\') continue;
        std::wstring dst = game + L"\\" + Widen(name);
        if (Ext(dst) == L"ini" && IsFile(dst)) continue; // mantem a configuracao do usuario
        for (size_t s = game.size() + 1; (s = dst.find(L'\\', s)) != std::wstring::npos; s++)
            CreateDirectoryW(dst.substr(0, s).c_str(), nullptr);
        Bytes data;
        if (!a.Extract(z, data, err) || !WriteAll(dst, data.data(), data.size())) { ok = false; break; }
        n++;
    }
    DeleteFileW(zip.c_str());
    if (!ok || !IsFile(game + L"\\dinput8.dll")) {
        Out(T("  could not extract MarkerPatch: %ls\n", "  nao deu para extrair o MarkerPatch: %ls\n"), err.c_str());
        return false;
    }
    Out(T("  MarkerPatch installed (%d files)\n", "  MarkerPatch instalado (%d arquivos)\n"), n);
    return true;
}

static int Run(int argc, wchar_t **argv)
{
    std::wstring game;
    bool rebuild = false;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--yes") || !wcscmp(argv[i], L"/y")) g_yes = true;
        else if (!wcscmp(argv[i], L"--rebuild")) rebuild = true;
        else if (!wcscmp(argv[i], L"--no-pause")) {}
        else if (argv[i][0] != L'-') game = argv[i];
    }

    Out(T("DS2TexInject %ls - Windows installer\n\n", "DS2TexInject %ls - instalador para Windows\n\n"), L"" DS2SETUP_VERSION);

    wchar_t self[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
    if (!game.empty()) {
        game = FullPath(game);
        if (Ext(game) == L"exe") game = ParentDir(game);
        if (!IsGameDir(game)) Out(T("deadspace2.exe is not in %ls\n", "deadspace2.exe nao esta em %ls\n"), game.c_str());
    }
    if (!IsGameDir(game)) game = FindGame(self);
    if (!IsGameDir(game)) {
        Out(T("Dead Space 2 folder not found automatically. Select deadspace2.exe in the window that opened.\n",
              "Pasta do Dead Space 2 nao encontrada automaticamente. Escolha o deadspace2.exe na janela que abriu.\n"));
        game = AskGameExe();
        if (!IsGameDir(game)) {
            Out(T("Cancelled. Run again with the path: DS2TexInject-Setup.exe \"C:\\path\\to\\Dead Space 2\"\n",
                  "Cancelado. Rode de novo com o caminho: DS2TexInject-Setup.exe \"C:\\caminho\\para\\Dead Space 2\"\n"));
            return 1;
        }
    }
    Out(T("Game: %ls\n", "Jogo: %ls\n"), game.c_str());

    if (!CanWrite(game)) {
        if (Elevated()) { Out(T("ERROR: cannot write to the game folder.\n", "ERRO: sem permissao para gravar na pasta do jogo.\n")); return 1; }
        Out(T("The game folder needs administrator rights. Continuing in a new window...\n",
              "A pasta do jogo precisa de permissao de administrador. Continuando em uma nova janela...\n"));
        std::wstring args = L"\"" + game + L"\"";
        if (g_yes) args += L" --yes";
        if (rebuild) args += L" --rebuild";
        SHELLEXECUTEINFOW sei = {sizeof sei};
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = self;
        sei.lpParameters = args.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei)) { Out(T("Cancelled.\n", "Cancelado.\n")); return 1; }
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        return (int)code;
    }

    // ASI loader
    if (!IsFile(game + L"\\dinput8.dll")) {
        Out(T("\nNo ASI loader (dinput8.dll) in the game folder: without it the plugin does not load.\n",
              "\nSem ASI loader (dinput8.dll) na pasta do jogo: sem ele o plugin nao e carregado.\n"));
        if (!Ask(T("No ASI loader found (dinput8.dll).\n\nDownload and install MarkerPatch now? (recommended: it loads the plugin and fixes many Dead Space 2 bugs)",
                   "Nenhum ASI loader encontrado (dinput8.dll).\n\nBaixar e instalar o MarkerPatch agora? (recomendado: carrega o plugin e corrige varios bugs do Dead Space 2)")) ||
            !InstallMarkerPatch(game))
            Out(T("WARNING: install MarkerPatch (https://github.com/Wemino/MarkerPatch) or Ultimate ASI Loader (Win32) by hand.\n",
                  "AVISO: instale o MarkerPatch (https://github.com/Wemino/MarkerPatch) ou o Ultimate ASI Loader (Win32) manualmente.\n"));
    }

    // plugin
    std::wstring plugins = game + L"\\plugins", texdir = game + L"\\texmod";
    CreateDirectoryW(plugins.c_str(), nullptr);
    CreateDirectoryW(texdir.c_str(), nullptr);
    if (!WriteResource(RES_ASI, plugins + L"\\DS2TexInject.asi")) {
        Out(T("ERROR: could not write plugins\\DS2TexInject.asi (is the game running?)\n",
              "ERRO: nao deu para gravar plugins\\DS2TexInject.asi (o jogo esta aberto?)\n"));
        return 1;
    }
    if (!IsFile(game + L"\\DS2TexInject.ini")) WriteResource(RES_INI, game + L"\\DS2TexInject.ini");
    Out(T("\nPlugin installed in %ls\\DS2TexInject.asi\n\n", "\nPlugin instalado em %ls\\DS2TexInject.asi\n\n"), plugins.c_str());

    // texturas
    if (!BuildCache(texdir, rebuild)) {
        Out(T("No .tpf or .zip texture packs in %ls\n", "Nenhum pacote de texturas .tpf ou .zip em %ls\n"), texdir.c_str());
        Out(T("Put your packs there and run this installer again. Opening the folder...\n",
              "Coloque seus pacotes la e rode este instalador de novo. Abrindo a pasta...\n"));
        ShellExecuteW(nullptr, L"open", texdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    Out(T("\nDone. Start Dead Space 2 as usual; press F10 in-game to compare before/after.\n",
          "\nPronto. Abra o Dead Space 2 normalmente; aperte F10 no jogo para comparar antes/depois.\n"));
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    g_pt = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE;
    CrcInit();
    int code = Run(argc, argv);
    // aberto com duplo clique (console so nosso): espera antes de fechar a janela
    DWORD procs[4];
    bool pause = GetConsoleProcessList(procs, 4) == 1;
    for (int i = 1; i < argc; i++) pause &= wcscmp(argv[i], L"--no-pause") != 0;
    if (pause) {
        Out(T("\nPress Enter to close...", "\nAperte Enter para fechar..."));
        wchar_t c[8]; DWORD n;
        ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), c, 1, &n, nullptr);
    }
    return code;
}
