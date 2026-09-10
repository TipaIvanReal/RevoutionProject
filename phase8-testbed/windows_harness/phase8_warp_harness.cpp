#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdint>

using Microsoft::WRL::ComPtr;

static constexpr UINT W = 256;
static constexpr UINT H = 256;
static constexpr UINT TEX_W = 64;
static constexpr UINT TEX_H = 64;

struct Vertex {
    float x,y,z;
    D3DCOLOR color;
    float u0,v0;
    float u1,v1;
};
static_assert(sizeof(Vertex)==32, "Generals terrain stride must stay 32 bytes");

struct Mat4 { float m[4][4]; };

struct Result {
    bool ok = false;
    bool on12 = false;
    bool same_d3d12_device = false;
    bool state_restore = false;
    bool blend_response = false;
    bool normal_response = false;
    bool ao_response = false;
    bool roughness_response = false;
    bool specular_response = false;
    double base_mean = 0.0;
    double left_mean = 0.0;
    double right_mean = 0.0;
    double normal_delta = 0.0;
    double ao_delta = 0.0;
    double roughness_delta = 0.0;
    double specular_delta = 0.0;
    std::string failure;
};

static std::string hrhex(HRESULT hr) {
    std::ostringstream o; o << "0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr); return o.str();
}
static void fail(Result& r, const std::string& msg) { if(r.failure.empty()) r.failure = msg; }

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h,m,w,l); }
static HWND make_window() {
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName=L"RevolutionP8WarpHarness";
    RegisterClassW(&wc);
    return CreateWindowExW(0,wc.lpszClassName,L"P8",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,hi,nullptr);
}

static bool read_file(const wchar_t* path, std::string& out) {
    std::ifstream f(path, std::ios::binary); if(!f) return false;
    std::ostringstream s; s << f.rdbuf(); out=s.str(); return true;
}

static HRESULT compile_shader(const std::string& src, const char* entry, const char* target, ComPtr<ID3DBlob>& code, std::string& err) {
    ComPtr<ID3DBlob> e;
    HRESULT hr=D3DCompile(src.data(),src.size(),"terrain_pbr.hlsl",nullptr,nullptr,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&e);
    if(FAILED(hr) && e) err.assign((const char*)e->GetBufferPointer(),e->GetBufferSize());
    return hr;
}

static Mat4 identity() {
    Mat4 a{}; for(int i=0;i<4;i++) a.m[i][i]=1.0f; return a;
}

static std::array<float,4> uv_coeff(int tileWidth, int ox, int oy, int atlasH=2048) {
    float span=float(tileWidth*64);
    return {2048.0f/span, float(atlasH)/span, -float(ox)/span, -float(oy)/span};
}

static std::array<std::array<float,2>,4> atlas_uv(int tileWidth, int ox, int oy, int atlasH=2048) {
    float span=float(tileWidth*64);
    float u0=float(ox)/2048.0f, v0=float(oy)/float(atlasH);
    float u1=float(ox+span)/2048.0f, v1=float(oy+span)/float(atlasH);
    return {{{u0,v0},{u1,v0},{u1,v1},{u0,v1}}};
}

static uint32_t argb(uint8_t a,uint8_t r,uint8_t g,uint8_t b) { return (uint32_t(a)<<24)|(uint32_t(r)<<16)|(uint32_t(g)<<8)|uint32_t(b); }

