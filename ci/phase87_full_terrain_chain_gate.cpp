#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <cmath>

extern "C" HRESULT __cdecl make_one_c(IDirect3DDevice9*, DWORD, IDirect3DTexture9**);
extern "C" int __cdecl convert_or_default_c(unsigned char*, void*, DWORD, DWORD, IDirect3DTexture9**);
extern "C" int __cdecl capture83_c(void*);
extern "C" void __cdecl restore83_c(void*);

static int g_fail=0;
static void fail(const char* s, HRESULT hr=S_OK){ ++g_fail; if(hr==S_OK) std::printf("FAIL: %s\n",s); else std::printf("FAIL: %s hr=0x%08lX\n",s,(unsigned long)hr); }
static void chk(HRESULT hr,const char* s){ if(FAILED(hr)) fail(s,hr); }
template<class T> static void rel(T*&p){ if(p){p->Release();p=nullptr;} }
static LRESULT CALLBACK W(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}

#pragma section(".p8bss",read,write)
__declspec(allocate(".p8bss")) volatile unsigned char gP8LowImageMap[0x700000] = {};

static IDirect3DTexture9*& G(DWORD va){ return *(IDirect3DTexture9**)va; }
static IDirect3DDevice9*& DEV(){ return *(IDirect3DDevice9**)0x00AEC004; }

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

static HRESULT compile(const char* src,const char* entry,const char* target,ID3DBlob** out){
    ID3DBlob* err=nullptr; HRESULT hr=D3DCompile(src,std::strlen(src),"phase87_full_chain",nullptr,nullptr,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,out,&err);
    if(FAILED(hr)&&err) std::printf("Compiler: %s\n",(const char*)err->GetBufferPointer()); rel(err); return hr;
}

struct Vertex{float x,y,z;DWORD c;float u0,v0,u1,v1;};

struct FakeTex8{void** vt;IDirect3DTexture9* proxy;bool failQI;};
static HRESULT __stdcall fake_qi(void* self,REFIID iid,void** out){
    FakeTex8* f=(FakeTex8*)self; if(!out) return E_POINTER; *out=nullptr; if(f->failQI) return E_NOINTERFACE;
    if(iid==IID_IDirect3DTexture9 && f->proxy){f->proxy->AddRef();*out=f->proxy;return S_OK;} return E_NOINTERFACE;
}
static ULONG __stdcall fake_addref(void*){return 2;}
static ULONG __stdcall fake_release(void*){return 1;}

struct TexClassLayout{ unsigned char p0[0x0C]; int tileCount; unsigned char p1[0x08]; int x; int y; };
static_assert(offsetof(TexClassLayout,tileCount)==0x0C,"tileCount");
static_assert(offsetof(TexClassLayout,x)==0x18,"x");
static_assert(offsetof(TexClassLayout,y)==0x1C,"y");

static bool uv_resolve_exact_layout(const TexClassLayout* c,int texHeight,float out[4]){
    if(!c||!out) return false; int n=c->tileCount; if(n<1||n>16) return false; if(texHeight<64||texHeight>8192) return false;
    int span=n*64; if(c->x<0||c->y<0) return false; if(c->x+span>2048) return false; if(c->y+span>texHeight) return false;
    out[0]=2048.0f/(float)span; out[1]=(float)texHeight/(float)span; out[2]=-(float)c->x/(float)span; out[3]=-(float)c->y/(float)span; return true;
}

static unsigned char* mat_base(){return (unsigned char*)0x00AC8008;}
static DWORD& mat_count(){return *(DWORD*)0x00AC8004;}
static const DWORD MAT_STRIDE=0x16C;
static const DWORD MAT_ALBEDO=0x150, MAT_NORMAL=0x154, MAT_AO=0x158, MAT_ROUGH=0x15C, MAT_SPEC=0x160;

