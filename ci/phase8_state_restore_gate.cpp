#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>

#define MAX_D3D9ON12_QUEUES_LOCAL 2
struct D3D9ON12_ARGS_LOCAL {
    BOOL Enable9On12;
    IUnknown* pD3D12Device;
    IUnknown* ppD3D12Queues[MAX_D3D9ON12_QUEUES_LOCAL];
    UINT NumQueues;
    UINT NodeMask;
};
typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9On12_LOCAL)(UINT, D3D9ON12_ARGS_LOCAL*, UINT);

static const GUID IID_IDirect3DDevice9On12_LOCAL =
{0xe7fda234,0xb589,0x4049,{0x94,0x0d,0x88,0x78,0x97,0x75,0x31,0xc8}};

static int g_failures = 0;
static void failf(const char* fmt, ...) {
    ++g_failures;
    std::fprintf(stderr, "FAIL: ");
    va_list ap; va_start(ap, fmt); std::vfprintf(stderr, fmt, ap); va_end(ap);
    std::fprintf(stderr, "\n");
}
static void check_hr(HRESULT hr, const char* what) {
    if (FAILED(hr)) failf("%s hr=0x%08lX", what, (unsigned long)hr);
}
template <class T> static void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

static HRESULT compile_blob(const char* src, const char* entry, const char* target, ID3DBlob** out) {
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, std::strlen(src), "phase8_gate", nullptr, nullptr, entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
    if (FAILED(hr) && err) std::fprintf(stderr, "Compiler: %s\n", (const char*)err->GetBufferPointer());
    rel(err);
    return hr;
}

struct Vertex {
    float x,y,z;
    DWORD color;
    float u0,v0,u1,v1;
};

static const char* kVSBase =
"struct I{float3 p:POSITION0;float4 c:COLOR0;float2 a:TEXCOORD0;float2 b:TEXCOORD1;};"
"float4 VSBase(I i):POSITION0{return float4(i.p,1);}";

static const char* kPSBase =
"float4 PSBase():COLOR0{return float4(.125,.25,.5,1);}";

/* Byte-for-byte shader source text embedded by Phase 8.1/8.2. */
static const char* kVSP8 =
"row_major float4x4 W:register(c0);row_major float4x4 V:register(c4);row_major float4x4 P:register(c8);"
"struct I{float3 p:POSITION0;float4 c:COLOR0;float2 a:TEXCOORD0;float2 b:TEXCOORD1;};"
"struct O{float4 p:POSITION0;float3 v:TEXCOORD0;float2 a:TEXCOORD1;float2 b:TEXCOORD2;float4 c:COLOR0;};"
"O VSMain(I i){O o;float4 w=mul(float4(i.p,1),W);float4 v=mul(w,V);o.p=mul(v,P);o.v=v.xyz;o.a=i.a;o.b=i.b;o.c=i.c;return o;}";

