// "Jogo" do test.sh. Faz o caminho do Dead Space 2 de verdade: carrega plugins\DS2TexInject.asi
// como um ASI loader (LoadLibrary), so depois carrega o d3d9.dll (o exe do jogo tambem nao o
// importa direto) e usa o Direct3D como um jogo usaria. O d3d9.dll da pasta e o falso
// (test/fake_d3d9.c), que imita o d3d9 nativo do Windows e conta chamadas na classe errada.
// Etapas: device NULLREF de "overlay" antes do jogo (passa direto); 8 threads criando texturas
// DEFAULT ao mesmo tempo; MANAGED, DEFAULT+DYNAMIC e SYSTEMMEM->DEFAULT; escrita pela superficie
// (inclusive de uma classe de superficie que aparece depois); UpdateSurface; device HAL de teste
// criado e solto depois do jogo; overlay que copia a vtable do device com hook proprio por cima do
// nosso; Reset; liberar tudo sem vazar.
// Uso: fakegame.exe [--log-alt]   (--log-alt: pasta do jogo sem permissao de escrita)
// Sai com 0 se tudo passou.
#define CINTERFACE
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

static void Check(int ok, const char *fmt, ...)
{
    va_list ap;
    printf(ok ? "  ok     " : "  FALHOU ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    if (!ok) g_fail++;
}

// ------------------------------------------------------------ hash TexMod (igual ao plugin)

static uint32_t g_crc[256];
static void CrcInit(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
        g_crc[i] = c;
    }
}
static uint32_t TexModCrc(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu; // sem xor final, como o TexMod
    while (n--) c = (c >> 8) ^ g_crc[(c ^ *p++) & 0xFF];
    return c;
}

// ------------------------------------------------------------ texturas de teste

enum { W = 8, H = 8, RW = 16, RH = 16, NTHREADS = 8, NITER = 40 };

// conteudo "original" k (8x8 A8R8G8B8). Os k em ComRep tem substituicao em texmod\_cache.
static void Pattern(int k, uint8_t *out)
{
    for (int i = 0; i < W * H * 4; i++) out[i] = (uint8_t)(i * 7 + k * 31 + 1);
}
static uint32_t Color(int k) { return 0xFF000000u | ((0x112233u + (uint32_t)k * 0x0A1B2Cu) & 0xFFFFFFu); }
static const int ComRep[] = {0, 1, 2, 4, 5, 6, 7, 8, 20, 21, 22, 23, 24, 25, 26, 27};

// DDS 16x16 A8R8G8B8 sem mipmaps (o plugin gera os mipmaps e regrava o arquivo)
static int WriteDDS(const wchar_t *path, uint32_t argb)
{
    DWORD hd[32] = {0};
    hd[0] = 0x20534444; // "DDS "
    DWORD *h = hd + 1;
    h[0] = 124; h[1] = 0x100F; h[2] = RH; h[3] = RW; h[4] = RW * 4;
    h[18] = 32; h[19] = 0x41; h[21] = 32;
    h[22] = 0x00FF0000; h[23] = 0x0000FF00; h[24] = 0x000000FF; h[25] = 0xFF000000;
    h[26] = 0x1000;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD wr;
    int ok = WriteFile(f, hd, 128, &wr, NULL) && wr == 128;
    for (int i = 0; i < RW * RH && ok; i++) ok = WriteFile(f, &argb, 4, &wr, NULL) && wr == 4;
    CloseHandle(f);
    return ok;
}

static void CopyIn(void *bits, INT pitch, int pattern)
{
    uint8_t px[W * H * 4];
    Pattern(pattern, px);
    for (int y = 0; y < H; y++) memcpy((uint8_t *)bits + y * pitch, px + y * W * 4, W * 4);
}