static HRESULT make_texture(IDirect3DDevice9* dev, int kind, bool B, float overrideScalar, bool useOverride, ComPtr<IDirect3DTexture9>& out) {
    HRESULT hr=dev->CreateTexture(TEX_W,TEX_H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&out,nullptr); if(FAILED(hr)) return hr;
    D3DLOCKED_RECT lr{}; hr=out->LockRect(0,&lr,nullptr,0); if(FAILED(hr)) return hr;
    for(UINT y=0;y<TEX_H;y++) {
        auto* row=(uint32_t*)((uint8_t*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<TEX_W;x++) {
            float fx=float(x)/(TEX_W-1), fy=float(y)/(TEX_H-1); uint8_t r=0,g=0,b=0;
            if(kind==0) {
                if(B) { r=uint8_t(45+65*fx); g=uint8_t(115+100*fy); b=uint8_t(50+35*(1-fx)); }
                else  { r=uint8_t(130+100*fx); g=uint8_t(65+80*fy); b=uint8_t(30+35*(1-fy)); }
            } else if(kind==1) {
                float nx=B?0.55f:-0.45f, ny=B?-0.20f:0.30f, nz=std::sqrt(std::max(0.001f,1.0f-nx*nx-ny*ny));
                if(useOverride) { nx=0; ny=0; nz=1; }
                r=uint8_t(std::clamp(nx*0.5f+0.5f,0.0f,1.0f)*255); g=uint8_t(std::clamp(ny*0.5f+0.5f,0.0f,1.0f)*255); b=uint8_t(std::clamp(nz*0.5f+0.5f,0.0f,1.0f)*255);
            } else {
                float v;
                if(kind==2) v=B?0.50f:0.90f;
                else if(kind==3) v=B?0.78f:0.18f;
                else v=B?0.25f:0.95f;
                if(useOverride) v=overrideScalar;
                uint8_t q=uint8_t(std::clamp(v,0.0f,1.0f)*255); r=g=b=q;
            }
            row[x]=argb(255,r,g,b);
        }
    }
    out->UnlockRect(0); return S_OK;
}

static HRESULT make_sentinel_texture(IDirect3DDevice9* dev, ComPtr<IDirect3DTexture9>& out) {
    HRESULT hr=dev->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&out,nullptr); if(FAILED(hr)) return hr;
    D3DLOCKED_RECT lr{}; if(FAILED(hr=out->LockRect(0,&lr,nullptr,0))) return hr;
    for(int y=0;y<4;y++) { auto* row=(uint32_t*)((uint8_t*)lr.pBits+y*lr.Pitch); for(int x=0;x<4;x++) row[x]=0xFF112233u; }
    out->UnlockRect(0); return S_OK;
}

struct Pipeline {
    ComPtr<IDirect3DVertexDeclaration9> decl;
    ComPtr<IDirect3DVertexShader9> vs;
    ComPtr<IDirect3DPixelShader9> ps;
    ComPtr<IDirect3DVertexDeclaration9> sentinelDecl;
    ComPtr<IDirect3DVertexShader9> sentinelVS;
    ComPtr<IDirect3DPixelShader9> sentinelPS;
};

