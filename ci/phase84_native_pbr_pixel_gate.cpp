#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <cmath>

static int g_fail = 0;
static void fail(const char* what, HRESULT hr = S_OK) {
    ++g_fail;
    if (hr == S_OK) std::fprintf(stderr, "FAIL: %s\n", what);
    else std::fprintf(stderr, "FAIL: %s hr=0x%08lX\n", what, (unsigned long)hr);
}
static void chk(HRESULT hr, const char* what) { if (FAILED(hr)) fail(what, hr); }
template<class T> static void rel(T*& p){ if(p){ p->Release(); p=nullptr; } }

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h,m,w,l);
}

static const char* kVS =
"row_major float4x4 W:register(c0);row_major float4x4 V:register(c4);row_major float4x4 P:register(c8);"
"struct I{float3 p:POSITION0;float4 c:COLOR0;float2 a:TEXCOORD0;float2 b:TEXCOORD1;};"
"struct O{float4 p:POSITION0;float3 v:TEXCOORD0;float2 a:TEXCOORD1;float2 b:TEXCOORD2;float4 c:COLOR0;};"
"O VSMain(I i){O o;float4 w=mul(float4(i.p,1),W);float4 v=mul(w,V);o.p=mul(v,P);o.v=v.xyz;o.a=i.a;o.b=i.b;o.c=i.c;return o;}";

static const char* kPS =
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

static const char* kPSMagenta =
"struct I{float3 v:TEXCOORD0;float2 a:TEXCOORD1;float2 b:TEXCOORD2;float4 c:COLOR0;};"
"float4 PSMain(I i):COLOR0{return float4(1,0,1,1);}";

static HRESULT compile(const char* src, const char* entry, const char* target, ID3DBlob** out) {
    ID3DBlob* err=nullptr;
    HRESULT hr=D3DCompile(src,std::strlen(src),"phase84_native_gate",nullptr,nullptr,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,out,&err);
    if(FAILED(hr) && err) std::fprintf(stderr,"Compiler: %s\n",(const char*)err->GetBufferPointer());
    rel(err);
    return hr;
}

struct Vertex { float x,y,z; DWORD c; float u0,v0,u1,v1; };

static HRESULT make_tex(IDirect3DDevice9* dev, D3DCOLOR color, IDirect3DTexture9** out) {
    *out=nullptr;
    HRESULT hr=dev->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,out,nullptr);
    if(FAILED(hr)) return hr;
    D3DLOCKED_RECT lr={};
    hr=(*out)->LockRect(0,&lr,nullptr,0);
    if(FAILED(hr)) return hr;
    *(DWORD*)lr.pBits=color;
    return (*out)->UnlockRect(0);
}

static D3DCOLOR read_center(IDirect3DDevice9* dev, IDirect3DSurface9* rt) {
    D3DSURFACE_DESC d={}; rt->GetDesc(&d);
    IDirect3DSurface9* sys=nullptr;
    HRESULT hr=dev->CreateOffscreenPlainSurface(d.Width,d.Height,d.Format,D3DPOOL_SYSTEMMEM,&sys,nullptr);
    if(FAILED(hr)){ fail("CreateOffscreenPlainSurface",hr); return 0; }
    hr=dev->GetRenderTargetData(rt,sys);
    if(FAILED(hr)){ fail("GetRenderTargetData",hr); rel(sys); return 0; }
    D3DLOCKED_RECT lr={};
    hr=sys->LockRect(&lr,nullptr,D3DLOCK_READONLY);
    if(FAILED(hr)){ fail("LockRect readback",hr); rel(sys); return 0; }
    DWORD* row=(DWORD*)((BYTE*)lr.pBits + (d.Height/2)*lr.Pitch);
    D3DCOLOR c=row[d.Width/2];
    sys->UnlockRect(); rel(sys); return c;
}

static bool near_chan(unsigned v, unsigned target, unsigned tol){ return v+tol>=target && v<=target+tol; }