static IDirect3DTexture9 *MakeTex(IDirect3DDevice9 *dev, DWORD usage, D3DPOOL pool, int pattern)
{
    IDirect3DTexture9 *t = NULL;
    if (FAILED(IDirect3DDevice9_CreateTexture(dev, W, H, 1, usage, D3DFMT_A8R8G8B8, pool, &t, NULL)) || !t) return NULL;
    if (pattern >= 0) {
        D3DLOCKED_RECT lr;
        if (FAILED(IDirect3DTexture9_LockRect(t, 0, &lr, NULL, (usage & D3DUSAGE_DYNAMIC) ? D3DLOCK_DISCARD : 0))) return t;
        CopyIn(lr.pBits, lr.Pitch, pattern);
        IDirect3DTexture9_UnlockRect(t, 0);
    }
    return t;
}

// reescreve o nivel 0 pela superficie (LockRect/UnlockRect da superficie, nao da textura)
static int WriteViaSurface(IDirect3DTexture9 *t, int pattern, DWORD flags)
{
    IDirect3DSurface9 *s = NULL;
    D3DLOCKED_RECT lr;
    if (FAILED(IDirect3DTexture9_GetSurfaceLevel(t, 0, &s)) || !s) return 0;
    int ok = SUCCEEDED(IDirect3DSurface9_LockRect(s, &lr, NULL, flags));
    if (ok) {
        CopyIn(lr.pBits, lr.Pitch, pattern);
        ok = SUCCEEDED(IDirect3DSurface9_UnlockRect(s));
    }
    IDirect3DSurface9_Release(s);
    return ok;
}

typedef LONG (*FakeLong_t)(void);
typedef const char *(*FakeStr_t)(void);
typedef void *(*FakeBound_t)(IDirect3DDevice9 *, DWORD);
typedef int (*FakeTexClass_t)(void *);
typedef void *(*FakeTexDevice_t)(void *);
typedef const BYTE *(*FakeTexBits_t)(void *, UINT);
static FakeBound_t FakeBound;
static FakeTexBits_t FakeTexBits;
static FakeTexClass_t FakeTexClass;
static FakeTexDevice_t FakeTexDevice;

// a textura ligada no stage foi trocada pela substituicao certa, criada neste device?
static int IsReplaced(IDirect3DDevice9 *dev, DWORD stage, IDirect3DTexture9 *orig, uint32_t argb, uint32_t *pxOut)
{
    void *b = FakeBound(dev, stage);
    const BYTE *bits = b ? FakeTexBits(b, 0) : NULL;
    uint32_t px = bits ? *(const uint32_t *)bits : 0;
    if (pxOut) *pxOut = px;
    return b && b != orig && px == argb && FakeTexDevice(b) == (void *)dev;
}

static void CheckReplaced(IDirect3DDevice9 *dev, DWORD stage, IDirect3DTexture9 *orig, uint32_t argb, const char *what)
{
    uint32_t px;
    int ok = IsReplaced(dev, stage, orig, argb, &px);
    void *b = FakeBound(dev, stage);
    Check(ok, "%s: trocada pela substituicao (pixel 0x%08X, esperado 0x%08X, classe %c%s)", what, px, argb,
          b ? FakeTexClass(b) : '?', b && FakeTexDevice(b) != (void *)dev ? ", DE OUTRO DEVICE" : "");
}

static char g_logPath[MAX_PATH] = "DS2TexInject.log";

static int LogCount(const char *needle)
{
    FILE *f = fopen(g_logPath, "r");
    if (!f) return -1;
    char line[1024];
    int n = 0;
    while (fgets(line, sizeof line, f)) n += strstr(line, needle) != NULL;
    fclose(f);
    return n;
}

// ------------------------------------------------------------ threads

static IDirect3DDevice9 *g_dev;
static volatile LONG g_threadFails;
static HANDLE g_go; // as threads esperam aqui e largam juntas