static const char* kPSP8 =
"sampler2D A0:register(s0);sampler2D A1:register(s1);sampler2D A2:register(s2);sampler2D A3:register(s3);sampler2D A4:register(s4);"
"sampler2D B0:register(s5);sampler2D B1:register(s6);sampler2D B2:register(s7);sampler2D B3:register(s8);sampler2D B4:register(s9);"
"float4 UA:register(c0);float4 UB:register(c1);float4 LP:register(c2);float4 PAR:register(c3);"
"struct I{float3 v:TEXCOORD0;float2 a:TEXCOORD1;float2 b:TEXCOORD2;float4 c:COLOR0;};"
"float3 nn(float3 x){return normalize(x);}"
"float4 PSMain(I i):COLOR0{float2 ua=i.a*UA.xy+UA.zw,ub=i.b*UB.xy+UB.zw;float w=saturate(i.c.a);"
"float3 al=lerp(tex2D(A0,ua).rgb,tex2D(B0,ub).rgb,w);float3 nt=nn(lerp(tex2D(A1,ua).xyz*2-1,tex2D(B1,ub).xyz*2-1,w));"
"float ao=lerp(tex2D(A2,ua).r,tex2D(B2,ub).r,w);float r=max(.055,saturate(lerp(tex2D(A3,ua).r,tex2D(B3,ub).r,w)));"
"float sm=saturate(lerp(tex2D(A4,ua).r,tex2D(B4,ub).r,w));float3 dx=ddx(i.v),dy=ddy(i.v);float2 tx=ddx(ua),ty=ddy(ua);"
"float3 vd=nn(-i.v);float3 ng=nn(cross(dx,dy));if(dot(ng,vd)<0)ng=-ng;float3 t=nn(dx*ty.y-dy*tx.y);t=nn(t-ng*dot(ng,t));"
"float3 bb=nn(cross(ng,t));if(tx.x*ty.y-tx.y*ty.x<0)bb=-bb;float3 n=nn(t*nt.x+bb*nt.y+ng*nt.z);float3 l=nn(LP.xyz),h=nn(l+vd);"
"float nl=saturate(dot(n,l)),nv=max(1e-4,saturate(dot(n,vd)));float nh=saturate(dot(n,h)),vh=saturate(dot(vd,h));float ar=r*r,a2=ar*ar,q=nh*nh*(a2-1)+1;"
"float D=a2/(3.14159265*q*q+1e-5);float k=(r+1)*(r+1)*.125;float G=(nl/(nl*(1-k)+k))*(nv/(nv*(1-k)+k));"
"float3 f0=lerp(.02.xxx,.16.xxx,sm);float fc=1-vh;fc=fc*fc*fc*fc*fc;float3 F=f0+(1-f0)*fc;float3 sp=D*G*F/(4*nl*nv+1e-4);"
"float3 df=al*(1-F)*.318309886;float3 amb=al*i.c.rgb*lerp(.42,1,ao)*PAR.x;float lum=dot(i.c.rgb,float3(.299,.587,.114));"
"float3 dir=(df+sp)*nl*LP.w*lerp(.65,1.15,saturate(lum));return float4(saturate(amb+dir),1);}";

static bool eqf(const float* a, const float* b, size_t n) {
    return std::memcmp(a,b,n*sizeof(float)) == 0;
}

