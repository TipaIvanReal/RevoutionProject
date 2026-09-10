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
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

using Microsoft::WRL::ComPtr;
static constexpr UINT W=256,H=256,TW=64,TH=64;
struct Vertex{float x,y,z;D3DCOLOR color;float u0,v0,u1,v1;};
static_assert(sizeof(Vertex)==32,"Generals terrain vertex stride must be 32 bytes");
struct M4{float m[4][4];};
struct Stats{double mean=0;double lr=0,lg=0,lb=0,rr=0,rg=0,rb=0;std::vector<uint32_t>px;};
struct Result{
 bool ok=false,on12=false,warp_available=false,on12_is_warp=false,state_restore=false,blend=false,normal=false,ao=false,rough=false,spec=false;
 double mean=0,blend_delta=0,dn=0,dao=0,dr=0,ds=0;std::string state_detail,failure;
};
static void fail(Result&r,const std::string&s){if(r.failure.empty())r.failure=s;}
static std::string hx(HRESULT h){std::ostringstream o;o<<"0x"<<std::hex<<std::uppercase<<(uint32_t)h;return o.str();}
static LRESULT CALLBACK wp(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static HWND window(){HINSTANCE i=GetModuleHandleW(nullptr);WNDCLASSW c{};c.lpfnWndProc=wp;c.hInstance=i;c.lpszClassName=L"RevP8Gate";RegisterClassW(&c);return CreateWindowExW(0,c.lpszClassName,L"P8",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,i,nullptr);}
static bool read_text(const wchar_t*p,std::string&o){std::ifstream f(p,std::ios::binary);if(!f)return false;std::ostringstream s;s<<f.rdbuf();o=s.str();return true;}
static M4 ident(){M4 a{};for(int i=0;i<4;i++)a.m[i][i]=1;return a;}
static uint32_t argb(uint8_t a,uint8_t r,uint8_t g,uint8_t b){return(uint32_t(a)<<24)|(uint32_t(r)<<16)|(uint32_t(g)<<8)|b;}
static std::array<float,4> coeff(int tilew,int ox,int oy,int ah=2048){float s=float(tilew*64);return{2048.0f/s,float(ah)/s,-float(ox)/s,-float(oy)/s};}
static std::array<std::array<float,2>,4> auv(int tilew,int ox,int oy,int ah=2048){float s=float(tilew*64),u0=float(ox)/2048.f,v0=float(oy)/ah,u1=float(ox+s)/2048.f,v1=float(oy+s)/ah;return{{{u0,v0},{u1,v0},{u1,v1},{u0,v1}}};}
static HRESULT comp(const std::string&s,const char*e,const char*t,ComPtr<ID3DBlob>&b,std::string&err){ComPtr<ID3DBlob>x;HRESULT h=D3DCompile(s.data(),s.size(),"terrain_pbr.hlsl",nullptr,nullptr,e,t,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&x);if(FAILED(h)&&x)err.assign((char*)x->GetBufferPointer(),x->GetBufferSize());return h;}

struct Pipe{ComPtr<IDirect3DVertexDeclaration9>d,sd;ComPtr<IDirect3DVertexShader9>v,sv;ComPtr<IDirect3DPixelShader9>p,sp;};
static HRESULT make_pipe(IDirect3DDevice9*d,const std::string&hlsl,Pipe&p,std::string&e){
 ComPtr<ID3DBlob>v,b;HRESULT h=comp(hlsl,"VSMain","vs_3_0",v,e);if(FAILED(h))return h;if(FAILED(h=comp(hlsl,"PSMain","ps_3_0",b,e)))return h;
 if(FAILED(h=d->CreateVertexShader((DWORD*)v->GetBufferPointer(),&p.v)))return h;if(FAILED(h=d->CreatePixelShader((DWORD*)b->GetBufferPointer(),&p.p)))return h;
 D3DVERTEXELEMENT9 de[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};
 if(FAILED(h=d->CreateVertexDeclaration(de,&p.d)))return h;
 const char*sv="struct I{float3 p:POSITION0;float4 c:COLOR0;float2 a:TEXCOORD0;float2 b:TEXCOORD1;};struct O{float4 p:POSITION0;};O main(I i){O o;o.p=float4(i.p,1);return o;}";
 const char*sp="float4 main():COLOR0{return float4(.13,.27,.41,1);}";ComPtr<ID3DBlob>x,y;std::string z;
 if(FAILED(h=comp(sv,"main","vs_3_0",x,z)))return h;if(FAILED(h=comp(sp,"main","ps_3_0",y,z)))return h;
 if(FAILED(h=d->CreateVertexShader((DWORD*)x->GetBufferPointer(),&p.sv)))return h;if(FAILED(h=d->CreatePixelShader((DWORD*)y->GetBufferPointer(),&p.sp)))return h;return d->CreateVertexDeclaration(de,&p.sd);
}

static HRESULT tex(IDirect3DDevice9*d,int kind,bool B,int over,float val,ComPtr<IDirect3DTexture9>&o){HRESULT h=d->CreateTexture(TW,TH,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&o,nullptr);if(FAILED(h))return h;D3DLOCKED_RECT l{};if(FAILED(h=o->LockRect(0,&l,nullptr,0)))return h;
 for(UINT y=0;y<TH;y++){auto*r=(uint32_t*)((uint8_t*)l.pBits+y*l.Pitch);for(UINT x=0;x<TW;x++){float fx=float(x)/(TW-1),fy=float(y)/(TH-1);uint8_t R,G,Bb;
  if(kind==0){if(B){R=uint8_t(30+55*fx);G=uint8_t(125+105*fy);Bb=uint8_t(35+40*(1-fx));}else{R=uint8_t(145+95*fx);G=uint8_t(50+70*fy);Bb=uint8_t(25+35*(1-fy));}}
  else if(kind==1){float nx=B?.58f:-.52f,ny=B?-.24f:.35f,nz=sqrtf(std::max(.001f,1-nx*nx-ny*ny));if(over==1){nx=0;ny=0;nz=1;}R=uint8_t(std::clamp(nx*.5f+.5f,0.f,1.f)*255);G=uint8_t(std::clamp(ny*.5f+.5f,0.f,1.f)*255);Bb=uint8_t(std::clamp(nz*.5f+.5f,0.f,1.f)*255);}
  else{float q=kind==2?(B?.42f:.86f):kind==3?(B?.88f:.12f):(B?.18f:1.f);if(over==kind)q=val;uint8_t z=uint8_t(std::clamp(q,0.f,1.f)*255);R=G=Bb=z;}r[x]=argb(255,R,G,Bb);}}
 o->UnlockRect(0);return S_OK;}
struct Set{std::array<ComPtr<IDirect3DTexture9>,10>t;};
static HRESULT settex(IDirect3DDevice9*d,int over,float val,Set&s){for(int side=0;side<2;side++)for(int k=0;k<5;k++){HRESULT h=tex(d,k,side!=0,over,val,s.t[side*5+k]);if(FAILED(h))return h;}return S_OK;}
static HRESULT sentinel_tex(IDirect3DDevice9*d,ComPtr<IDirect3DTexture9>&o){HRESULT h=d->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&o,nullptr);if(FAILED(h))return h;D3DLOCKED_RECT l{};if(FAILED(h=o->LockRect(0,&l,nullptr,0)))return h;for(int y=0;y<4;y++){auto*r=(uint32_t*)((uint8_t*)l.pBits+y*l.Pitch);for(int x=0;x<4;x++)r[x]=0xFF17395Bu;}o->UnlockRect(0);return S_OK;}