static DWORD WINAPI Worker(LPVOID arg)
{
    int k = (int)(INT_PTR)arg; // thread k usa o stage k e o padrao 20+k
    WaitForSingleObject(g_go, INFINITE);
    for (int i = 0; i < NITER; i++) {
        IDirect3DTexture9 *t = MakeTex(g_dev, D3DUSAGE_DYNAMIC, D3DPOOL_DEFAULT, 20 + k);
        if (!t) { InterlockedIncrement(&g_threadFails); continue; }
        IDirect3DDevice9_SetTexture(g_dev, (DWORD)k, (IDirect3DBaseTexture9 *)t);
        if (!IsReplaced(g_dev, (DWORD)k, t, Color(20 + k), NULL)) InterlockedIncrement(&g_threadFails);
        IDirect3DDevice9_SetTexture(g_dev, (DWORD)k, NULL);
        IDirect3DTexture9_Release(t);
    }
    return 0;
}

// overlay que copia a vtable do device e poe um hook proprio no SetTexture da copia, chamando o que
// estava la (o nosso hook)
static HRESULT(__stdcall *g_nextSetTexture)(IDirect3DDevice9 *, DWORD, IDirect3DBaseTexture9 *);
static volatile LONG g_overlayCalls;
static HRESULT __stdcall OverlaySetTexture(IDirect3DDevice9 *d, DWORD s, IDirect3DBaseTexture9 *t)
{
    InterlockedIncrement(&g_overlayCalls);
    return g_nextSetTexture(d, s, t);
}

