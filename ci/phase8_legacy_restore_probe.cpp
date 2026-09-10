#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

#define MAXQ 2
struct D3D9ON12_ARGS_LOCAL { BOOL Enable9On12; IUnknown* pD3D12Device; IUnknown* ppD3D12Queues[MAXQ]; UINT NumQueues; UINT NodeMask; };
typedef IDirect3D9* (WINAPI *PFN_Create9On12)(UINT,D3D9ON12_ARGS_LOCAL*,UINT);
static const GUID IID_9On12={0xe7fda234,0xb589,0x4049,{0x94,0x0d,0x88,0x78,0x97,0x75,0x31,0xc8}};
static LRESULT CALLBACK W(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);} 
template<class T> static void R(T*&p){if(p){p->Release();p=nullptr;}}
static int fails=0;
static void F(const char*s){++fails;std::printf("MISMATCH: %s\n",s);} 
static HRESULT C(const char*src,const char*entry,const char*target,ID3DBlob**out){ID3DBlob*e=nullptr;HRESULT h=D3DCompile(src,strlen(src),nullptr,nullptr,nullptr,entry,target,0,0,out,&e);if(e){std::printf("%s\n",(char*)e->GetBufferPointer());e->Release();}return h;}

struct V{float x,y,z;DWORD c;float u0,v0,u1,v1;};
static const char* VS="struct I{float3 p:POSITION0;float4 c:COLOR0;float2 a:TEXCOORD0;float2 b:TEXCOORD1;};float4 M(I i):POSITION0{return float4(i.p,1);}";
static const char* PS="float4 M():COLOR0{return float4(.4,.5,.6,1);}";

struct Saved {
 IDirect3DBaseTexture9* t[10];
 DWORD fvf;
 IDirect3DVertexDeclaration9* decl;
 IDirect3DVertexShader9* vs;
 IDirect3DPixelShader9* ps;
 DWORD samp[10][5];
 float vc[48],pc[16];
};
static const D3DSAMPLERSTATETYPE ST[5]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MAGFILTER,D3DSAMP_MINFILTER,D3DSAMP_MIPFILTER};

static void cap_tex(IDirect3DDevice9*d,Saved&s){memset(s.t,0,sizeof(s.t));for(int i=0;i<10;i++)d->GetTexture(i,&s.t[i]);}
static void rest_tex(IDirect3DDevice9*d,Saved&s){for(int i=0;i<10;i++){d->SetTexture(i,s.t[i]);R(s.t[i]);}}
static void cap_target(IDirect3DDevice9*d,Saved&s){memset(&s,0,sizeof(s));cap_tex(d,s);d->GetFVF(&s.fvf);d->GetVertexDeclaration(&s.decl);d->GetVertexShader(&s.vs);d->GetPixelShader(&s.ps);for(int i=0;i<10;i++)for(int j=0;j<5;j++)d->GetSamplerState(i,ST[j],&s.samp[i][j]);d->GetVertexShaderConstantF(0,s.vc,12);d->GetPixelShaderConstantF(0,s.pc,4);}
static void rest_target(IDirect3DDevice9*d,Saved&s){rest_tex(d,s);for(int i=0;i<10;i++)for(int j=0;j<5;j++)d->SetSamplerState(i,ST[j],s.samp[i][j]);d->SetVertexShaderConstantF(0,s.vc,12);d->SetPixelShaderConstantF(0,s.pc,4);d->SetVertexShader(s.vs);d->SetPixelShader(s.ps);if(s.fvf)d->SetFVF(s.fvf);else d->SetVertexDeclaration(s.decl);R(s.decl);R(s.vs);R(s.ps);}

static void baseline(IDirect3DDevice9*d,IDirect3DVertexBuffer9*vb,IDirect3DIndexBuffer9*ib,IDirect3DTexture9**t){
 d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX2);
 d->SetStreamSource(0,vb,0,sizeof(V));d->SetIndices(ib);
 for(int i=0;i<10;i++){d->SetTexture(i,t[i]);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);} 
 float vc[48],pc[16];for(int i=0;i<48;i++)vc[i]=100+i*.25f;for(int i=0;i<16;i++)pc[i]=-10-i*.5f;d->SetVertexShaderConstantF(0,vc,12);d->SetPixelShaderConstantF(0,pc,4);
}
static void mutate_p8(IDirect3DDevice9*d,IDirect3DVertexDeclaration9*decl,IDirect3DVertexShader9*vs,IDirect3DPixelShader9*ps,IDirect3DTexture9**t){
 d->SetVertexDeclaration(decl);d->SetVertexShader(vs);d->SetPixelShader(ps);
 float vc[48]={};for(int i=0;i<48;i++)vc[i]=i+1;float pc[16]={1,1,0,0,1,1,0,0,.2f,.4f,-.7f,1,.7f,0,0,0};d->SetVertexShaderConstantF(0,vc,12);d->SetPixelShaderConstantF(0,pc,4);
 for(int i=0;i<10;i++){d->SetTexture(i,t[i]);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);} 
 d->BeginScene();d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1);d->EndScene();
}
static int compare(IDirect3DDevice9*d,Saved&want,bool verbose){int before=fails;DWORD fvf=0;d->GetFVF(&fvf);if(fvf!=want.fvf)F("FVF");IDirect3DVertexDeclaration9*decl=nullptr;d->GetVertexDeclaration(&decl);if(decl!=want.decl)F("declaration");R(decl);IDirect3DVertexShader9*vs=nullptr;d->GetVertexShader(&vs);if(vs!=want.vs)F("VS");R(vs);IDirect3DPixelShader9*ps=nullptr;d->GetPixelShader(&ps);if(ps!=want.ps)F("PS");R(ps);for(int i=0;i<10;i++){IDirect3DBaseTexture9*t=nullptr;d->GetTexture(i,&t);if(t!=want.t[i]){char b[32];sprintf_s(b,"texture %d",i);F(b);}R(t);for(int j=0;j<5;j++){DWORD x=0;d->GetSamplerState(i,ST[j],&x);if(x!=want.samp[i][j]){char b[48];sprintf_s(b,"sampler %d/%d",i,(int)ST[j]);F(b);}}}float vc[48],pc[16];d->GetVertexShaderConstantF(0,vc,12);d->GetPixelShaderConstantF(0,pc,4);if(memcmp(vc,want.vc,sizeof(vc)))F("VS constants");if(memcmp(pc,want.pc,sizeof(pc)))F("PS constants");int n=fails-before;if(verbose)std::printf("mismatch_count=%d\n",n);return n;}
static void release_saved(Saved&s){for(auto&p:s.t)R(p);R(s.decl);R(s.vs);R(s.ps);}