struct Scene{ComPtr<IDirect3DVertexBuffer9>vb;ComPtr<IDirect3DIndexBuffer9>ib;ComPtr<IDirect3DSurface9>rt,sys;};
static HRESULT make_scene(IDirect3DDevice9*d,Scene&s){auto a=auv(4,256,128),b=auv(8,512,256);Vertex v[4]={{-.92f,-.92f,.45f,argb(0,230,224,216),a[0][0],a[0][1],b[0][0],b[0][1]},{.92f,-.92f,.45f,argb(255,230,224,216),a[1][0],a[1][1],b[1][0],b[1][1]},{.92f,.92f,.45f,argb(255,246,240,228),a[2][0],a[2][1],b[2][0],b[2][1]},{-.92f,.92f,.45f,argb(0,246,240,228),a[3][0],a[3][1],b[3][0],b[3][1]}};uint16_t q[6]={0,1,2,0,2,3};HRESULT h;void*p;
 if(FAILED(h=d->CreateVertexBuffer(sizeof(v),0,0,D3DPOOL_MANAGED,&s.vb,nullptr)))return h;if(FAILED(h=s.vb->Lock(0,0,&p,0)))return h;memcpy(p,v,sizeof(v));s.vb->Unlock();
 if(FAILED(h=d->CreateIndexBuffer(sizeof(q),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&s.ib,nullptr)))return h;if(FAILED(h=s.ib->Lock(0,0,&p,0)))return h;memcpy(p,q,sizeof(q));s.ib->Unlock();
 if(FAILED(h=d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&s.rt,nullptr)))return h;return d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&s.sys,nullptr);
}
static Stats stats(IDirect3DSurface9*s){Stats a;D3DLOCKED_RECT l{};if(FAILED(s->LockRect(&l,nullptr,D3DLOCK_READONLY)))return a;a.px.resize(W*H);double sum=0;uint64_t nl=0,nr=0;for(UINT y=0;y<H;y++){auto*r=(const uint32_t*)((uint8_t*)l.pBits+y*l.Pitch);for(UINT x=0;x<W;x++){uint32_t p=r[x];a.px[y*W+x]=p;double R=((p>>16)&255)/255.,G=((p>>8)&255)/255.,B=(p&255)/255.;sum+=.299*R+.587*G+.114*B;if(x<W/4){a.lr+=R;a.lg+=G;a.lb+=B;nl++;}if(x>=3*W/4){a.rr+=R;a.rg+=G;a.rb+=B;nr++;}}}s->UnlockRect();a.mean=sum/(W*H);if(nl){a.lr/=nl;a.lg/=nl;a.lb/=nl;}if(nr){a.rr/=nr;a.rg/=nr;a.rb/=nr;}return a;}
static double delta(const Stats&a,const Stats&b){if(a.px.size()!=b.px.size()||a.px.empty())return 0;double z=0;for(size_t i=0;i<a.px.size();i++)for(int sh:{16,8,0})z+=std::abs(int((a.px[i]>>sh)&255)-int((b.px[i]>>sh)&255))/255.;return z/(a.px.size()*3.);}
static bool bmp(const Stats&a){if(a.px.size()!=W*H)return false;BITMAPFILEHEADER f{};BITMAPINFOHEADER i{};i.biSize=sizeof(i);i.biWidth=W;i.biHeight=-int(H);i.biPlanes=1;i.biBitCount=32;i.biCompression=BI_RGB;i.biSizeImage=W*H*4;f.bfType=0x4D42;f.bfOffBits=sizeof(f)+sizeof(i);f.bfSize=f.bfOffBits+i.biSizeImage;std::ofstream o("phase8_d3d9on12_output.bmp",std::ios::binary);if(!o)return false;o.write((char*)&f,sizeof(f));o.write((char*)&i,sizeof(i));o.write((char*)a.px.data(),a.px.size()*4);return true;}