int main(int argc, char **argv)
{
    wchar_t path[MAX_PATH];
    uint8_t px[W * H * 4];

    // --log-alt: pasta do jogo sem permissao de escrita (jogo em C:\Program Files), o log vai para
    // %LOCALAPPDATA%\DS2TexInject\DS2TexInject.log
    int altLog = argc > 1 && !strcmp(argv[1], "--log-alt");
    if (altLog) {
        const char *la = getenv("LOCALAPPDATA");
        if (!la) { printf("ERRO: sem LOCALAPPDATA\n"); return 1; }
        snprintf(g_logPath, sizeof g_logPath, "%s\\DS2TexInject\\DS2TexInject.log", la);
        DeleteFileA(g_logPath); // de uma rodada anterior no mesmo prefixo
    }

    CrcInit();
    CreateDirectoryW(L"texmod", NULL);
    CreateDirectoryW(L"texmod\\_cache", NULL);
    for (size_t i = 0; i < sizeof ComRep / sizeof *ComRep; i++) {
        Pattern(ComRep[i], px);
        swprintf(path, MAX_PATH, L"texmod\\_cache\\0x%08X.dds", TexModCrc(px, sizeof px));
        if (!WriteDDS(path, Color(ComRep[i]))) { printf("nao deu para gravar %ls\n", path); return 1; }
    }

    printf("ASI loader: LoadLibrary(plugins\\DS2TexInject.asi)\n");
    HMODULE asi = LoadLibraryW(L"plugins\\DS2TexInject.asi");
    DWORD err = GetLastError();
    Check(asi != NULL, "o Windows carregou o plugin (erro %lu%s)", asi ? 0 : err, err == 126 && !asi ? ": falta uma DLL" : "");
    if (!asi) return 1;
    if (!altLog) {
        Check(GetFileAttributesA(g_logPath) != INVALID_FILE_ATTRIBUTES, "DS2TexInject.log criado na pasta do jogo");
    } else {
        Check(GetFileAttributesA("DS2TexInject.log") == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(g_logPath) != INVALID_FILE_ATTRIBUTES,
              "pasta do jogo sem permissao: log em %s", g_logPath);
        Check(LogCount("sem permissao de escrita") == 1, "o log diz onde foi parar");
    }

    HMODULE d3d9 = LoadLibraryW(L"d3d9.dll");
    FakeLong_t FakeErrors = d3d9 ? (FakeLong_t)(void *)GetProcAddress(d3d9, "FakeErrors") : NULL;
    if (!FakeErrors) { printf("ERRO: o d3d9.dll carregado nao e o falso (falta WINEDLLOVERRIDES=d3d9=n?)\n"); return 1; }
    FakeStr_t FakeFirstError = (FakeStr_t)(void *)GetProcAddress(d3d9, "FakeFirstError");
    FakeLong_t FakeLiveTextures = (FakeLong_t)(void *)GetProcAddress(d3d9, "FakeLiveTextures");
    FakeBound = (FakeBound_t)(void *)GetProcAddress(d3d9, "FakeBound");
    FakeTexClass = (FakeTexClass_t)(void *)GetProcAddress(d3d9, "FakeTexClass");
    FakeTexDevice = (FakeTexDevice_t)(void *)GetProcAddress(d3d9, "FakeTexDevice");
    FakeTexBits = (FakeTexBits_t)(void *)GetProcAddress(d3d9, "FakeTexBits");
    typedef IDirect3D9 *(WINAPI * Create_t)(UINT);
    Create_t create = (Create_t)(void *)GetProcAddress(d3d9, "Direct3DCreate9");
    IDirect3D9 *d3d = create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp = {0};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;

    printf("Overlay: device NULLREF criado antes do jogo\n");
    IDirect3DDevice9 *early = NULL;
    Check(SUCCEEDED(IDirect3D9_CreateDevice(d3d, 0, D3DDEVTYPE_NULLREF, NULL, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &early)) && early,
          "device NULLREF criado");
    if (early) {
        IDirect3DTexture9 *t = MakeTex(early, 0, D3DPOOL_MANAGED, 0);
        IDirect3DDevice9_SetTexture(early, 0, (IDirect3DBaseTexture9 *)t);
        Check(FakeBound(early, 0) == t, "device de overlay passa direto, sem substituicao");
        IDirect3DDevice9_SetTexture(early, 0, NULL);
        IDirect3DTexture9_Release(t);
        IDirect3DDevice9_Release(early);
    }

    IDirect3DDevice9 *dev = NULL;
    Check(SUCCEEDED(IDirect3D9_CreateDevice(d3d, 0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                                            &pp, &dev)) && dev,
          "device do jogo (HAL) criado");
    if (!dev) return 1;
    g_dev = dev;
    IDirect3DDevice9_BeginScene(dev); // primeiro quadro: e assim que o plugin sabe qual e o device do jogo
    IDirect3DDevice9_EndScene(dev);
    // uma textura DEFAULT comum primeiro: a classe de superficie das DYNAMIC (outra) so aparece depois
    IDirect3DTexture9 *warm = MakeTex(dev, 0, D3DPOOL_DEFAULT, -1);
    if (warm) IDirect3DTexture9_Release(warm);

    printf("Threads: %d threads criando texturas DEFAULT+DYNAMIC ao mesmo tempo\n", NTHREADS);
    HANDLE th[NTHREADS];
    g_go = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (int k = 0; k < NTHREADS; k++) th[k] = CreateThread(NULL, 0, Worker, (LPVOID)(INT_PTR)k, 0, NULL);
    SetEvent(g_go);
    DWORD wr = WaitForMultipleObjects(NTHREADS, th, TRUE, 60000);
    for (int k = 0; k < NTHREADS; k++) CloseHandle(th[k]);
    Check(wr == WAIT_OBJECT_0, "as threads terminaram");
    Check(g_threadFails == 0, "%d texturas criadas em paralelo, todas trocadas (falhas: %ld)", NTHREADS * NITER, g_threadFails);

    printf("Texturas:\n");
    IDirect3DTexture9 *tA = MakeTex(dev, 0, D3DPOOL_MANAGED, 0);
    IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)tA);
    CheckReplaced(dev, 0, tA, Color(0), "MANAGED");

    IDirect3DTexture9 *tB = MakeTex(dev, D3DUSAGE_DYNAMIC, D3DPOOL_DEFAULT, 1);
    IDirect3DDevice9_SetTexture(dev, 1, (IDirect3DBaseTexture9 *)tB);
    CheckReplaced(dev, 1, tB, Color(1), "DEFAULT+DYNAMIC");

    IDirect3DTexture9 *src = MakeTex(dev, 0, D3DPOOL_SYSTEMMEM, 2);
    IDirect3DTexture9 *dst = MakeTex(dev, 0, D3DPOOL_DEFAULT, -1);
    IDirect3DDevice9_UpdateTexture(dev, (IDirect3DBaseTexture9 *)src, (IDirect3DBaseTexture9 *)dst);
    IDirect3DDevice9_SetTexture(dev, 2, (IDirect3DBaseTexture9 *)dst);
    CheckReplaced(dev, 2, dst, Color(2), "SYSTEMMEM -> UpdateTexture -> DEFAULT");

    IDirect3DTexture9 *tD = MakeTex(dev, 0, D3DPOOL_MANAGED, 3);
    IDirect3DDevice9_SetTexture(dev, 3, (IDirect3DBaseTexture9 *)tD);
    Check(FakeBound(dev, 3) == tD, "textura sem substituicao continua a original");

    printf("Escrita pela superficie (o hash e refeito no SetTexture):\n");
    IDirect3DTexture9 *tE = MakeTex(dev, 0, D3DPOOL_MANAGED, 9);
    IDirect3DDevice9_SetTexture(dev, 4, (IDirect3DBaseTexture9 *)tE);
    Check(FakeBound(dev, 4) == tE, "MANAGED com conteudo sem substituicao fica a original");
    Check(WriteViaSurface(tE, 4, 0), "MANAGED reescrita pela superficie");
    IDirect3DDevice9_SetTexture(dev, 4, (IDirect3DBaseTexture9 *)tE);
    CheckReplaced(dev, 4, tE, Color(4), "MANAGED depois de reescrita pela superficie");

    IDirect3DTexture9 *tF = MakeTex(dev, D3DUSAGE_DYNAMIC, D3DPOOL_DEFAULT, 10);
    IDirect3DDevice9_SetTexture(dev, 5, (IDirect3DBaseTexture9 *)tF);
    Check(FakeBound(dev, 5) == tF, "DEFAULT+DYNAMIC com conteudo sem substituicao fica a original");
    Check(WriteViaSurface(tF, 5, D3DLOCK_DISCARD), "DEFAULT+DYNAMIC reescrita pela superficie");
    IDirect3DDevice9_SetTexture(dev, 5, (IDirect3DBaseTexture9 *)tF);
    CheckReplaced(dev, 5, tF, Color(5), "DEFAULT+DYNAMIC depois de reescrita pela superficie");

    printf("UpdateSurface (SYSTEMMEM -> DEFAULT):\n");
    IDirect3DTexture9 *usSrc = MakeTex(dev, 0, D3DPOOL_SYSTEMMEM, 6), *usDst = MakeTex(dev, 0, D3DPOOL_DEFAULT, -1);
    IDirect3DSurface9 *sSrc = NULL, *sDst = NULL;
    IDirect3DTexture9_GetSurfaceLevel(usSrc, 0, &sSrc);
    IDirect3DTexture9_GetSurfaceLevel(usDst, 0, &sDst);
    Check(SUCCEEDED(IDirect3DDevice9_UpdateSurface(dev, sSrc, NULL, sDst, NULL)), "UpdateSurface aceito");
    IDirect3DSurface9_Release(sSrc);
    IDirect3DSurface9_Release(sDst);
    IDirect3DDevice9_SetTexture(dev, 6, (IDirect3DBaseTexture9 *)usDst);
    CheckReplaced(dev, 6, usDst, Color(6), "DEFAULT preenchida por UpdateSurface");

    printf("Overlay: device HAL de teste criado e solto depois do jogo:\n");
    IDirect3DDevice9 *late = NULL;
    IDirect3D9_CreateDevice(d3d, 0, D3DDEVTYPE_HAL, NULL, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &late);
    if (late) {
        IDirect3DTexture9 *tG2 = MakeTex(late, 0, D3DPOOL_MANAGED, 7);
        IDirect3DDevice9_SetTexture(late, 0, (IDirect3DBaseTexture9 *)tG2);
        Check(FakeBound(late, 0) == tG2, "device de teste passa direto, sem substituicao");
        IDirect3DDevice9_SetTexture(late, 0, NULL);
        IDirect3DTexture9_Release(tG2);
        IDirect3DDevice9_Release(late); // solto: o plugin nao pode mais usar esse device
    }
    IDirect3DTexture9 *tG = MakeTex(dev, 0, D3DPOOL_MANAGED, 7); // MATCH: pre-carrega no device do jogo
    IDirect3DDevice9_SetTexture(dev, 7, (IDirect3DBaseTexture9 *)tG);
    CheckReplaced(dev, 7, tG, Color(7), "no device do jogo depois do device de teste");

    printf("Overlay que copia a vtable do device e poe um hook por cima do nosso:\n");
    IDirect3DDevice9Vtbl *realVt = dev->lpVtbl;
    static IDirect3DDevice9Vtbl copyVt;
    copyVt = *realVt;
    g_nextSetTexture = copyVt.SetTexture;
    copyVt.SetTexture = OverlaySetTexture;
    dev->lpVtbl = &copyVt;
    IDirect3DTexture9 *tH = MakeTex(dev, 0, D3DPOOL_MANAGED, 8);
    Check(tH != NULL, "CreateTexture pela vtable copiada funciona");
    IDirect3DDevice9_SetTexture(dev, 8, (IDirect3DBaseTexture9 *)tH);
    CheckReplaced(dev, 8, tH, Color(8), "pela vtable copiada, sem recursao");
    Check(g_overlayCalls >= 1, "o hook do overlay foi chamado (%ld vezes)", g_overlayCalls);
    IDirect3DDevice9_BeginScene(dev);
    IDirect3DDevice9_EndScene(dev);
    IDirect3DDevice9_SetTexture(dev, 8, NULL);
    dev->lpVtbl = realVt;

    printf("Reset (soltando as texturas DEFAULT do jogo antes, como o d3d9 exige):\n");
    for (DWORD s = 0; s < 9; s++) IDirect3DDevice9_SetTexture(dev, s, NULL);
    IDirect3DTexture9_Release(tB);
    IDirect3DTexture9_Release(dst);
    IDirect3DTexture9_Release(src);
    IDirect3DTexture9_Release(tF);
    IDirect3DTexture9_Release(usSrc);
    IDirect3DTexture9_Release(usDst);
    Check(SUCCEEDED(IDirect3DDevice9_Reset(dev, &pp)), "Reset aceito (nenhuma textura DEFAULT do plugin presa)");
    IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)tA);
    CheckReplaced(dev, 0, tA, Color(0), "MANAGED depois do Reset");

    printf("Liberando tudo:\n");
    IDirect3DDevice9_SetTexture(dev, 0, NULL);
    IDirect3DTexture9_Release(tA);
    IDirect3DTexture9_Release(tD);
    IDirect3DTexture9_Release(tE);
    IDirect3DTexture9_Release(tG);
    if (tH) IDirect3DTexture9_Release(tH);
    Check(FakeLiveTextures() == 0, "nenhuma textura vazou, substituicoes inclusive (vivas: %ld)", FakeLiveTextures());

    LONG errors = FakeErrors();
    Check(errors == 0, "nenhuma chamada na classe errada, em objeto ou device liberado ou com textura de outro device (%ld%s%s)", errors,
          errors ? ": " : "", errors ? FakeFirstError() : "");
    Check(LogCount("device do jogo:") == 1, "um so device do jogo escolhido (linhas: %d)", LogCount("device do jogo:"));
    Check(LogCount("MATCH") >= 8, "log com as trocas (MATCH: %d)", LogCount("MATCH"));
    Check(LogCount("ERRO") == 0 && LogCount("FALHA") == 0 && LogCount("AVISO") == 0, "log sem ERRO/FALHA/AVISO");

    IDirect3DDevice9_Release(dev);
    IDirect3D9_Release(d3d);
    printf(g_fail ? "%d verificacao(oes) falharam\n" : "tudo certo\n", g_fail);
    return g_fail ? 1 : 0;
}