static HRESULT make_pipeline(IDirect3DDevice9* dev, const std::string& hlsl, Pipeline& p, std::string& err) {
    ComPtr<ID3DBlob> vb,pb; HRESULT hr=compile_shader(hlsl,"VSMain","vs_3_0",vb,err); if(FAILED(hr)) return hr;
    hr=compile_shader(hlsl,"PSMain","ps_3_0",pb,err); if(FAILED(hr)) return hr;
    if(FAILED(hr=dev->CreateVertexShader((DWORD*)vb->GetBufferPointer(),&p.vs))) return hr;
    if(FAILED(hr=dev->CreatePixelShader((DWORD*)pb->GetBufferPointer(),&p.ps))) return hr;
    D3DVERTEXELEMENT9 de[]={
      {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
      {0,12,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
      {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
      {0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
      D3DDECL_END()
    };
    if(FAILED(hr=dev->CreateVertexDeclaration(de,&p.decl))) return hr;
    const char* svs=
      "struct I{float3 p:POSITION0;float4 c:COLOR0;float2 a:TEXCOORD0;float2 b:TEXCOORD1;};"
      "struct O{float4 p:POSITION0;float4 c:COLOR0;};O main(I i){O o;o.p=float4(i.p,1);o.c=i.c;return o;}";
    const char* sps="float4 main(float4 c:COLOR0):COLOR0{return float4(.1,.2,.3,1);}";
    ComPtr<ID3DBlob> sv,sf; std::string e2;
    hr=compile_shader(svs,"main","vs_3_0",sv,e2); if(FAILED(hr)){err=e2;return hr;}
    hr=compile_shader(sps,"main","ps_3_0",sf,e2); if(FAILED(hr)){err=e2;return hr;}
    if(FAILED(hr=dev->CreateVertexShader((DWORD*)sv->GetBufferPointer(),&p.sentinelVS))) return hr;
    if(FAILED(hr=dev->CreatePixelShader((DWORD*)sf->GetBufferPointer(),&p.sentinelPS))) return hr;
    if(FAILED(hr=dev->CreateVertexDeclaration(de,&p.sentinelDecl))) return hr;
    return S_OK;
}

struct Scene {
    ComPtr<IDirect3DVertexBuffer9> vb;
    ComPtr<IDirect3DIndexBuffer9> ib;
    ComPtr<IDirect3DSurface9> rt;
    ComPtr<IDirect3DSurface9> sys;
};

static HRESULT make_scene(IDirect3DDevice9* dev, Scene& s) {
    auto au=atlas_uv(4,256,128), bu=atlas_uv(8,512,256);
    Vertex v[4]={
      {-0.95f,-0.95f,2.0f,argb(0,220,220,220),au[0][0],au[0][1],bu[0][0],bu[0][1]},
      { 0.95f,-0.95f,2.0f,argb(255,220,220,220),au[1][0],au[1][1],bu[1][0],bu[1][1]},
      { 0.95f, 0.95f,2.0f,argb(255,245,235,220),au[2][0],au[2][1],bu[2][0],bu[2][1]},
      {-0.95f, 0.95f,2.0f,argb(0,245,235,220),au[3][0],au[3][1],bu[3][0],bu[3][1]}
    };
    uint16_t idx[6]={0,1,2,0,2,3}; HRESULT hr;
    if(FAILED(hr=dev->CreateVertexBuffer(sizeof(v),0,0,D3DPOOL_MANAGED,&s.vb,nullptr))) return hr;
    void* p=nullptr; if(FAILED(hr=s.vb->Lock(0,0,&p,0))) return hr; memcpy(p,v,sizeof(v)); s.vb->Unlock();
    if(FAILED(hr=dev->CreateIndexBuffer(sizeof(idx),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&s.ib,nullptr))) return hr;
    if(FAILED(hr=s.ib->Lock(0,0,&p,0))) return hr; memcpy(p,idx,sizeof(idx)); s.ib->Unlock();
    if(FAILED(hr=dev->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,TRUE,&s.rt,nullptr))) return hr;
    if(FAILED(hr=dev->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&s.sys,nullptr))) return hr;
    return S_OK;
}

struct RenderStats { double mean=0,left=0,right=0; uint64_t hash=1469598103934665603ull; std::vector<uint32_t> px; };
static RenderStats analyze_surface(IDirect3DSurface9* sys) {
    RenderStats st; D3DLOCKED_RECT lr{}; if(FAILED(sys->LockRect(&lr,nullptr,D3DLOCK_READONLY))) return st;
    st.px.resize(W*H); double sum=0,sl=0,sr=0; uint64_t nl=0,nr=0;
    for(UINT y=0;y<H;y++) { const uint32_t* row=(const uint32_t*)((const uint8_t*)lr.pBits+y*lr.Pitch); for(UINT x=0;x<W;x++) {
        uint32_t p=row[x]; st.px[y*W+x]=p; st.hash^=p; st.hash*=1099511628211ull;
        double r=((p>>16)&255)/255.0,g=((p>>8)&255)/255.0,b=(p&255)/255.0,l=.299*r+.587*g+.114*b; sum+=l;
        if(x<W/4){sl+=l;nl++;} if(x>=3*W/4){sr+=l;nr++;}
    }}
    sys->UnlockRect(); st.mean=sum/(W*H); st.left=sl/std::max<uint64_t>(1,nl); st.right=sr/std::max<uint64_t>(1,nr); return st;
}

static double image_delta(const RenderStats& a, const RenderStats& b) {
    if(a.px.size()!=b.px.size()||a.px.empty()) return 0; double s=0;
    for(size_t i=0;i<a.px.size();i++) { for(int sh: {16,8,0}) s+=std::abs(int((a.px[i]>>sh)&255)-int((b.px[i]>>sh)&255))/255.0; }
    return s/(a.px.size()*3.0);
}

static bool save_bmp(const wchar_t* path, const RenderStats& st) {
    if(st.px.size()!=W*H) return false; BITMAPFILEHEADER bf{}; BITMAPINFOHEADER bi{};
    bi.biSize=sizeof(bi); bi.biWidth=W; bi.biHeight=-int(H); bi.biPlanes=1; bi.biBitCount=32; bi.biCompression=BI_RGB; bi.biSizeImage=W*H*4;
    bf.bfType=0x4D42; bf.bfOffBits=sizeof(bf)+sizeof(bi); bf.bfSize=bf.bfOffBits+bi.biSizeImage;
    std::ofstream f(path,std::ios::binary); if(!f)return false; f.write((char*)&bf,sizeof(bf));f.write((char*)&bi,sizeof(bi));f.write((char*)st.px.data(),st.px.size()*4); return true;
}

struct TextureSet { std::array<ComPtr<IDirect3DTexture9>,10> t; };
static HRESULT make_set(IDirect3DDevice9* dev, int overrideKind, float overrideScalar, TextureSet& set) {
    for(int side=0;side<2;side++) for(int k=0;k<5;k++) {
        bool ov=(k==overrideKind); HRESULT hr=make_texture(dev,k,side!=0,overrideScalar,ov,set.t[side*5+k]); if(FAILED(hr)) return hr;
    }
    return S_OK;
}

static bool verify_state(IDirect3DDevice9* dev, Pipeline& p, IDirect3DBaseTexture9* sentinel) {
    ComPtr<IDirect3DPixelShader9> ps; ComPtr<IDirect3DVertexShader9> vs; ComPtr<IDirect3DVertexDeclaration9> de;
    if(FAILED(dev->GetPixelShader(&ps))||ps.Get()!=p.sentinelPS.Get()) return false;
    if(FAILED(dev->GetVertexShader(&vs))||vs.Get()!=p.sentinelVS.Get()) return false;
    if(FAILED(dev->GetVertexDeclaration(&de))||de.Get()!=p.sentinelDecl.Get()) return false;
    for(UINT i=0;i<10;i++) { ComPtr<IDirect3DBaseTexture9> t; if(FAILED(dev->GetTexture(i,&t))||t.Get()!=sentinel) return false;
        DWORD q=0; if(FAILED(dev->GetSamplerState(i,D3DSAMP_ADDRESSU,&q))||q!=D3DTADDRESS_WRAP) return false;
        if(FAILED(dev->GetSamplerState(i,D3DSAMP_ADDRESSV,&q))||q!=D3DTADDRESS_MIRROR) return false;
        if(FAILED(dev->GetSamplerState(i,D3DSAMP_MAGFILTER,&q))||q!=D3DTEXF_POINT) return false;
        if(FAILED(dev->GetSamplerState(i,D3DSAMP_MINFILTER,&q))||q!=D3DTEXF_POINT) return false;
        if(FAILED(dev->GetSamplerState(i,D3DSAMP_MIPFILTER,&q))||q!=D3DTEXF_NONE) return false;
    }
    return true;
}

static HRESULT render_exact_phase8(IDirect3DDevice9* dev, Pipeline& p, Scene& s, const TextureSet& set, IDirect3DTexture9* sentinel, RenderStats& stats, bool& restored) {
    HRESULT hr; restored=false;
    dev->SetRenderTarget(0,s.rt.Get()); dev->SetDepthStencilSurface(nullptr); dev->SetRenderState(D3DRS_ZENABLE,FALSE); dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE); dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetStreamSource(0,s.vb.Get(),0,sizeof(Vertex)); dev->SetIndices(s.ib.Get());
    dev->SetVertexDeclaration(p.sentinelDecl.Get()); dev->SetVertexShader(p.sentinelVS.Get()); dev->SetPixelShader(p.sentinelPS.Get());
    for(UINT i=0;i<10;i++) { dev->SetTexture(i,sentinel); dev->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP); dev->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_MIRROR); dev->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT); dev->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT); dev->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE); }
    std::array<ComPtr<IDirect3DBaseTexture9>,10> oldTex; for(UINT i=0;i<10;i++){ if(FAILED(hr=dev->GetTexture(i,&oldTex[i]))) return hr; }
    ComPtr<IDirect3DStateBlock9> psb,vsb; if(FAILED(hr=dev->CreateStateBlock(D3DSBT_PIXELSTATE,&psb))) return hr; if(FAILED(hr=dev->CreateStateBlock(D3DSBT_VERTEXSTATE,&vsb))) return hr;
    if(FAILED(hr=psb->Capture())) return hr; if(FAILED(hr=vsb->Capture())) return hr;
    Mat4 wm=identity(), vm=identity(), pm=identity(); vm.m[2][2]=-1.0f; pm.m[2][2]=-0.25f;
    auto UA=uv_coeff(4,256,128), UB=uv_coeff(8,512,256);
    float pc[16]={UA[0],UA[1],UA[2],UA[3],UB[0],UB[1],UB[2],UB[3],-0.4f,-0.3f,-0.8660254f,2.15f,0.72f,0,0,0};
    if(SUCCEEDED(hr)) hr=dev->SetVertexDeclaration(p.decl.Get());
    if(SUCCEEDED(hr)) hr=dev->SetVertexShader(p.vs.Get());
    if(SUCCEEDED(hr)) hr=dev->SetPixelShader(p.ps.Get());
    if(SUCCEEDED(hr)) hr=dev->SetVertexShaderConstantF(0,&wm.m[0][0],4);
    if(SUCCEEDED(hr)) hr=dev->SetVertexShaderConstantF(4,&vm.m[0][0],4);
    if(SUCCEEDED(hr)) hr=dev->SetVertexShaderConstantF(8,&pm.m[0][0],4);
    if(SUCCEEDED(hr)) hr=dev->SetPixelShaderConstantF(0,pc,4);
    for(UINT i=0;i<10&&SUCCEEDED(hr);i++) hr=dev->SetTexture(i,set.t[i].Get());
    for(UINT i=0;i<10&&SUCCEEDED(hr);i++) {
        if(FAILED(hr=dev->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP))) break;
        if(FAILED(hr=dev->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP))) break;
        if(FAILED(hr=dev->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR))) break;
        if(FAILED(hr=dev->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_LINEAR))) break;
        if(FAILED(hr=dev->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR))) break;
    }
    if(SUCCEEDED(hr)) { dev->Clear(0,nullptr,D3DCLEAR_TARGET,0xFF000000,1,0); if(SUCCEEDED(hr=dev->BeginScene())) { hr=dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2); dev->EndScene(); } }
    vsb->Apply(); psb->Apply(); for(UINT i=0;i<10;i++) dev->SetTexture(i,oldTex[i].Get());
    restored=verify_state(dev,p,sentinel);
    if(FAILED(hr)) return hr;
    if(FAILED(hr=dev->GetRenderTargetData(s.rt.Get(),s.sys.Get()))) return hr;
    stats=analyze_surface(s.sys.Get()); return S_OK;
}