struct MutState{ComPtr<IDirect3DVertexDeclaration9>decl;ComPtr<IDirect3DVertexShader9>vs;ComPtr<IDirect3DPixelShader9>ps;std::array<ComPtr<IDirect3DBaseTexture9>,10>t;DWORD ss[10][5]{};float vc[48]{},pc[16]{};};
static HRESULT capture_mut(IDirect3DDevice9*d,MutState&s){HRESULT h;if(FAILED(h=d->GetVertexDeclaration(&s.decl)))return h;if(FAILED(h=d->GetVertexShader(&s.vs)))return h;if(FAILED(h=d->GetPixelShader(&s.ps)))return h;D3DSAMPLERSTATETYPE n[5]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER};for(UINT i=0;i<10;i++){if(FAILED(h=d->GetTexture(i,&s.t[i])))return h;for(int j=0;j<5;j++)if(FAILED(h=d->GetSamplerState(i,n[j],&s.ss[i][j])))return h;}if(FAILED(h=d->GetVertexShaderConstantF(0,s.vc,12)))return h;return d->GetPixelShaderConstantF(0,s.pc,4);}
static bool verify_mut(IDirect3DDevice9*d,const MutState&s,std::string&why){MutState q;HRESULT h=capture_mut(d,q);if(FAILED(h)){why="capture after restore "+hx(h);return false;}if(q.decl.Get()!=s.decl.Get()){why="vertex declaration";return false;}if(q.vs.Get()!=s.vs.Get()){why="vertex shader";return false;}if(q.ps.Get()!=s.ps.Get()){why="pixel shader";return false;}if(memcmp(q.vc,s.vc,sizeof(s.vc))){why="VS constants c0-c11";return false;}if(memcmp(q.pc,s.pc,sizeof(s.pc))){why="PS constants c0-c3";return false;}for(int i=0;i<10;i++){if(q.t[i].Get()!=s.t[i].Get()){why="texture s"+std::to_string(i);return false;}if(memcmp(q.ss[i],s.ss[i],sizeof(s.ss[i]))){why="sampler state s"+std::to_string(i);return false;}}why="ok";return true;}
static void sentinel_state(IDirect3DDevice9*d,Pipe&p,IDirect3DTexture9*t){d->SetVertexDeclaration(p.sd.Get());d->SetVertexShader(p.sv.Get());d->SetPixelShader(p.sp.Get());for(UINT i=0;i<10;i++){d->SetTexture(i,t);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_MIRROR);d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);}float v[48],q[16];for(int i=0;i<48;i++)v[i]=100.f+i*.125f;for(int i=0;i<16;i++)q[i]=-50.f+i*.25f;d->SetVertexShaderConstantF(0,v,12);d->SetPixelShaderConstantF(0,q,4);}
static HRESULT setup_scene_state(IDirect3DDevice9*d,Scene&s){D3DVIEWPORT9 vp{0,0,W,H,0,1};HRESULT h;if(FAILED(h=d->SetRenderTarget(0,s.rt.Get())))return h;d->SetDepthStencilSurface(nullptr);if(FAILED(h=d->SetViewport(&vp)))return h;if(FAILED(h=d->SetRenderState(D3DRS_ZENABLE,FALSE)))return h;if(FAILED(h=d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE)))return h;if(FAILED(h=d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE)))return h;if(FAILED(h=d->SetStreamSource(0,s.vb.Get(),0,sizeof(Vertex))))return h;return d->SetIndices(s.ib.Get());}