int main() {
    std::printf("Phase 8.2 D3D9On12 state-restore gate (%s)\n", sizeof(void*)==4?"x86":"x64");

    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {}; wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = L"P8GateWnd";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"P8Gate", WS_OVERLAPPEDWINDOW,
                              0,0,96,96,nullptr,nullptr,hi,nullptr);
    if (!hwnd) { failf("CreateWindow failed: %lu", GetLastError()); return 2; }

    HMODULE d3d9dll = LoadLibraryW(L"d3d9.dll");
    if (!d3d9dll) { failf("LoadLibrary(d3d9.dll) failed"); return 2; }
    auto create9on12 = (PFN_Direct3DCreate9On12_LOCAL)GetProcAddress(d3d9dll, "Direct3DCreate9On12");
    if (!create9on12) { failf("Direct3DCreate9On12 export unavailable"); return 2; }

    /* Catch-all 9On12 override. nullptr D3D12 device intentionally lets 9On12 select
       the D3D12 device corresponding to the active D3D9 adapter. */
    D3D9ON12_ARGS_LOCAL args = {};
    args.Enable9On12 = TRUE;
    IDirect3D9* d3d = create9on12(D3D_SDK_VERSION, &args, 1);
    if (!d3d) { failf("Direct3DCreate9On12 returned nullptr"); return 2; }

    D3DADAPTER_IDENTIFIER9 aid = {};
    if (SUCCEEDED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &aid)))
        std::printf("adapter=%s\n", aid.Description);

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.BackBufferWidth = 64; pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev = nullptr;
    DWORD vpFlags[2] = { D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DCREATE_SOFTWARE_VERTEXPROCESSING };
    HRESULT hr = E_FAIL;
    for (DWORD f : vpFlags) {
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, f, &pp, &dev);
        if (SUCCEEDED(hr)) break;
        pp.AutoDepthStencilFormat = D3DFMT_D16;
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, f, &pp, &dev);
        if (SUCCEEDED(hr)) break;
        pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    }
    if (FAILED(hr) || !dev) { failf("CreateDevice failed hr=0x%08lX", (unsigned long)hr); return 2; }

    IUnknown* on12 = nullptr;
    hr = dev->QueryInterface(IID_IDirect3DDevice9On12_LOCAL, (void**)&on12);
    if (FAILED(hr) || !on12) { failf("IDirect3DDevice9On12 QI failed hr=0x%08lX", (unsigned long)hr); return 2; }
    std::printf("d3d9on12=true\n");
    rel(on12);

    D3DCAPS9 caps = {};
    check_hr(dev->GetDeviceCaps(&caps), "GetDeviceCaps");
    std::printf("vs=%u.%u ps=%u.%u maxSimultaneousTextures=%lu\n",
        D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion), D3DSHADER_VERSION_MINOR(caps.VertexShaderVersion),
        D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion), D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion),
        (unsigned long)caps.MaxSimultaneousTextures);
    if (caps.VertexShaderVersion < D3DVS_VERSION(3,0) || caps.PixelShaderVersion < D3DPS_VERSION(3,0))
        failf("SM3 unavailable");
    if (caps.MaxSimultaneousTextures < 8) std::printf("note: MaxSimultaneousTextures is fixed-function capability; PS3 sampler count tested by shader creation\n");

    ID3DBlob *bvs0=nullptr,*bps0=nullptr,*bvs1=nullptr,*bps1=nullptr;
    check_hr(compile_blob(kVSBase,"VSBase","vs_3_0",&bvs0), "compile baseline VS");
    check_hr(compile_blob(kPSBase,"PSBase","ps_3_0",&bps0), "compile baseline PS");
    check_hr(compile_blob(kVSP8,"VSMain","vs_3_0",&bvs1), "compile Phase8 VS");
    check_hr(compile_blob(kPSP8,"PSMain","ps_3_0",&bps1), "compile Phase8 PS");
    if (g_failures) return 3;

    IDirect3DVertexShader9 *vs0=nullptr,*vs1=nullptr;
    IDirect3DPixelShader9 *ps0=nullptr,*ps1=nullptr;
    check_hr(dev->CreateVertexShader((DWORD*)bvs0->GetBufferPointer(),&vs0), "Create baseline VS");
    check_hr(dev->CreatePixelShader((DWORD*)bps0->GetBufferPointer(),&ps0), "Create baseline PS");
    check_hr(dev->CreateVertexShader((DWORD*)bvs1->GetBufferPointer(),&vs1), "Create Phase8 VS");
    check_hr(dev->CreatePixelShader((DWORD*)bps1->GetBufferPointer(),&ps1), "Create Phase8 PS");
    rel(bvs0); rel(bps0); rel(bvs1); rel(bps1);
    if (g_failures) return 3;

    D3DVERTEXELEMENT9 elems[] = {
        {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,12,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
        {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        {0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
        D3DDECL_END()
    };
    IDirect3DVertexDeclaration9 *decl0=nullptr,*decl1=nullptr;
    check_hr(dev->CreateVertexDeclaration(elems,&decl0), "Create decl0");
    check_hr(dev->CreateVertexDeclaration(elems,&decl1), "Create decl1");

    IDirect3DVertexBuffer9 *vb0=nullptr,*vb1=nullptr;
    IDirect3DIndexBuffer9 *ib0=nullptr,*ib1=nullptr;
    const Vertex verts[3] = {
        {-0.55f,-0.45f,0.25f,0x40FFFFFF,0,1,0,1},
        { 0.00f, 0.55f,0.25f,0x80FFFFFF,.5f,0,.5f,0},
        { 0.55f,-0.45f,0.25f,0xC0FFFFFF,1,1,1,1}
    };
    const WORD inds[3] = {0,1,2};
    check_hr(dev->CreateVertexBuffer(sizeof(verts),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb0,nullptr), "Create vb0");
    check_hr(dev->CreateVertexBuffer(sizeof(verts),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb1,nullptr), "Create vb1");
    check_hr(dev->CreateIndexBuffer(sizeof(inds),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib0,nullptr), "Create ib0");
    check_hr(dev->CreateIndexBuffer(sizeof(inds),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib1,nullptr), "Create ib1");
    void* q=nullptr;
    if (vb0 && SUCCEEDED(vb0->Lock(0,0,&q,0))) { std::memcpy(q,verts,sizeof(verts)); vb0->Unlock(); }
    if (vb1 && SUCCEEDED(vb1->Lock(0,0,&q,0))) { std::memcpy(q,verts,sizeof(verts)); vb1->Unlock(); }
    if (ib0 && SUCCEEDED(ib0->Lock(0,0,&q,0))) { std::memcpy(q,inds,sizeof(inds)); ib0->Unlock(); }
    if (ib1 && SUCCEEDED(ib1->Lock(0,0,&q,0))) { std::memcpy(q,inds,sizeof(inds)); ib1->Unlock(); }

    IDirect3DTexture9* tex0[10] = {};
    IDirect3DTexture9* tex1[10] = {};
    for (UINT i=0;i<10;i++) {
        check_hr(dev->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&tex0[i],nullptr), "Create baseline texture");
        check_hr(dev->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&tex1[i],nullptr), "Create Phase8 texture");
        D3DLOCKED_RECT lr = {};
        if (tex0[i] && SUCCEEDED(tex0[i]->LockRect(0,&lr,nullptr,0))) {
            for (int y=0;y<2;y++) for (int x=0;x<2;x++) ((DWORD*)((BYTE*)lr.pBits+y*lr.Pitch))[x]=0xFF203040u + i;
            tex0[i]->UnlockRect(0);
        }
        if (tex1[i] && SUCCEEDED(tex1[i]->LockRect(0,&lr,nullptr,0))) {
            for (int y=0;y<2;y++) for (int x=0;x<2;x++) ((DWORD*)((BYTE*)lr.pBits+y*lr.Pitch))[x]=0xFF8090A0u + i;
            tex1[i]->UnlockRect(0);
        }
    }
    if (g_failures) return 3;

    /* Establish deliberately non-PBR baseline state. */
    check_hr(dev->SetVertexDeclaration(decl0), "baseline decl");
    check_hr(dev->SetVertexShader(vs0), "baseline VS");
    check_hr(dev->SetPixelShader(ps0), "baseline PS");
    check_hr(dev->SetStreamSource(0,vb0,0,sizeof(Vertex)), "baseline VB");
    check_hr(dev->SetIndices(ib0), "baseline IB");
    D3DVIEWPORT9 vp0 = {0,0,64,64,0.0f,1.0f};
    check_hr(dev->SetViewport(&vp0), "baseline viewport");
    for (UINT s=0;s<10;s++) {
        check_hr(dev->SetTexture(s,tex0[s]), "baseline texture");
        check_hr(dev->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP), "baseline ADDRESSU");
        check_hr(dev->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP), "baseline ADDRESSV");
        check_hr(dev->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT), "baseline MAG");
        check_hr(dev->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT), "baseline MIN");
        check_hr(dev->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE), "baseline MIP");
    }
    float vconst0[48], pconst0[16];
    for (int i=0;i<48;i++) vconst0[i] = 1000.0f + i * 0.25f;
    for (int i=0;i<16;i++) pconst0[i] = -20.0f - i * 0.5f;
    check_hr(dev->SetVertexShaderConstantF(0,vconst0,12), "baseline VS constants");
    check_hr(dev->SetPixelShaderConstantF(0,pconst0,4), "baseline PS constants");

    IDirect3DSurface9 *rt0=nullptr,*ds0=nullptr;
    check_hr(dev->GetRenderTarget(0,&rt0), "capture RT0 identity");
    check_hr(dev->GetDepthStencilSurface(&ds0), "capture depth identity");

    IDirect3DStateBlock9* sb = nullptr;
    hr = dev->CreateStateBlock(D3DSBT_ALL,&sb);
    check_hr(hr, "CreateStateBlock(D3DSBT_ALL)");
    if (!sb) failf("CreateStateBlock returned null block");
    if (g_failures) return 3;

    /* Mutate the exact classes of D3D9 state touched by Phase 8 PBR. */
    check_hr(dev->SetVertexDeclaration(decl1), "P8 decl");
    check_hr(dev->SetVertexShader(vs1), "P8 VS");
    check_hr(dev->SetPixelShader(ps1), "P8 PS");
    check_hr(dev->SetStreamSource(0,vb1,0,sizeof(Vertex)), "P8 VB");
    check_hr(dev->SetIndices(ib1), "P8 IB");
    D3DVIEWPORT9 vp1 = {1,1,62,62,0.0f,1.0f};
    check_hr(dev->SetViewport(&vp1), "P8 viewport mutation");
    for (UINT s=0;s<10;s++) {
        check_hr(dev->SetTexture(s,tex1[s]), "P8 texture");
        check_hr(dev->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_MIRROR), "P8 ADDRESSU");
        check_hr(dev->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_MIRROR), "P8 ADDRESSV");
        check_hr(dev->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR), "P8 MAG");
        check_hr(dev->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_LINEAR), "P8 MIN");
        check_hr(dev->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR), "P8 MIP");
    }
    const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    check_hr(dev->SetVertexShaderConstantF(0,ident,4), "P8 W");
    check_hr(dev->SetVertexShaderConstantF(4,ident,4), "P8 V");
    check_hr(dev->SetVertexShaderConstantF(8,ident,4), "P8 P");
    const float pc[16] = {1,1,0,0, 1,1,0,0, .25f,.55f,-.7f,1.0f, .7f,0,0,0};
    check_hr(dev->SetPixelShaderConstantF(0,pc,4), "P8 PS constants");

    check_hr(dev->BeginScene(), "BeginScene");
    hr = dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1);
    check_hr(hr, "P8 DrawIndexedPrimitive");
    check_hr(dev->EndScene(), "EndScene");

    hr = sb->Apply();
    check_hr(hr, "stateblock Apply");

    /* Decompose mismatches exactly like the Phase 8 Windows gate. */
    IDirect3DSurface9 *rt=nullptr,*ds=nullptr;
    if (FAILED(dev->GetRenderTarget(0,&rt)) || rt!=rt0) failf("RT0 restore mismatch");
    if (FAILED(dev->GetDepthStencilSurface(&ds)) || ds!=ds0) failf("depth restore mismatch");
    rel(rt); rel(ds);
    D3DVIEWPORT9 vp = {};
    if (FAILED(dev->GetViewport(&vp)) || std::memcmp(&vp,&vp0,sizeof(vp0))) failf("viewport restore mismatch");

    IDirect3DVertexBuffer9* vb=nullptr; UINT off=0,stride=0;
    if (FAILED(dev->GetStreamSource(0,&vb,&off,&stride)) || vb!=vb0 || off!=0 || stride!=sizeof(Vertex)) failf("VB restore mismatch");
    rel(vb);
    IDirect3DIndexBuffer9* ib=nullptr;
    if (FAILED(dev->GetIndices(&ib)) || ib!=ib0) failf("IB restore mismatch");
    rel(ib);
    IDirect3DVertexDeclaration9* decl=nullptr;
    if (FAILED(dev->GetVertexDeclaration(&decl)) || decl!=decl0) failf("declaration restore mismatch");
    rel(decl);
    IDirect3DVertexShader9* vs=nullptr;
    if (FAILED(dev->GetVertexShader(&vs)) || vs!=vs0) failf("VS restore mismatch");
    rel(vs);
    IDirect3DPixelShader9* ps=nullptr;
    if (FAILED(dev->GetPixelShader(&ps)) || ps!=ps0) failf("PS restore mismatch");
    rel(ps);

    for (UINT s=0;s<10;s++) {
        IDirect3DBaseTexture9* t=nullptr;
        if (FAILED(dev->GetTexture(s,&t)) || t!=(IDirect3DBaseTexture9*)tex0[s]) failf("texture %u restore mismatch",s);
        rel(t);
        const D3DSAMPLERSTATETYPE sts[5] = {D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MAGFILTER,D3DSAMP_MINFILTER,D3DSAMP_MIPFILTER};
        const DWORD want[5] = {D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_NONE};
        for (int j=0;j<5;j++) {
            DWORD got=0xFFFFFFFFu;
            if (FAILED(dev->GetSamplerState(s,sts[j],&got)) || got!=want[j]) failf("sampler %u state %u restore mismatch got=%lu want=%lu",s,(unsigned)sts[j],(unsigned long)got,(unsigned long)want[j]);
        }
    }
    float vg[48] = {}, pg[16] = {};
    if (FAILED(dev->GetVertexShaderConstantF(0,vg,12)) || !eqf(vg,vconst0,48)) failf("VS constants restore mismatch");
    if (FAILED(dev->GetPixelShaderConstantF(0,pg,4)) || !eqf(pg,pconst0,16)) failf("PS constants restore mismatch");

    if (!g_failures) std::printf("PASS: state restore identical after Phase 8 PBR draw\n");
    else std::fprintf(stderr,"state restore mismatch: %d failure(s)\n",g_failures);

    rel(sb); rel(rt0); rel(ds0);
    for (int i=0;i<10;i++) { rel(tex0[i]); rel(tex1[i]); }
    rel(vb0); rel(vb1); rel(ib0); rel(ib1); rel(decl0); rel(decl1);
    rel(vs0); rel(vs1); rel(ps0); rel(ps1); rel(dev); rel(d3d);
    FreeLibrary(d3d9dll);
    DestroyWindow(hwnd);
    return g_failures ? 1 : 0;
}