static bool write_json(const Result& r) {
    std::ofstream f("phase8_warp_report.json"); if(!f) return false;
    f << std::boolalpha << "{\n"
      << "  \"ok\": " << r.ok << ",\n"
      << "  \"d3d9on12\": " << r.on12 << ",\n"
      << "  \"same_d3d12_device\": " << r.same_d3d12_device << ",\n"
      << "  \"state_restore\": " << r.state_restore << ",\n"
      << "  \"blend_response\": " << r.blend_response << ",\n"
      << "  \"normal_response\": " << r.normal_response << ",\n"
      << "  \"ao_response\": " << r.ao_response << ",\n"
      << "  \"roughness_response\": " << r.roughness_response << ",\n"
      << "  \"specular_response\": " << r.specular_response << ",\n"
      << "  \"base_mean\": " << r.base_mean << ",\n"
      << "  \"left_mean\": " << r.left_mean << ",\n"
      << "  \"right_mean\": " << r.right_mean << ",\n"
      << "  \"normal_delta\": " << r.normal_delta << ",\n"
      << "  \"ao_delta\": " << r.ao_delta << ",\n"
      << "  \"roughness_delta\": " << r.roughness_delta << ",\n"
      << "  \"specular_delta\": " << r.specular_delta << ",\n"
      << "  \"failure\": \"";
    for(char c:r.failure){ if(c=='\\'||c=='\"')f<<'\\'; if(c=='\n')f<<"\\n"; else f<<c; }
    f << "\"\n}\n"; return true;
}