// Mirrors the candidate contract: Phase 8 changes only declaration/shaders/constants/textures/samplers.
// Surrounding RT/viewport/geometry state is prepared before capture and is never touched by the material pass.
static HRESULT render(IDirect3DDevice9*d,Pipe&p,Scene&s,const Set&ts,IDirect3DTexture9*sent,Stats&o,bool&restored,std::string&why){restored=false;HRESULT h=setup_scene_state(d,s);if(FAILED(h))return h;sentinel_state(d,p,sent);MutState before;if(FAILED(h=capture_mut(d,before)))return h;
 std::array<ComPtr<IDirect3DBaseTexture9>,10>oldTex;for(UINT i=0;i<10;i++)oldTex[i]=before.t[i];ComPtr<IDirect3DStateBlock9>psb,vsb;if(FAILED(h=d->CreateStateBlock(D3DSBT_PIXELSTATE,&psb)))return h;if(FAILED(h=d->CreateStateBlock(D3DSBT_VERTEXSTATE,&vsb)))return h;if(FAILED(h=psb->Capture()))return h;if(FAILED(h=vsb->Capture()))return h;
 M4 m=ident();auto A=coeff(4,256,128),B=coeff(8,512,256);float pc[16]={A[0],A[1],A[2],A[3],B[0],B[1],B[2],B[3],-.28f,-.18f,-.9434f,3.2f,.62f,0,0,0};
 if(FAILED(h=d->SetVertexDeclaration(p.d.Get())))goto restore;if(FAILED(h=d->SetVertexShader(p.v.Get())))goto restore;if(FAILED(h=d->SetPixelShader(p.p.Get())))goto restore;if(FAILED(h=d->SetVertexShaderConstantF(0,&m.m[0][0],4)))goto restore;if(FAILED(h=d->SetVertexShaderConstantF(4,&m.m[0][0],4)))goto restore;if(FAILED(h=d->SetVertexShaderConstantF(8,&m.m[0][0],4)))goto restore;if(FAILED(h=d->SetPixelShaderConstantF(0,pc,4)))goto restore;
 for(UINT i=0;i<10;i++){if(FAILED(h=d->SetTexture(i,ts.t[i].Get())))goto restore;if(FAILED(h=d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP)))goto restore;if(FAILED(h=d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP)))goto restore;if(FAILED(h=d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_LINEAR)))goto restore;if(FAILED(h=d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR)))goto restore;if(FAILED(h=d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE)))goto restore;}
 if(FAILED(h=d->Clear(0,nullptr,D3DCLEAR_TARGET,0xFF000000,1,0)))goto restore;if(SUCCEEDED(h=d->BeginScene())){h=d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2);d->EndScene();}
 if(SUCCEEDED(h)){HRESULT r=d->GetRenderTargetData(s.rt.Get(),s.sys.Get());if(FAILED(r))h=r;else o=stats(s.sys.Get());}