int main(){std::printf("Phase8 legacy restore probe %s\n",sizeof(void*)==4?"x86":"x64");WNDCLASSW wc={};wc.lpfnWndProc=W;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"p8p";RegisterClassW(&wc);HWND h=CreateWindowW(wc.lpszClassName,L"p",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);HMODULE m=LoadLibraryW(L"d3d9.dll");auto fn=(PFN_Create9On12)GetProcAddress(m,"Direct3DCreate9On12");D3D9ON12_ARGS_LOCAL a={};a.Enable9On12=TRUE;IDirect3D9*d3=fn?fn(D3D_SDK_VERSION,&a,1):nullptr;if(!d3)return 2;D3DPRESENT_PARAMETERS pp={};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=h;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_UNKNOWN;IDirect3DDevice9*d=nullptr;HRESULT hr=d3->CreateDevice(0,D3DDEVTYPE_HAL,h,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);if(FAILED(hr)||!d)return 2;IUnknown*q=nullptr;if(FAILED(d->QueryInterface(IID_9On12,(void**)&q)))return 2;R(q);std::printf("d3d9on12=true\n");
 ID3DBlob*bv=nullptr,*bp=nullptr;if(FAILED(C(VS,"M","vs_3_0",&bv))||FAILED(C(PS,"M","ps_3_0",&bp)))return 2;IDirect3DVertexShader9*vs=nullptr;IDirect3DPixelShader9*ps=nullptr;d->CreateVertexShader((DWORD*)bv->GetBufferPointer(),&vs);d->CreatePixelShader((DWORD*)bp->GetBufferPointer(),&ps);R(bv);R(bp);D3DVERTEXELEMENT9 e[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};IDirect3DVertexDeclaration9*decl=nullptr;d->CreateVertexDeclaration(e,&decl);
 V vv[3]={{-.5f,-.5f,.5f,0xffffffff,0,1,0,1},{0,.5f,.5f,0xffffffff,.5f,0,.5f,0},{.5f,-.5f,.5f,0xffffffff,1,1,1,1}};WORD ii[3]={0,1,2};IDirect3DVertexBuffer9*vb=nullptr;IDirect3DIndexBuffer9*ib=nullptr;d->CreateVertexBuffer(sizeof(vv),0,0,D3DPOOL_DEFAULT,&vb,nullptr);d->CreateIndexBuffer(sizeof(ii),0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr);void*p=nullptr;vb->Lock(0,0,&p,0);memcpy(p,vv,sizeof(vv));vb->Unlock();ib->Lock(0,0,&p,0);memcpy(p,ii,sizeof(ii));ib->Unlock();IDirect3DTexture9*t0[10]={},*t1[10]={};for(int i=0;i<10;i++){d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t0[i],nullptr);d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t1[i],nullptr);} 
 baseline(d,vb,ib,t0);Saved want={};cap_target(d,want);
 Saved legacy={};cap_tex(d,legacy);mutate_p8(d,decl,vs,ps,t1);rest_tex(d,legacy);std::printf("LEGACY_8_1:\n");int legacy_m=compare(d,want,true);release_saved(legacy);
 baseline(d,vb,ib,t0);Saved targeted={};cap_target(d,targeted);mutate_p8(d,decl,vs,ps,t1);rest_target(d,targeted);std::printf("TARGETED_RESTORE:\n");int targeted_m=compare(d,want,true);release_saved(targeted);
 release_saved(want);for(int i=0;i<10;i++){R(t0[i]);R(t1[i]);}R(vb);R(ib);R(decl);R(vs);R(ps);R(d);R(d3);if(m)FreeLibrary(m);DestroyWindow(h);
 if(legacy_m==0){std::printf("FAIL: legacy restore unexpectedly clean\n");return 1;}if(targeted_m!=0){std::printf("FAIL: targeted restore still leaks\n");return 1;}std::printf("PASS: targeted restore closes legacy leak without state block\n");return 0;}