static unsigned char* find_material_pointer_key(void* cls){
    DWORD n=mat_count(); if(n<1||n>512) return nullptr; unsigned char* p=mat_base();
    for(DWORD i=0;i<n;i++,p+=MAT_STRIDE) if(*(void**)p==cls) return p; return nullptr;
}

static bool cache_from_material(unsigned char* mat,unsigned char entry[0x30],IDirect3DTexture9* noDefault){
    std::memset(entry,0,0x30); if(!mat) return false;
    struct F{DWORD mo,so,oo,ga;};
    F f[5]={{MAT_ALBEDO,0x04,0x18,0},{MAT_NORMAL,0x08,0x1C,0x00AEA180},{MAT_AO,0x0C,0x20,0x00AEA184},{MAT_ROUGH,0x10,0x24,0x00AEA188},{MAT_SPEC,0x14,0x28,0x00AEA18C}};
    for(int i=0;i<5;i++){
        void* src=*(void**)(mat+f[i].mo); IDirect3DTexture9** def = f[i].ga ? &G(f[i].ga) : &noDefault;
        if(!convert_or_default_c(entry,src,f[i].so,f[i].oo,def)) return false;
    }
    *(DWORD*)(entry+0x2C)=1; return true;
}

static void release_cache(unsigned char entry[0x30]){
    for(DWORD off=0x18;off<=0x28;off+=4){IDirect3DTexture9* p=*(IDirect3DTexture9**)(entry+off); rel(p); *(IDirect3DTexture9**)(entry+off)=nullptr;}
}

static D3DCOLOR read_center(IDirect3DDevice9* dev,IDirect3DSurface9* rt){
    D3DSURFACE_DESC d={};rt->GetDesc(&d);IDirect3DSurface9* sys=nullptr;HRESULT hr=dev->CreateOffscreenPlainSurface(d.Width,d.Height,d.Format,D3DPOOL_SYSTEMMEM,&sys,nullptr);
    if(FAILED(hr)){fail("CreateOffscreenPlainSurface",hr);return 0;} hr=dev->GetRenderTargetData(rt,sys);if(FAILED(hr)){fail("GetRenderTargetData",hr);rel(sys);return 0;}
    D3DLOCKED_RECT lr={};hr=sys->LockRect(&lr,nullptr,D3DLOCK_READONLY);if(FAILED(hr)){fail("LockRect readback",hr);rel(sys);return 0;}
    DWORD* row=(DWORD*)((BYTE*)lr.pBits+(d.Height/2)*lr.Pitch);D3DCOLOR c=row[d.Width/2];sys->UnlockRect();rel(sys);return c;
}

static bool near_color(D3DCOLOR a,D3DCOLOR b,unsigned tol){
    for(int sh=0;sh<=16;sh+=8){int x=(a>>sh)&255,y=(b>>sh)&255;if(std::abs(x-y)>(int)tol)return false;}return true;
}

struct Saved{
    IDirect3DBaseTexture9* t[10]; IDirect3DVertexDeclaration9* decl; IDirect3DVertexShader9* vs; IDirect3DPixelShader9* ps; DWORD s[10][5]; float vc[48],pc[16];
};
static const D3DSAMPLERSTATETYPE SST[5]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MAGFILTER,D3DSAMP_MINFILTER,D3DSAMP_MIPFILTER};
static void save_state(IDirect3DDevice9*d,Saved&w){std::memset(&w,0,sizeof(w));for(int i=0;i<10;i++){d->GetTexture(i,&w.t[i]);for(int j=0;j<5;j++)d->GetSamplerState(i,SST[j],&w.s[i][j]);}d->GetVertexDeclaration(&w.decl);d->GetVertexShader(&w.vs);d->GetPixelShader(&w.ps);d->GetVertexShaderConstantF(0,w.vc,12);d->GetPixelShaderConstantF(0,w.pc,4);}
static void free_state(Saved&w){for(auto&p:w.t)rel(p);rel(w.decl);rel(w.vs);rel(w.ps);}
static int compare_state(IDirect3DDevice9*d,const Saved&w){int n=0;IDirect3DVertexDeclaration9*decl=nullptr;d->GetVertexDeclaration(&decl);if(decl!=w.decl)n++;rel(decl);IDirect3DVertexShader9*vs=nullptr;d->GetVertexShader(&vs);if(vs!=w.vs)n++;rel(vs);IDirect3DPixelShader9*ps=nullptr;d->GetPixelShader(&ps);if(ps!=w.ps)n++;rel(ps);for(int i=0;i<10;i++){IDirect3DBaseTexture9*t=nullptr;d->GetTexture(i,&t);if(t!=w.t[i])n++;rel(t);for(int j=0;j<5;j++){DWORD x=0;d->GetSamplerState(i,SST[j],&x);if(x!=w.s[i][j])n++;}}float vc[48],pc[16];d->GetVertexShaderConstantF(0,vc,12);d->GetPixelShaderConstantF(0,pc,4);if(std::memcmp(vc,w.vc,sizeof(vc)))n++;if(std::memcmp(pc,w.pc,sizeof(pc)))n++;return n;}