restore:
 {HRESULT hv=vsb->Apply(),hp=psb->Apply();for(UINT i=0;i<10;i++)d->SetTexture(i,oldTex[i].Get());if(SUCCEEDED(h)&&FAILED(hv))h=hv;if(SUCCEEDED(h)&&FAILED(hp))h=hp;}
 restored=verify_mut(d,before,why);return h;
}

static void json(const Result&r){std::ofstream f("phase8_d3d9on12_report.json");f<<std::boolalpha<<"{\n  \"ok\": "<<r.ok<<",\n  \"d3d9on12\": "<<r.on12<<",\n  \"warp_available\": "<<r.warp_available<<",\n  \"on12_is_warp\": "<<r.on12_is_warp<<",\n  \"state_restore\": "<<r.state_restore<<",\n  \"state_detail\": \""<<r.state_detail<<"\",\n  \"blend_response\": "<<r.blend<<",\n  \"normal_response\": "<<r.normal<<",\n  \"ao_response\": "<<r.ao<<",\n  \"roughness_response\": "<<r.rough<<",\n  \"specular_response\": "<<r.spec<<",\n  \"base_mean\": "<<r.mean<<",\n  \"blend_rgb_delta\": "<<r.blend_delta<<",\n  \"normal_delta\": "<<r.dn<<",\n  \"ao_delta\": "<<r.dao<<",\n  \"roughness_delta\": "<<r.dr<<",\n  \"specular_delta\": "<<r.ds<<",\n  \"failure\": \"";for(char c:r.failure){if(c=='\\'||c=='\"')f<<'\\';if(c=='\n')f<<"\\n";else f<<c;}f<<"\"\n}\n";}
int wmain(){Result R;HWND h=window();if(!h){fail(R,"window");json(R);return 2;}ComPtr<IDXGIFactory4>fac;ComPtr<IDXGIAdapter>wa;ComPtr<ID3D12Device>wd;if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&fac)))&&SUCCEEDED(fac->EnumWarpAdapter(IID_PPV_ARGS(&wa)))&&SUCCEEDED(D3D12CreateDevice(wa.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&wd))))R.warp_available=true;
 HMODULE m=LoadLibraryW(L"d3d9.dll");if(!m){fail(R,"d3d9.dll");json(R);return 3;}auto c=(PFN_Direct3DCreate9On12)GetProcAddress(m,"Direct3DCreate9On12");if(!c){fail(R,"Direct3DCreate9On12 export missing");json(R);return 4;}D3D9ON12_ARGS a{};a.Enable9On12=TRUE;a.pD3D12Device=nullptr;a.NumQueues=0;a.NodeMask=0;ComPtr<IDirect3D9>d9;d9.Attach(c(D3D_SDK_VERSION,&a,1));if(!d9){fail(R,"Direct3DCreate9On12 null");json(R);return 5;}D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=h;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;ComPtr<IDirect3DDevice9>d;HRESULT x=d9->CreateDevice(0,D3DDEVTYPE_HAL,h,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&d);if(FAILED(x))x=d9->CreateDevice(0,D3DDEVTYPE_HAL,h,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&d);if(FAILED(x)){fail(R,"CreateDevice "+hx(x));json(R);return 6;}ComPtr<IDirect3DDevice9On12>o;x=d.As(&o);R.on12=SUCCEEDED(x)&&o;if(!R.on12){fail(R,"IDirect3DDevice9On12 QI "+hx(x));json(R);return 7;}ComPtr<ID3D12Device>d12;x=o->GetD3D12Device(IID_PPV_ARGS(&d12));if(FAILED(x)){fail(R,"GetD3D12Device "+hx(x));json(R);return 8;}if(wd){LUID a1=d12->GetAdapterLuid(),a2=wd->GetAdapterLuid();R.on12_is_warp=(a1.HighPart==a2.HighPart&&a1.LowPart==a2.LowPart);}D3DCAPS9 caps{};d->GetDeviceCaps(&caps);if(caps.PixelShaderVersion<D3DPS_VERSION(3,0)||caps.VertexShaderVersion<D3DVS_VERSION(3,0)){fail(R,"SM3 unavailable");json(R);return 9;}std::string hs;if(!read_text(L"terrain_pbr.hlsl",hs)){fail(R,"terrain_pbr.hlsl missing");json(R);return 10;}Pipe p;std::string er;if(FAILED(x=make_pipe(d.Get(),hs,p,er))){fail(R,"pipeline "+hx(x)+" "+er);json(R);return 11;}Scene s;if(FAILED(x=make_scene(d.Get(),s))){fail(R,"scene "+hx(x));json(R);return 12;}ComPtr<IDirect3DTexture9>st;if(FAILED(x=sentinel_tex(d.Get(),st))){fail(R,"sentinel "+hx(x));json(R);return 13;}Set b,n,ao,r,sp;if(FAILED(settex(d.Get(),-1,0,b))||FAILED(settex(d.Get(),1,0,n))||FAILED(settex(d.Get(),2,1,ao))||FAILED(settex(d.Get(),3,.96f,r))||FAILED(settex(d.Get(),4,0,sp))){fail(R,"textures");json(R);return 14;}
 Stats s0,sn,sa,sr,ss;bool rr=true,z=false;std::string why;auto run=[&](const Set&t,Stats&q){HRESULT e=render(d.Get(),p,s,t,st.Get(),q,z,why);if(!z){R.state_detail=why;fail(R,"state restore mismatch: "+why);rr=false;}if(FAILED(e)){fail(R,"render "+hx(e));rr=false;}R.state_restore=z;};run(b,s0);if(rr)run(n,sn);if(rr)run(ao,sa);if(rr)run(r,sr);if(rr)run(sp,ss);
 if(rr){R.mean=s0.mean;R.blend_delta=(std::abs(s0.lr-s0.rr)+std::abs(s0.lg-s0.rg)+std::abs(s0.lb-s0.rb))/3.;R.dn=delta(s0,sn);R.dao=delta(s0,sa);R.dr=delta(s0,sr);R.ds=delta(s0,ss);R.blend=R.blend_delta>.025;R.normal=R.dn>.002;R.ao=R.dao>.008;R.rough=R.dr>.0002;R.spec=R.ds>.0002;bmp(s0);}R.ok=rr&&R.on12&&R.state_restore&&R.blend&&R.normal&&R.ao&&R.rough&&R.spec;if(!R.ok&&R.failure.empty())fail(R,"material response gate failed");json(R);DestroyWindow(h);return R.ok?0:1;}
