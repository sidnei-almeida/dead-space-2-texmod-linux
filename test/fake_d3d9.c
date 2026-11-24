// d3d9.dll falso para o test.sh. Imita o que importa do d3d9 nativo do Windows: texturas (e suas
// superficies) MANAGED/SYSTEMMEM e DEFAULT sao de classes diferentes, e devices HAL e NULLREF/REF
// tambem, com vtables e metodos diferentes (DXVK e Wine usam uma classe so, por isso la esse tipo de
// bug nao aparece). Cada metodo confere se foi
// chamado num objeto da propria classe e se o objeto ainda esta vivo; o que nao bater conta como
// erro, que o fakegame.exe le no fim. Metodos que o plugin nao deveria chamar encerram o teste.
#define CINTERFACE
#define COBJMACROS
#define INITGUID
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __declspec(dllexport)

static volatile LONG g_errors, g_liveTex;
static char g_firstError[512];

static void Err(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (InterlockedIncrement(&g_errors) == 1) memcpy(g_firstError, msg, sizeof msg);
    fprintf(stderr, "[fake_d3d9] ERRO: %s\n", msg);
    fflush(stderr);
}

// ------------------------------------------------------------ metodos inesperados
// Um stub por slot e por interface. Nao retorna (a pilha stdcall nao bateria): encerra o processo.

static void Unexpected(const char *iface, int slot)
{
    Err("metodo inesperado: %s slot %d", iface, slot);
    ExitProcess(3);
}

#define STUB(p, n) static void __stdcall p##_stub##n(void) { Unexpected(#p, n); }
#define STUB10(p, t) STUB(p, t##0) STUB(p, t##1) STUB(p, t##2) STUB(p, t##3) STUB(p, t##4) \
                     STUB(p, t##5) STUB(p, t##6) STUB(p, t##7) STUB(p, t##8) STUB(p, t##9)
#define REF(p, n) (void *)p##_stub##n,
#define REF10(p, t) REF(p, t##0) REF(p, t##1) REF(p, t##2) REF(p, t##3) REF(p, t##4) \
                    REF(p, t##5) REF(p, t##6) REF(p, t##7) REF(p, t##8) REF(p, t##9)
#define STUBS(p)                                                                                     \
    STUB10(p, ) STUB10(p, 1) STUB10(p, 2) STUB10(p, 3) STUB10(p, 4) STUB10(p, 5) STUB10(p, 6)        \
    STUB10(p, 7) STUB10(p, 8) STUB10(p, 9) STUB10(p, 10) STUB10(p, 11)                              \
    static void *p##_stubs[120] = {REF10(p, ) REF10(p, 1) REF10(p, 2) REF10(p, 3) REF10(p, 4)       \
                                   REF10(p, 5) REF10(p, 6) REF10(p, 7) REF10(p, 8) REF10(p, 9)      \
                                   REF10(p, 10) REF10(p, 11)};

STUBS(d3d)
STUBS(device)
STUBS(texture)
STUBS(surface)

#define FILL_STUBS(vt, p) memcpy(&(vt), p##_stubs, sizeof(vt))

// ------------------------------------------------------------ objetos

enum { CLS_M = 'M', CLS_D = 'D' }; // M = MANAGED/SYSTEMMEM, D = DEFAULT (como CMipMap x CDriverMipMap)
enum { SURF_Y = 'Y' };             // superficie de textura DEFAULT+DYNAMIC: outra classe no mesmo pool

typedef struct FakeDevice FakeDevice;
typedef struct FakeTex FakeTex;

typedef struct FakeSurf {
    IDirect3DSurface9Vtbl *lpVtbl;
    int cls;
    FakeTex *tex;
    UINT level;
} FakeSurf;

struct FakeTex {
    IDirect3DTexture9Vtbl *lpVtbl;
    int cls, dead;
    volatile LONG ref;
    FakeDevice *dev;
    UINT w, h, levels;
    D3DFORMAT fmt;
    D3DPOOL pool;
    DWORD usage;
    BYTE *bits[16];
    UINT pitch[16], rows[16];
    int locked[16];
    FakeSurf *surf[16];
};

#define MAX_STAGES 20 // 0..15 + D3DVERTEXTEXTURESAMPLER0..3
enum { DEV_HAL = 'H', DEV_REF = 'R' }; // HAL x NULLREF/REF (como CD3DHal x CD3DRef)
struct FakeDevice {
    IDirect3DDevice9Vtbl *lpVtbl;
    int cls, dead;
    volatile LONG ref;
    FakeTex *bound[MAX_STAGES];
};

typedef struct FakeD3D {
    IDirect3D9Vtbl *lpVtbl;
    volatile LONG ref;
} FakeD3D;

static IDirect3DTexture9Vtbl g_texVt[2];  // [0] classe M, [1] classe D
static IDirect3DSurface9Vtbl g_surfVt[3]; // [0] M, [1] D, [2] Y
static IDirect3DDevice9Vtbl g_devVt[2]; // [0] HAL, [1] NULLREF/REF
static IDirect3D9Vtbl g_d3dVt;

// lista de todas as texturas ja criadas: nunca libera a memoria, para pegar uso depois do Release
static FakeTex **g_allTex;
static size_t g_allTexN, g_allTexCap;
static CRITICAL_SECTION g_cs;

static int IsFakeTex(const void *p)
{
    int r = 0;
    EnterCriticalSection(&g_cs);
    for (size_t i = 0; i < g_allTexN && !r; i++) r = g_allTex[i] == p;
    LeaveCriticalSection(&g_cs);
    return r;
}

static int LevelLayout(D3DFORMAT fmt, UINT w, UINT h, UINT *pitch, UINT *rows)
{
    switch ((DWORD)fmt) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8: *pitch = w * 4; *rows = h; return 1;
    case D3DFMT_DXT1: *pitch = ((w + 3) / 4) * 8; *rows = (h + 3) / 4; return 1;
    case D3DFMT_DXT3:
    case D3DFMT_DXT5: *pitch = ((w + 3) / 4) * 16; *rows = (h + 3) / 4; return 1;
    }
    return 0;
}