static D3DCOLOR draw_pair(IDirect3DDevice9* dev,IDirect3DSurface9* rt,IDirect3DVertexDeclaration9* decl,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,IDirect3DVertexBuffer9* vb,IDirect3DIndexBuffer9* ib,unsigned char A[0x30],unsigned char B[0x30],const float UA[4],const float UB[4]){
    chk(dev->Clear(0,nullptr,D3DCLEAR_TARGET,0xFF102030,1.0f,0),"Clear");
    chk(dev->SetVertexDeclaration(decl),"SetVertexDeclaration");chk(dev->SetStreamSource(0,vb,0,sizeof(Vertex)),"SetStreamSource");chk(dev->SetIndices(ib),"SetIndices");chk(dev->SetVertexShader(vs),"SetVertexShader");chk(dev->SetPixelShader(ps),"SetPixelShader");
    float I[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};dev->SetVertexShaderConstantF(0,I,4);dev->SetVertexShaderConstantF(4,I,4);dev->SetVertexShaderConstantF(8,I,4);
    float pc[16]={UA[0],UA[1],UA[2],UA[3],UB[0],UB[1],UB[2],UB[3],0,0,-1,1,1,0,0,0};dev->SetPixelShaderConstantF(0,pc,4);
    for(int i=0;i<5;i++){IDirect3DTexture9* ta=*(IDirect3DTexture9**)(A+0x18+i*4);IDirect3DTexture9* tb=*(IDirect3DTexture9**)(B+0x18+i*4);chk(dev->SetTexture(i,ta),"SetTexture A");chk(dev->SetTexture(i+5,tb),"SetTexture B");}
    for(int i=0;i<10;i++){dev->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);dev->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);}
    chk(dev->BeginScene(),"BeginScene");chk(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1),"DrawIndexedPrimitive");chk(dev->EndScene(),"EndScene");
    return read_center(dev,rt);
}