int wmain() {
    Result R; HWND hwnd=make_window(); if(!hwnd){fail(R,"CreateWindow failed");write_json(R);return 2;}
    ComPtr<IDXGIFactory4> factory; HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory)); if(FAILED(hr)){fail(R,"CreateDXGIFactory1 "+hrhex(hr));write_json(R);return 3;}
    ComPtr<IDXGIAdapter> warp; hr=factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)); if(FAILED(hr)){fail(R,"EnumWarpAdapter "+hrhex(hr));write_json(R);return 4;}
    ComPtr<ID3D12Device> d12; hr=D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12)); if(FAILED(hr)){fail(R,"D3D12CreateDevice(WARP) "+hrhex(hr));write_json(R);return 5;}
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT; ComPtr<ID3D12CommandQueue> queue; hr=d12->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)); if(FAILED(hr)){fail(R,"CreateCommandQueue "+hrhex(hr));write_json(R);return 6;}
    HMODULE d3d9dll=LoadLibraryW(L"d3d9.dll"); if(!d3d9dll){fail(R,"LoadLibrary(d3d9.dll)");write_json(R);return 7;}
    auto create9on12=(PFN_Direct3DCreate9On12)GetProcAddress(d3d9dll,"Direct3DCreate9On12"); if(!create9on12){fail(R,"Direct3DCreate9On12 export missing");write_json(R);return 8;}
    D3D9ON12_ARGS a{}; a.Enable9On12=TRUE; a.pD3D12Device=d12.Get(); a.ppD3D12Queues[0]=queue.Get(); a.NumQueues=1; a.NodeMask=0;
    ComPtr<IDirect3D9> d9; d9.Attach(create9on12(D3D_SDK_VERSION,&a,1)); if(!d9){fail(R,"Direct3DCreate9On12 returned null");write_json(R);return 9;}
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow=hwnd; pp.BackBufferWidth=W; pp.BackBufferHeight=H; pp.BackBufferFormat=D3DFMT_X8R8G8B8; pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9> dev; hr=d9->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&dev);
    if(FAILED(hr)) hr=d9->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&dev);
    if(FAILED(hr)){fail(R,"IDirect3D9::CreateDevice "+hrhex(hr));write_json(R);return 10;}
    ComPtr<IDirect3DDevice9On12> on12; hr=dev.As(&on12); R.on12=SUCCEEDED(hr)&&on12;
    if(!R.on12){fail(R,"QueryInterface(IDirect3DDevice9On12) failed "+hrhex(hr));write_json(R);return 11;}
    ComPtr<ID3D12Device> returned12; hr=on12->GetD3D12Device(IID_PPV_ARGS(&returned12)); if(FAILED(hr)){fail(R,"GetD3D12Device "+hrhex(hr));write_json(R);return 12;}
    ComPtr<IUnknown> i1,i2; d12.As(&i1); returned12.As(&i2); R.same_d3d12_device=(i1.Get()==i2.Get());
    if(!R.same_d3d12_device){fail(R,"9On12 did not return supplied WARP D3D12 device");write_json(R);return 13;}
    D3DCAPS9 caps{}; dev->GetDeviceCaps(&caps); if(caps.PixelShaderVersion < D3DPS_VERSION(3,0) || caps.VertexShaderVersion < D3DVS_VERSION(3,0)){fail(R,"SM3 unavailable");write_json(R);return 14;}
    std::string hlsl; if(!read_file(L"terrain_pbr.hlsl",hlsl)){fail(R,"terrain_pbr.hlsl not found");write_json(R);return 15;}
    Pipeline pipe; std::string cerrs; hr=make_pipeline(dev.Get(),hlsl,pipe,cerrs); if(FAILED(hr)){fail(R,"shader/pipeline build "+hrhex(hr)+" "+cerrs);write_json(R);return 16;}
    Scene scene; hr=make_scene(dev.Get(),scene); if(FAILED(hr)){fail(R,"scene build "+hrhex(hr));write_json(R);return 17;}
    ComPtr<IDirect3DTexture9> sentinel; hr=make_sentinel_texture(dev.Get(),sentinel); if(FAILED(hr)){fail(R,"sentinel texture "+hrhex(hr));write_json(R);return 18;}
    TextureSet base, neutralN, whiteAO, roughHi, specOff;
    if(FAILED(hr=make_set(dev.Get(),-1,0,base)) || FAILED(hr=make_set(dev.Get(),1,0,neutralN)) || FAILED(hr=make_set(dev.Get(),2,1.0f,whiteAO)) || FAILED(hr=make_set(dev.Get(),3,0.95f,roughHi)) || FAILED(hr=make_set(dev.Get(),4,0.0f,specOff))) { fail(R,"texture set build "+hrhex(hr));write_json(R);return 19; }
    RenderStats s0,sN,sAO,sR,sS; bool restored=false, rr=true;
    if(FAILED(hr=render_exact_phase8(dev.Get(),pipe,scene,base,sentinel.Get(),s0,restored))){fail(R,"base render "+hrhex(hr));rr=false;} R.state_restore=restored;
    if(rr&&FAILED(hr=render_exact_phase8(dev.Get(),pipe,scene,neutralN,sentinel.Get(),sN,restored))){fail(R,"normal render "+hrhex(hr));rr=false;} R.state_restore=R.state_restore&&restored;
    if(rr&&FAILED(hr=render_exact_phase8(dev.Get(),pipe,scene,whiteAO,sentinel.Get(),sAO,restored))){fail(R,"AO render "+hrhex(hr));rr=false;} R.state_restore=R.state_restore&&restored;
    if(rr&&FAILED(hr=render_exact_phase8(dev.Get(),pipe,scene,roughHi,sentinel.Get(),sR,restored))){fail(R,"roughness render "+hrhex(hr));rr=false;} R.state_restore=R.state_restore&&restored;
    if(rr&&FAILED(hr=render_exact_phase8(dev.Get(),pipe,scene,specOff,sentinel.Get(),sS,restored))){fail(R,"specular render "+hrhex(hr));rr=false;} R.state_restore=R.state_restore&&restored;
    if(rr) {
        R.base_mean=s0.mean;R.left_mean=s0.left;R.right_mean=s0.right;
        R.normal_delta=image_delta(s0,sN);R.ao_delta=image_delta(s0,sAO);R.roughness_delta=image_delta(s0,sR);R.specular_delta=image_delta(s0,sS);
        R.blend_response=std::abs(s0.left-s0.right)>0.015;
        R.normal_response=R.normal_delta>0.0025; R.ao_response=R.ao_delta>0.01; R.roughness_response=R.roughness_delta>0.0005; R.specular_response=R.specular_delta>0.0005;
        save_bmp(L"phase8_warp_output.bmp",s0);
    }
    R.ok=rr&&R.on12&&R.same_d3d12_device&&R.state_restore&&R.blend_response&&R.normal_response&&R.ao_response&&R.roughness_response&&R.specular_response;
    if(!R.ok&&R.failure.empty()) fail(R,"one or more functional gates failed");
    write_json(R);
    std::cout << "D3D9On12 WARP=" << R.on12 << " sameD3D12=" << R.same_d3d12_device << " stateRestore=" << R.state_restore << "\n";
    std::cout << "deltas N="<<R.normal_delta<<" AO="<<R.ao_delta<<" R="<<R.roughness_delta<<" S="<<R.specular_delta<<" blend="<<std::abs(R.left_mean-R.right_mean)<<"\n";
    if(!R.failure.empty()) std::cout << "failure: " << R.failure << "\n";
    DestroyWindow(hwnd); return R.ok?0:20;
}