static void CheckTex(FakeTex *t, int cls, const char *method)
{
    if (t->cls != cls) Err("%s da classe %c chamado numa textura da classe %c (pool=%d)", method, cls, t->cls, t->pool);
    if (t->dead) Err("%s chamado numa textura ja liberada (%p)", method, (void *)t);
}

static ULONG TexReleaseImpl(FakeTex *t)
{
    LONG r = InterlockedDecrement(&t->ref);
    if (r == 0) { t->dead = 1; InterlockedDecrement(&g_liveTex); }
    if (r < 0) Err("Release a mais numa textura (%p)", (void *)t);
    return (ULONG)(r < 0 ? 0 : r);
}

// ------------------------------------------------------------ textura

static HRESULT Tex_QueryInterface(FakeTex *t, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DResource9) ||
        IsEqualGUID(riid, &IID_IDirect3DBaseTexture9) || IsEqualGUID(riid, &IID_IDirect3DTexture9)) {
        InterlockedIncrement(&t->ref);
        *ppv = t;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG Tex_AddRef(FakeTex *t) { return (ULONG)InterlockedIncrement(&t->ref); }
static ULONG Tex_Release(FakeTex *t) { return TexReleaseImpl(t); }
static HRESULT Tex_GetDevice(FakeTex *t, IDirect3DDevice9 **out)
{
    InterlockedIncrement(&t->dev->ref);
    *out = (IDirect3DDevice9 *)t->dev;
    return D3D_OK;
}
static D3DRESOURCETYPE Tex_GetType(FakeTex *t) { (void)t; return D3DRTYPE_TEXTURE; }
static DWORD Tex_GetLevelCount(FakeTex *t) { return t->levels; }
static HRESULT Tex_GetLevelDesc(FakeTex *t, UINT level, D3DSURFACE_DESC *d)
{
    if (level >= t->levels || !d) return D3DERR_INVALIDCALL;
    memset(d, 0, sizeof *d);
    d->Format = t->fmt;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = t->usage;
    d->Pool = t->pool;
    d->Width = t->w >> level ? t->w >> level : 1;
    d->Height = t->h >> level ? t->h >> level : 1;
    return D3D_OK;
}
static HRESULT Tex_GetSurfaceLevel(FakeTex *t, UINT level, IDirect3DSurface9 **out)
{
    if (level >= t->levels || !out) return D3DERR_INVALIDCALL;
    if (!t->surf[level]) {
        FakeSurf *s = calloc(1, sizeof *s);
        s->cls = t->cls == CLS_D && (t->usage & D3DUSAGE_DYNAMIC) ? SURF_Y : t->cls;
        s->lpVtbl = &g_surfVt[s->cls == CLS_M ? 0 : s->cls == CLS_D ? 1 : 2];
        s->tex = t;
        s->level = level;
        t->surf[level] = s;
    }
    InterlockedIncrement(&t->ref); // a superficie divide a contagem com a textura, como no d3d9
    *out = (IDirect3DSurface9 *)t->surf[level];
    return D3D_OK;
}
static HRESULT Tex_LockRect(FakeTex *t, UINT level, D3DLOCKED_RECT *lr, const RECT *rc, DWORD flags)
{
    (void)flags;
    if (level >= t->levels || !lr || rc) return D3DERR_INVALIDCALL;
    if (t->pool == D3DPOOL_DEFAULT && !(t->usage & D3DUSAGE_DYNAMIC)) return D3DERR_INVALIDCALL;
    if (t->locked[level]) { Err("LockRect num nivel ja bloqueado"); return D3DERR_INVALIDCALL; }
    t->locked[level] = 1;
    lr->pBits = t->bits[level];
    lr->Pitch = (INT)t->pitch[level];
    return D3D_OK;
}
static HRESULT Tex_UnlockRect(FakeTex *t, UINT level)
{
    if (level >= t->levels || !t->locked[level]) return D3DERR_INVALIDCALL;
    t->locked[level] = 0;
    return D3D_OK;
}

// Para cada metodo, uma versao por classe que confere a classe do objeto e chama a comum.
#define TEX_METHOD(ret, name, params, args)                                                              \
    static ret __stdcall M_Tex_##name params { CheckTex((FakeTex *)This, CLS_M, "Texture::" #name); return Tex_##name args; } \
    static ret __stdcall D_Tex_##name params { CheckTex((FakeTex *)This, CLS_D, "Texture::" #name); return Tex_##name args; }

TEX_METHOD(HRESULT, QueryInterface, (IDirect3DTexture9 *This, REFIID r, void **p), ((FakeTex *)This, r, p))
TEX_METHOD(ULONG, AddRef, (IDirect3DTexture9 *This), ((FakeTex *)This))
TEX_METHOD(ULONG, Release, (IDirect3DTexture9 *This), ((FakeTex *)This))
TEX_METHOD(HRESULT, GetDevice, (IDirect3DTexture9 *This, IDirect3DDevice9 **d), ((FakeTex *)This, d))
TEX_METHOD(D3DRESOURCETYPE, GetType, (IDirect3DTexture9 *This), ((FakeTex *)This))
TEX_METHOD(DWORD, GetLevelCount, (IDirect3DTexture9 *This), ((FakeTex *)This))
TEX_METHOD(HRESULT, GetLevelDesc, (IDirect3DTexture9 *This, UINT l, D3DSURFACE_DESC *d), ((FakeTex *)This, l, d))
TEX_METHOD(HRESULT, GetSurfaceLevel, (IDirect3DTexture9 *This, UINT l, IDirect3DSurface9 **s), ((FakeTex *)This, l, s))
TEX_METHOD(HRESULT, LockRect, (IDirect3DTexture9 *This, UINT l, D3DLOCKED_RECT *lr, const RECT *rc, DWORD f), ((FakeTex *)This, l, lr, rc, f))
TEX_METHOD(HRESULT, UnlockRect, (IDirect3DTexture9 *This, UINT l), ((FakeTex *)This, l))

// ------------------------------------------------------------ superficie

static void CheckSurf(FakeSurf *s, int cls, const char *method)
{
    if (s->cls != cls) Err("%s da classe %c chamado numa superficie da classe %c", method, cls, s->cls);
    if (s->tex->dead) Err("%s chamado numa superficie de textura ja liberada", method);
}
static HRESULT Surf_QueryInterface(FakeSurf *s, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DResource9) ||
        IsEqualGUID(riid, &IID_IDirect3DSurface9)) {
        InterlockedIncrement(&s->tex->ref);
        *ppv = s;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG Surf_AddRef(FakeSurf *s) { return (ULONG)InterlockedIncrement(&s->tex->ref); }
static ULONG Surf_Release(FakeSurf *s) { return TexReleaseImpl(s->tex); } // interno, sem passar pela vtable
static HRESULT Surf_GetContainer(FakeSurf *s, REFIID riid, void **ppv) { return Tex_QueryInterface(s->tex, riid, ppv); }
static HRESULT Surf_GetDesc(FakeSurf *s, D3DSURFACE_DESC *d) { return Tex_GetLevelDesc(s->tex, s->level, d); }
static HRESULT Surf_LockRect(FakeSurf *s, D3DLOCKED_RECT *lr, const RECT *rc, DWORD f) { return Tex_LockRect(s->tex, s->level, lr, rc, f); }
static HRESULT Surf_UnlockRect(FakeSurf *s) { return Tex_UnlockRect(s->tex, s->level); }

#define SURF_METHOD(ret, name, params, args)                                                             \
    static ret __stdcall M_Surf_##name params { CheckSurf((FakeSurf *)This, CLS_M, "Surface::" #name); return Surf_##name args; } \
    static ret __stdcall D_Surf_##name params { CheckSurf((FakeSurf *)This, CLS_D, "Surface::" #name); return Surf_##name args; } \
    static ret __stdcall Y_Surf_##name params { CheckSurf((FakeSurf *)This, SURF_Y, "Surface::" #name); return Surf_##name args; }

SURF_METHOD(HRESULT, QueryInterface, (IDirect3DSurface9 *This, REFIID r, void **p), ((FakeSurf *)This, r, p))
SURF_METHOD(ULONG, AddRef, (IDirect3DSurface9 *This), ((FakeSurf *)This))
SURF_METHOD(ULONG, Release, (IDirect3DSurface9 *This), ((FakeSurf *)This))
SURF_METHOD(HRESULT, GetContainer, (IDirect3DSurface9 *This, REFIID r, void **p), ((FakeSurf *)This, r, p))
SURF_METHOD(HRESULT, GetDesc, (IDirect3DSurface9 *This, D3DSURFACE_DESC *d), ((FakeSurf *)This, d))
SURF_METHOD(HRESULT, LockRect, (IDirect3DSurface9 *This, D3DLOCKED_RECT *lr, const RECT *rc, DWORD f), ((FakeSurf *)This, lr, rc, f))
SURF_METHOD(HRESULT, UnlockRect, (IDirect3DSurface9 *This), ((FakeSurf *)This))

// ------------------------------------------------------------ device

static int StageIndex(DWORD stage)
{
    if (stage < 16) return (int)stage;
    if (stage >= D3DVERTEXTEXTURESAMPLER0 && stage <= D3DVERTEXTEXTURESAMPLER3) return 16 + (int)(stage - D3DVERTEXTEXTURESAMPLER0);
    return -1;
}

static HRESULT Dev_QueryInterface(IDirect3DDevice9 *This, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3DDevice9)) {
        InterlockedIncrement(&((FakeDevice *)This)->ref);
        *ppv = This;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG Dev_AddRef(IDirect3DDevice9 *This) { return (ULONG)InterlockedIncrement(&((FakeDevice *)This)->ref); }
static ULONG Dev_Release(IDirect3DDevice9 *This)
{
    LONG r = InterlockedDecrement(&((FakeDevice *)This)->ref);
    if (r == 0) ((FakeDevice *)This)->dead = 1; // nunca libera a memoria: uso depois disso vira erro
    if (r < 0) Err("Release a mais num device");
    return (ULONG)(r < 0 ? 0 : r);
}

static HRESULT Dev_CreateTexture(IDirect3DDevice9 *This, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt,
                                           D3DPOOL pool, IDirect3DTexture9 **out, HANDLE *shared)
{
    UINT maxl = 1, pitch, rows;
    if (!out || shared || !w || !h || !LevelLayout(fmt, 1, 1, &pitch, &rows)) return D3DERR_INVALIDCALL;
    if ((usage & D3DUSAGE_DYNAMIC) && pool == D3DPOOL_MANAGED) return D3DERR_INVALIDCALL;
    for (UINT s = w > h ? w : h; s > 1; s >>= 1) maxl++;
    if (!levels || levels > maxl) levels = maxl;
    if (levels > 16) return D3DERR_INVALIDCALL;
    FakeTex *t = calloc(1, sizeof *t);
    t->cls = (pool == D3DPOOL_DEFAULT) ? CLS_D : CLS_M;
    t->lpVtbl = &g_texVt[t->cls == CLS_M ? 0 : 1];
    t->ref = 1;
    t->dev = (FakeDevice *)This;
    t->w = w; t->h = h; t->levels = levels; t->fmt = fmt; t->pool = pool; t->usage = usage;
    for (UINT l = 0; l < levels; l++) {
        UINT lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1;
        LevelLayout(fmt, lw, lh, &t->pitch[l], &t->rows[l]);
        t->bits[l] = calloc(t->rows[l], t->pitch[l]);
    }
    EnterCriticalSection(&g_cs);
    if (g_allTexN == g_allTexCap) {
        g_allTexCap = g_allTexCap ? g_allTexCap * 2 : 64;
        g_allTex = realloc(g_allTex, g_allTexCap * sizeof *g_allTex);
    }
    g_allTex[g_allTexN++] = t;
    LeaveCriticalSection(&g_cs);
    InterlockedIncrement(&g_liveTex);
    *out = (IDirect3DTexture9 *)t;
    return D3D_OK;
}

static HRESULT Dev_SetTexture(IDirect3DDevice9 *This, DWORD stage, IDirect3DBaseTexture9 *bt)
{
    FakeDevice *d = (FakeDevice *)This;
    int i = StageIndex(stage);
    if (i < 0) { Err("SetTexture com stage invalido %lu", stage); return D3DERR_INVALIDCALL; }
    FakeTex *t = (FakeTex *)bt;
    if (t) {
        if (!IsFakeTex(t)) { Err("SetTexture com ponteiro que nao e textura (%p)", (void *)t); return D3DERR_INVALIDCALL; }
        if (t->dead) { Err("SetTexture com textura ja liberada (%p)", (void *)t); return D3DERR_INVALIDCALL; }
        if (t->dev != d) { Err("SetTexture com textura de outro device (%p)", (void *)t); return D3DERR_INVALIDCALL; }
        InterlockedIncrement(&t->ref); // o device guarda a propria referencia (sem passar pela vtable)
    }
    if (d->bound[i]) TexReleaseImpl(d->bound[i]);
    d->bound[i] = t;
    return D3D_OK;
}

static HRESULT Dev_UpdateTexture(IDirect3DDevice9 *This, IDirect3DBaseTexture9 *src, IDirect3DBaseTexture9 *dst)
{
    (void)This;
    FakeTex *s = (FakeTex *)src, *d = (FakeTex *)dst;
    if (!s || !d || !IsFakeTex(s) || !IsFakeTex(d)) return D3DERR_INVALIDCALL;
    if (s->dead || d->dead) { Err("UpdateTexture com textura ja liberada"); return D3DERR_INVALIDCALL; }
    if (s->pool != D3DPOOL_SYSTEMMEM || d->pool != D3DPOOL_DEFAULT || s->w != d->w || s->h != d->h || s->fmt != d->fmt)
        return D3DERR_INVALIDCALL;
    UINT n = s->levels < d->levels ? s->levels : d->levels;
    for (UINT l = 0; l < n; l++) memcpy(d->bits[l], s->bits[l], (size_t)s->rows[l] * s->pitch[l]);
    return D3D_OK;
}

// Como no d3d9 de verdade: Reset falha se ainda existir alguma textura D3DPOOL_DEFAULT viva.
static HRESULT Dev_Reset(IDirect3DDevice9 *This, D3DPRESENT_PARAMETERS *pp)
{
    (void)pp;
    int alive = 0;
    EnterCriticalSection(&g_cs);
    for (size_t i = 0; i < g_allTexN; i++)
        alive += !g_allTex[i]->dead && g_allTex[i]->pool == D3DPOOL_DEFAULT && g_allTex[i]->dev == (FakeDevice *)This;
    LeaveCriticalSection(&g_cs);
    if (alive) { Err("Reset com %d textura(s) D3DPOOL_DEFAULT ainda viva(s)", alive); return D3DERR_INVALIDCALL; }
    return D3D_OK;
}

static HRESULT Dev_BeginScene(IDirect3DDevice9 *This) { (void)This; return D3D_OK; }
static HRESULT Dev_EndScene(IDirect3DDevice9 *This) { (void)This; return D3D_OK; }

// UpdateSurface sem retangulos: copia o nivel inteiro (SYSTEMMEM -> DEFAULT, como o d3d9 exige)
static HRESULT Dev_UpdateSurface(IDirect3DDevice9 *This, IDirect3DSurface9 *src, const RECT *rc, IDirect3DSurface9 *dst, const POINT *pt)
{
    (void)This;
    FakeSurf *s = (FakeSurf *)src, *d = (FakeSurf *)dst;
    if (!s || !d || rc || pt) return D3DERR_INVALIDCALL;
    FakeTex *st = s->tex, *dt = d->tex;
    if (st->dead || dt->dead) { Err("UpdateSurface com textura ja liberada"); return D3DERR_INVALIDCALL; }
    if (st->pool != D3DPOOL_SYSTEMMEM || dt->pool != D3DPOOL_DEFAULT || st->fmt != dt->fmt ||
        (st->w >> s->level) != (dt->w >> d->level) || (st->h >> s->level) != (dt->h >> d->level))
        return D3DERR_INVALIDCALL;
    memcpy(dt->bits[d->level], st->bits[s->level], (size_t)st->rows[s->level] * st->pitch[s->level]);
    return D3D_OK;
}

static void CheckDev(IDirect3DDevice9 *This, int cls, const char *method)
{
    FakeDevice *d = (FakeDevice *)This;
    if (d->cls != cls) Err("%s do device %c chamado num device %c", method, cls, d->cls);
    if (d->dead) Err("%s chamado num device ja liberado (%p)", method, (void *)d);
}

#define DEV_METHOD(ret, name, params, args)                                                              \
    static ret __stdcall H_Dev_##name params { CheckDev(This, DEV_HAL, "Device::" #name); return Dev_##name args; } \
    static ret __stdcall R_Dev_##name params { CheckDev(This, DEV_REF, "Device::" #name); return Dev_##name args; }

DEV_METHOD(HRESULT, QueryInterface, (IDirect3DDevice9 *This, REFIID r, void **p), (This, r, p))
DEV_METHOD(ULONG, AddRef, (IDirect3DDevice9 *This), (This))
DEV_METHOD(ULONG, Release, (IDirect3DDevice9 *This), (This))
DEV_METHOD(HRESULT, CreateTexture, (IDirect3DDevice9 *This, UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DTexture9 **o, HANDLE *sh),
           (This, w, h, l, u, f, p, o, sh))
DEV_METHOD(HRESULT, SetTexture, (IDirect3DDevice9 *This, DWORD st, IDirect3DBaseTexture9 *t), (This, st, t))
DEV_METHOD(HRESULT, UpdateTexture, (IDirect3DDevice9 *This, IDirect3DBaseTexture9 *a, IDirect3DBaseTexture9 *b), (This, a, b))
DEV_METHOD(HRESULT, UpdateSurface, (IDirect3DDevice9 *This, IDirect3DSurface9 *a, const RECT *r, IDirect3DSurface9 *b, const POINT *p), (This, a, r, b, p))
DEV_METHOD(HRESULT, Reset, (IDirect3DDevice9 *This, D3DPRESENT_PARAMETERS *pp), (This, pp))
DEV_METHOD(HRESULT, BeginScene, (IDirect3DDevice9 *This), (This))
DEV_METHOD(HRESULT, EndScene, (IDirect3DDevice9 *This), (This))

// ------------------------------------------------------------ IDirect3D9

static HRESULT __stdcall D3D_QueryInterface(IDirect3D9 *This, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3D9)) {
        InterlockedIncrement(&((FakeD3D *)This)->ref);
        *ppv = This;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG __stdcall D3D_AddRef(IDirect3D9 *This) { return (ULONG)InterlockedIncrement(&((FakeD3D *)This)->ref); }
static ULONG __stdcall D3D_Release(IDirect3D9 *This) { return (ULONG)InterlockedDecrement(&((FakeD3D *)This)->ref); }
static HRESULT __stdcall D3D_CreateDevice(IDirect3D9 *This, UINT adapter, D3DDEVTYPE type, HWND wnd, DWORD flags,
                                          D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    (void)This; (void)adapter; (void)wnd; (void)flags; (void)pp;
    if (!out) return D3DERR_INVALIDCALL;
    FakeDevice *d = calloc(1, sizeof *d);
    d->cls = type == D3DDEVTYPE_HAL ? DEV_HAL : DEV_REF;
    d->lpVtbl = &g_devVt[d->cls == DEV_HAL ? 0 : 1];
    d->ref = 1;
    *out = (IDirect3DDevice9 *)d;
    return D3D_OK;
}

// ------------------------------------------------------------ montagem das vtables e exports

static void InitVtables(void)
{
    static int done;
    if (done) return;
    done = 1;
    InitializeCriticalSection(&g_cs);
    for (int c = 0; c < 2; c++) {
        IDirect3DTexture9Vtbl *tv = &g_texVt[c];
        FILL_STUBS(*tv, texture);
#define SET_TEX(name) tv->name = c == 0 ? M_Tex_##name : D_Tex_##name
        SET_TEX(QueryInterface); SET_TEX(AddRef); SET_TEX(Release); SET_TEX(GetDevice); SET_TEX(GetType);
        SET_TEX(GetLevelCount); SET_TEX(GetLevelDesc); SET_TEX(GetSurfaceLevel); SET_TEX(LockRect); SET_TEX(UnlockRect);
    }
    for (int c = 0; c < 3; c++) {
        IDirect3DSurface9Vtbl *sv = &g_surfVt[c];
        FILL_STUBS(*sv, surface);
#define SET_SURF(name) sv->name = c == 0 ? M_Surf_##name : c == 1 ? D_Surf_##name : Y_Surf_##name
        SET_SURF(QueryInterface); SET_SURF(AddRef); SET_SURF(Release); SET_SURF(GetContainer); SET_SURF(GetDesc);
        SET_SURF(LockRect); SET_SURF(UnlockRect);
    }
    for (int c = 0; c < 2; c++) {
        IDirect3DDevice9Vtbl *dv = &g_devVt[c];
        FILL_STUBS(*dv, device);
#define SET_DEV(name) dv->name = c == 0 ? H_Dev_##name : R_Dev_##name
        SET_DEV(QueryInterface); SET_DEV(AddRef); SET_DEV(Release); SET_DEV(CreateTexture); SET_DEV(SetTexture);
        SET_DEV(UpdateTexture); SET_DEV(UpdateSurface); SET_DEV(Reset); SET_DEV(BeginScene); SET_DEV(EndScene);
    }
    FILL_STUBS(g_d3dVt, d3d);
    g_d3dVt.QueryInterface = D3D_QueryInterface;
    g_d3dVt.AddRef = D3D_AddRef;
    g_d3dVt.Release = D3D_Release;
    g_d3dVt.CreateDevice = D3D_CreateDevice;
}

EXPORT IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk)
{
    InitVtables();
    if (sdk != D3D_SDK_VERSION) Err("Direct3DCreate9 com SDK %u", sdk);
    FakeD3D *d = calloc(1, sizeof *d);
    d->lpVtbl = &g_d3dVt;
    d->ref = 1;
    return (IDirect3D9 *)d;
}

// Consultas do fakegame.exe
EXPORT LONG FakeErrors(void) { return g_errors; }
EXPORT const char *FakeFirstError(void) { return g_firstError; }
EXPORT LONG FakeLiveTextures(void) { return g_liveTex; }
EXPORT void *FakeBound(IDirect3DDevice9 *dev, DWORD stage)
{
    int i = StageIndex(stage);
    return i < 0 ? NULL : ((FakeDevice *)dev)->bound[i];
}
EXPORT int FakeTexClass(void *t) { return IsFakeTex(t) ? ((FakeTex *)t)->cls : 0; }
EXPORT void *FakeTexDevice(void *t) { return IsFakeTex(t) ? (void *)((FakeTex *)t)->dev : NULL; }
EXPORT const BYTE *FakeTexBits(void *t, UINT level)
{
    if (!IsFakeTex(t) || level >= ((FakeTex *)t)->levels) return NULL;
    return ((FakeTex *)t)->bits[level];
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(inst); InitVtables(); }
    return TRUE;
}