int main(){
    std::printf("Phase 8.7 full terrain chain gate x86\n");gP8LowImageMap[0]=0;gP8LowImageMap[sizeof(gP8LowImageMap)-1]=0;
    MEMORY_BASIC_INFORMATION mbi={};if(!VirtualQuery((void*)0x00AEC9A0,&mbi,sizeof(mbi))||mbi.State!=MEM_COMMIT){std::printf("low image mapping unavailable base=%p\n",GetModuleHandleW(nullptr));return 2;}
    std::printf("test_image_base=%p exact_globals_mapped=true\n",GetModuleHandleW(nullptr));std::memcpy((void*)0x00AEB988,&IID_IDirect3DTexture9,sizeof(GUID));

    WNDCLASSW wc={};wc.lpfnWndProc=W;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"p87full";RegisterClassW(&wc);HWND h=CreateWindowW(wc.lpszClassName,L"p87full",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    IDirect3D9*d3=Direct3DCreate9(D3D_SDK_VERSION);if(!d3||!h){fail("window/D3D9 create");return 2;}D3DPRESENT_PARAMETERS pp={};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=h;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_UNKNOWN;
    IDirect3DDevice9*dev=nullptr;HRESULT hr=d3->CreateDevice(0,D3DDEVTYPE_HAL,h,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr))hr=d3->CreateDevice(0,D3DDEVTYPE_REF,h,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr)||!dev){fail("CreateDevice",hr);return 2;}DEV()=dev;

    D3DCAPS9 caps={};dev->GetDeviceCaps(&caps);std::printf("native D3D9 vs=%u.%u ps=%u.%u\n",D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion),D3DSHADER_VERSION_MINOR(caps.VertexShaderVersion),D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion),D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion));if(caps.VertexShaderVersion<D3DVS_VERSION(3,0)||caps.PixelShaderVersion<D3DPS_VERSION(3,0))fail("SM3 unavailable");

    // Exact neutral helper outputs.
    struct Def{DWORD addr,color;} defs[]={{0x00AEA180,0xFF8080FF},{0x00AEA184,0xFFFFFFFF},{0x00AEA188,0xFFA6A6A6},{0x00AEA18C,0xFF000000}};
    for(auto&d:defs){G(d.addr)=nullptr;hr=make_one_c(dev,d.color,&G(d.addr));if(FAILED(hr)||!G(d.addr))fail("make neutral texture",hr);}

    IDirect3DTexture9 *albA=nullptr,*albB=nullptr,*neutralN=nullptr,*whiteAO=nullptr,*rough65=nullptr,*blackS=nullptr;make_one_c(dev,0xFFFF0000,&albA);make_one_c(dev,0xFF00FF00,&albB);make_one_c(dev,0xFF8080FF,&neutralN);make_one_c(dev,0xFFFFFFFF,&whiteAO);make_one_c(dev,0xFFA6A6A6,&rough65);make_one_c(dev,0xFF000000,&blackS);
    void*vt[3]={(void*)&fake_qi,(void*)&fake_addref,(void*)&fake_release};FakeTex8 aAlb={vt,albA,false},bAlb={vt,albB,false},aN={vt,neutralN,false},aAO={vt,whiteAO,false},aR={vt,rough65,false},aS={vt,blackS,false},bN={vt,neutralN,false},bAO={vt,whiteAO,false},bR={vt,rough65,false},bS={vt,blackS,false};

    TexClassLayout classA={},classB={};classA.tileCount=4;classA.x=128;classA.y=256;classB.tileCount=8;classB.x=256;classB.y=512;float UA[4]={},UB[4]={};if(!uv_resolve_exact_layout(&classA,2048,UA)||!uv_resolve_exact_layout(&classB,2048,UB))fail("UV resolver rejected valid classes");
    std::printf("UV A={%.3f,%.3f,%.3f,%.3f} B={%.3f,%.3f,%.3f,%.3f}\n",UA[0],UA[1],UA[2],UA[3],UB[0],UB[1],UB[2],UB[3]);if(std::fabs(UA[0]-8)>0.001f||std::fabs(UA[2]+0.5f)>0.001f||std::fabs(UB[0]-4)>0.001f||std::fabs(UB[2]+0.5f)>0.001f)fail("UV constants mismatch");

    // Exact r30 pointer-key material table layout. Reverse order to prove scan, not index assumption.
    std::memset(mat_base(),0,MAT_STRIDE*2);mat_count()=2;unsigned char* mB=mat_base();unsigned char* mA=mat_base()+MAT_STRIDE;*(void**)mB=&classB;*(void**)mA=&classA;
    *(void**)(mA+MAT_ALBEDO)=&aAlb;*(void**)(mA+MAT_NORMAL)=&aN;*(void**)(mA+MAT_AO)=&aAO;*(void**)(mA+MAT_ROUGH)=&aR;*(void**)(mA+MAT_SPEC)=&aS;
    *(void**)(mB+MAT_ALBEDO)=&bAlb;*(void**)(mB+MAT_NORMAL)=&bN;*(void**)(mB+MAT_AO)=&bAO;*(void**)(mB+MAT_ROUGH)=&bR;*(void**)(mB+MAT_SPEC)=&bS;
    unsigned char*selA=find_material_pointer_key(&classA);unsigned char*selB=find_material_pointer_key(&classB);std::printf("material select A=%p B=%p\n",selA,selB);if(selA!=mA||selB!=mB)fail("material pointer-key selection mismatch");

    unsigned char refA[0x30]={},refB[0x30]={};IDirect3DTexture9*none=nullptr;if(!cache_from_material(selA,refA,none)||!cache_from_material(selB,refB,none))fail("reference all-real cache build");

    // Missing/failing process maps: albedo stays real and required, N/AO/R/S fall back to exact neutral maps.
    FakeTex8 failR={vt,rough65,true};*(void**)(mA+MAT_NORMAL)=nullptr;*(void**)(mA+MAT_AO)=nullptr;*(void**)(mA+MAT_ROUGH)=&failR;*(void**)(mA+MAT_SPEC)=nullptr;*(void**)(mB+MAT_NORMAL)=nullptr;*(void**)(mB+MAT_AO)=nullptr;*(void**)(mB+MAT_ROUGH)=nullptr;*(void**)(mB+MAT_SPEC)=nullptr;
    unsigned char missA[0x30]={},missB[0x30]={};if(!cache_from_material(selA,missA,none)||!cache_from_material(selB,missB,none))fail("missing-process cache build");
    std::printf("fallback A N=%p AO=%p R=%p S=%p\n",*(void**)(missA+0x1C),*(void**)(missA+0x20),*(void**)(missA+0x24),*(void**)(missA+0x28));
    if(*(IDirect3DTexture9**)(missA+0x1C)!=G(0x00AEA180)||*(IDirect3DTexture9**)(missA+0x20)!=G(0x00AEA184)||*(IDirect3DTexture9**)(missA+0x24)!=G(0x00AEA188)||*(IDirect3DTexture9**)(missA+0x28)!=G(0x00AEA18C))fail("A fallback pointer mismatch");
    if(*(IDirect3DTexture9**)(missB+0x1C)!=G(0x00AEA180)||*(IDirect3DTexture9**)(missB+0x20)!=G(0x00AEA184)||*(IDirect3DTexture9**)(missB+0x24)!=G(0x00AEA188)||*(IDirect3DTexture9**)(missB+0x28)!=G(0x00AEA18C))fail("B fallback pointer mismatch");

    ID3DBlob*bv=nullptr,*bp=nullptr;chk(compile(kVS,"VSMain","vs_3_0",&bv),"compile Phase8 VS");chk(compile(kPS,"PSMain","ps_3_0",&bp),"compile Phase8 PS");IDirect3DVertexShader9*vs=nullptr;IDirect3DPixelShader9*ps=nullptr;if(bv)chk(dev->CreateVertexShader((DWORD*)bv->GetBufferPointer(),&vs),"CreateVertexShader");if(bp)chk(dev->CreatePixelShader((DWORD*)bp->GetBufferPointer(),&ps),"CreatePixelShader");rel(bv);rel(bp);
    D3DVERTEXELEMENT9 elems[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};IDirect3DVertexDeclaration9*decl=nullptr;chk(dev->CreateVertexDeclaration(elems,&decl),"CreateVertexDeclaration");
    Vertex vv[3]={{-.8f,-.8f,.5f,0x80FFFFFF,0,1,0,1},{0,.8f,.5f,0x80FFFFFF,.5f,0,.5f,0},{.8f,-.8f,.5f,0x80FFFFFF,1,1,1,1}};WORD ii[3]={0,1,2};IDirect3DVertexBuffer9*vb=nullptr;IDirect3DIndexBuffer9*ib=nullptr;dev->CreateVertexBuffer(sizeof(vv),0,0,D3DPOOL_MANAGED,&vb,nullptr);dev->CreateIndexBuffer(sizeof(ii),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr);void*q=nullptr;if(vb&&SUCCEEDED(vb->Lock(0,0,&q,0))){std::memcpy(q,vv,sizeof(vv));vb->Unlock();}if(ib&&SUCCEEDED(ib->Lock(0,0,&q,0))){std::memcpy(q,ii,sizeof(ii));ib->Unlock();}
    IDirect3DSurface9*rt=nullptr;chk(dev->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr),"CreateRenderTarget");chk(dev->SetRenderTarget(0,rt),"SetRenderTarget");dev->SetDepthStencilSurface(nullptr);D3DVIEWPORT9 vp={0,0,64,64,0,1};dev->SetViewport(&vp);dev->SetRenderState(D3DRS_ZENABLE,FALSE);dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);

    // Baseline state for exact Phase8.3 targeted capture/restore.
    IDirect3DTexture9*baseTex[10]={};for(int i=0;i<10;i++)make_one_c(dev,0xFF202020+i,&baseTex[i]);dev->SetVertexShader(nullptr);dev->SetPixelShader(nullptr);dev->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX2);for(int i=0;i<10;i++){dev->SetTexture(i,baseTex[i]);dev->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);dev->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);dev->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT);dev->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT);dev->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);}float vc0[48],pc0[16];for(int i=0;i<48;i++)vc0[i]=10+i*.1f;for(int i=0;i<16;i++)pc0[i]=-2-i*.1f;dev->SetVertexShaderConstantF(0,vc0,12);dev->SetPixelShaderConstantF(0,pc0,4);Saved baseline={};save_state(dev,baseline);IDirect3DBaseTexture9*snap[10]={};if(!capture83_c(snap))fail("exact capture83 failed");

    D3DCOLOR cRef=draw_pair(dev,rt,decl,vs,ps,vb,ib,refA,refB,UA,UB);std::printf("reference PBR pixel=0x%08lX\n",(unsigned long)cRef);restore83_c(snap);int mism=compare_state(dev,baseline);std::printf("restore mismatch_count=%d\n",mism);if(mism)fail("targeted restore mismatch");

    // Capture again, then render the missing-process path through the same exact shader/draw sequence.
    if(!capture83_c(snap))fail("second exact capture83 failed");D3DCOLOR cMiss=draw_pair(dev,rt,decl,vs,ps,vb,ib,missA,missB,UA,UB);std::printf("missing-process PBR pixel=0x%08lX\n",(unsigned long)cMiss);restore83_c(snap);mism=compare_state(dev,baseline);std::printf("restore2 mismatch_count=%d\n",mism);if(mism)fail("second targeted restore mismatch");
    if(cRef==0xFF102030||cMiss==0xFF102030)fail("PBR draw did not touch readback pixel");if(!near_color(cRef,cMiss,2)){std::printf("reference/default pixel mismatch ref=0x%08lX miss=0x%08lX\n",(unsigned long)cRef,(unsigned long)cMiss);fail("neutral fallback changes physically-neutral reference");}

    free_state(baseline);release_cache(refA);release_cache(refB);release_cache(missA);release_cache(missB);for(auto&d:defs){IDirect3DTexture9*p=G(d.addr);rel(p);G(d.addr)=nullptr;}for(auto&p:baseTex)rel(p);rel(albA);rel(albB);rel(neutralN);rel(whiteAO);rel(rough65);rel(blackS);rel(vb);rel(ib);rel(decl);rel(vs);rel(ps);DEV()=nullptr;rel(rt);rel(dev);rel(d3);DestroyWindow(h);
    if(g_fail){std::printf("TOTAL FAILURES=%d\n",g_fail);return 1;}std::printf("PASS: material selection -> UV -> cache/defaults -> exact PBR shader -> draw/readback -> exact restore\n");return 0;
}