int main(){
    std::printf("Phase 8.4 native D3D9 PBR pixel gate (%s)\n",sizeof(void*)==4?"x86":"x64");
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW wc={}; wc.lpfnWndProc=wndproc; wc.hInstance=hi; wc.lpszClassName=L"P84NativeGate";
    RegisterClassW(&wc);
    HWND hwnd=CreateWindowW(wc.lpszClassName,L"P84NativeGate",WS_OVERLAPPEDWINDOW,0,0,96,96,nullptr,nullptr,hi,nullptr);
    if(!hwnd){ fail("CreateWindow"); return 2; }

    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d){ fail("Direct3DCreate9"); return 2; }
    D3DPRESENT_PARAMETERS pp={};
    pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow=hwnd;
    pp.BackBufferWidth=64; pp.BackBufferHeight=64; pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev=nullptr;
    HRESULT hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr)) hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr)||!dev){ fail("CreateDevice native D3D9",hr); rel(d3d); return 2; }

    D3DCAPS9 caps={}; chk(dev->GetDeviceCaps(&caps),"GetDeviceCaps");
    std::printf("adapter native D3D9; vs=%u.%u ps=%u.%u\n",
        D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion),D3DSHADER_VERSION_MINOR(caps.VertexShaderVersion),
        D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion),D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion));
    if(caps.VertexShaderVersion<D3DVS_VERSION(3,0)||caps.PixelShaderVersion<D3DPS_VERSION(3,0)) fail("SM3 unavailable");

    ID3DBlob *bvs=nullptr,*bps=nullptr,*bpm=nullptr;
    chk(compile(kVS,"VSMain","vs_3_0",&bvs),"compile exact Phase8 VS");
    chk(compile(kPS,"PSMain","ps_3_0",&bps),"compile exact Phase8 PS");
    chk(compile(kPSMagenta,"PSMain","ps_3_0",&bpm),"compile magenta coverage PS");
    if(g_fail) return 3;

    IDirect3DVertexShader9* vs=nullptr; IDirect3DPixelShader9 *ps=nullptr,*pm=nullptr;
    chk(dev->CreateVertexShader((DWORD*)bvs->GetBufferPointer(),&vs),"CreateVertexShader");
    chk(dev->CreatePixelShader((DWORD*)bps->GetBufferPointer(),&ps),"Create exact PBR PixelShader");
    chk(dev->CreatePixelShader((DWORD*)bpm->GetBufferPointer(),&pm),"Create magenta PixelShader");
    rel(bvs); rel(bps); rel(bpm);

    D3DVERTEXELEMENT9 elems[]={
        {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,12,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
        {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        {0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
        D3DDECL_END()
    };
    IDirect3DVertexDeclaration9* decl=nullptr; chk(dev->CreateVertexDeclaration(elems,&decl),"CreateVertexDeclaration");
    const Vertex vv[]={
        {-0.8f,-0.8f,0.5f,0x80FFFFFF,0,1,0,1},
        { 0.0f, 0.8f,0.5f,0x80FFFFFF,0.5f,0,0.5f,0},
        { 0.8f,-0.8f,0.5f,0x80FFFFFF,1,1,1,1}
    };
    const WORD ii[]={0,1,2};
    IDirect3DVertexBuffer9* vb=nullptr; IDirect3DIndexBuffer9* ib=nullptr;
    chk(dev->CreateVertexBuffer(sizeof(vv),0,0,D3DPOOL_MANAGED,&vb,nullptr),"CreateVertexBuffer");
    chk(dev->CreateIndexBuffer(sizeof(ii),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr),"CreateIndexBuffer");
    void* q=nullptr; if(vb&&SUCCEEDED(vb->Lock(0,0,&q,0))){ std::memcpy(q,vv,sizeof(vv)); vb->Unlock(); } else fail("VB lock");
    if(ib&&SUCCEEDED(ib->Lock(0,0,&q,0))){ std::memcpy(q,ii,sizeof(ii)); ib->Unlock(); } else fail("IB lock");

    IDirect3DTexture9* tex[10]={};
    D3DCOLOR colors[10]={
        0xFFFF0000,0xFF8080FF,0xFFFFFFFF,0xFFA6A6A6,0xFF000000,
        0xFF00FF00,0xFF8080FF,0xFFFFFFFF,0xFFA6A6A6,0xFF000000
    };
    for(int i=0;i<10;i++){ char n[64]; std::sprintf(n,"CreateTexture s%d",i); chk(make_tex(dev,colors[i],&tex[i]),n); }

    IDirect3DStateBlock9 *sbp=nullptr,*sbv=nullptr;
    chk(dev->CreateStateBlock(D3DSBT_PIXELSTATE,&sbp),"CreateStateBlock PIXELSTATE");
    chk(dev->CreateStateBlock(D3DSBT_VERTEXSTATE,&sbv),"CreateStateBlock VERTEXSTATE");
    if(g_fail) return 4;

    IDirect3DSurface9* rt=nullptr;
    chk(dev->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr),"CreateRenderTarget");
    chk(dev->SetRenderTarget(0,rt),"SetRenderTarget");
    dev->SetDepthStencilSurface(nullptr);
    D3DVIEWPORT9 vp={0,0,64,64,0.0f,1.0f}; dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE); dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE); dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);
    dev->SetVertexDeclaration(decl); dev->SetStreamSource(0,vb,0,sizeof(Vertex)); dev->SetIndices(ib); dev->SetVertexShader(vs);

    float I[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    dev->SetVertexShaderConstantF(0,I,4); dev->SetVertexShaderConstantF(4,I,4); dev->SetVertexShaderConstantF(8,I,4);
    float pc[16]={1,1,0,0, 1,1,0,0, 0,0,-1,0, 1,0,0,0};
    dev->SetPixelShaderConstantF(0,pc,4);
    for(int i=0;i<10;i++){
        dev->SetTexture(i,tex[i]);
        dev->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        dev->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        dev->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        dev->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        dev->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
    }

    // Mirror the Phase 8 draw sequence: apply init-time state blocks, then bind PBR state.
    chk(sbp->Apply(),"Apply PIXELSTATE block"); chk(sbv->Apply(),"Apply VERTEXSTATE block");
    dev->SetRenderState(D3DRS_ZENABLE,FALSE); dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE); dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);
    dev->SetVertexDeclaration(decl); dev->SetStreamSource(0,vb,0,sizeof(Vertex)); dev->SetIndices(ib); dev->SetVertexShader(vs);
    dev->SetVertexShaderConstantF(0,I,4); dev->SetVertexShaderConstantF(4,I,4); dev->SetVertexShaderConstantF(8,I,4);
    dev->SetPixelShaderConstantF(0,pc,4);
    for(int i=0;i<10;i++) dev->SetTexture(i,tex[i]);

    chk(dev->Clear(0,nullptr,D3DCLEAR_TARGET,0xFF000000,1,0),"Clear magenta");
    chk(dev->BeginScene(),"BeginScene magenta"); dev->SetPixelShader(pm);
    chk(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1),"Draw magenta coverage"); chk(dev->EndScene(),"EndScene magenta");
    D3DCOLOR cm=read_center(dev,rt);
    unsigned mr=(cm>>16)&255, mg=(cm>>8)&255, mb=cm&255;
    std::printf("magenta center=0x%08lX rgb=%u,%u,%u\n",(unsigned long)cm,mr,mg,mb);
    if(!(mr>220 && mg<35 && mb>220)) fail("magenta coverage pixel mismatch");

    chk(dev->Clear(0,nullptr,D3DCLEAR_TARGET,0xFF000000,1,0),"Clear PBR");
    chk(dev->BeginScene(),"BeginScene PBR"); dev->SetPixelShader(ps);
    chk(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1),"Draw exact PBR"); chk(dev->EndScene(),"EndScene PBR");
    D3DCOLOR cp=read_center(dev,rt);
    unsigned pr=(cp>>16)&255, pg=(cp>>8)&255, pb=cp&255;
    std::printf("pbr center=0x%08lX rgb=%u,%u,%u\n",(unsigned long)cp,pr,pg,pb);
    if(pr<70 || pg<70 || pb>80) fail("exact PBR pixel not red/green blended as expected");

    chk(sbp->Capture(),"Capture PIXELSTATE block"); chk(sbv->Capture(),"Capture VERTEXSTATE block");

    for(auto& t:tex) rel(t); rel(rt); rel(sbp); rel(sbv); rel(vb); rel(ib); rel(decl); rel(vs); rel(ps); rel(pm); rel(dev); rel(d3d);
    DestroyWindow(hwnd);
    if(g_fail){ std::fprintf(stderr,"TOTAL FAILURES=%d\n",g_fail); return 5; }
    std::printf("PASS: native D3D9 compiles and visibly renders both coverage-magenta and exact Phase 8 PBR\n");
    return 0;
}
