#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

#define MAX_Q 2
struct ARGS9ON12{BOOL Enable9On12;IUnknown* pD3D12Device;IUnknown* ppD3D12Queues[MAX_Q];UINT NumQueues;UINT NodeMask;};
typedef IDirect3D9* (WINAPI *PFN9ON12)(UINT,ARGS9ON12*,UINT);
static const GUID IID9ON12={0xe7fda234,0xb589,0x4049,{0x94,0x0d,0x88,0x78,0x97,0x75,0x31,0xc8}};
static int fails=0;
#define CK(x,msg) do{HRESULT _h=(x);if(FAILED(_h)){std::fprintf(stderr,"FAIL %s 0x%08lX\n",msg,(unsigned long)_h);++fails;}}while(0)
template<class T>static void rel(T*&p){if(p){p->Release();p=nullptr;}}
static LRESULT CALLBACK wp(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static HRESULT comp(const char*s,const char*e,const char*t,ID3DBlob**o){ID3DBlob*er=nullptr;HRESULT h=D3DCompile(s,std::strlen(s),"g",nullptr,nullptr,e,t,0,0,o,&er);if(er){if(FAILED(h))std::fprintf(stderr,"%s\n",(char*)er->GetBufferPointer());er->Release();}return h;}
struct V{float x,y,z;DWORD c;float u,v,u2,v2;};
struct Snap{
 IDirect3DVertexDeclaration9* decl{}; IDirect3DVertexShader9* vs{}; IDirect3DPixelShader9* ps{};
 IDirect3DBaseTexture9* tex[10]{}; float vc[48]{}; float pc[16]{}; DWORD samp[10][5]{};
};
static const D3DSAMPLERSTATETYPE ST[5]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MAGFILTER,D3DSAMP_MINFILTER,D3DSAMP_MIPFILTER};
static bool capture(IDirect3DDevice9*d,Snap&s){
 if(FAILED(d->GetVertexDeclaration(&s.decl)))return false; if(FAILED(d->GetVertexShader(&s.vs)))return false; if(FAILED(d->GetPixelShader(&s.ps)))return false;
 if(FAILED(d->GetVertexShaderConstantF(0,s.vc,12)))return false; if(FAILED(d->GetPixelShaderConstantF(0,s.pc,4)))return false;
 for(UINT i=0;i<10;i++){if(FAILED(d->GetTexture(i,&s.tex[i])))return false;for(int j=0;j<5;j++)if(FAILED(d->GetSamplerState(i,ST[j],&s.samp[i][j])))return false;} return true;
}
static void apply(IDirect3DDevice9*d,Snap&s){
 d->SetVertexDeclaration(s.decl); d->SetVertexShader(s.vs); d->SetPixelShader(s.ps); d->SetVertexShaderConstantF(0,s.vc,12); d->SetPixelShaderConstantF(0,s.pc,4);
 for(UINT i=0;i<10;i++){d->SetTexture(i,s.tex[i]);for(int j=0;j<5;j++)d->SetSamplerState(i,ST[j],s.samp[i][j]);}
 rel(s.decl);rel(s.vs);rel(s.ps);for(auto&t:s.tex)rel(t);
}
int main(){
 std::printf("Phase 8.3 explicit restore gate (%s)\n",sizeof(void*)==4?"x86":"x64");
 WNDCLASSW wc={};wc.lpfnWndProc=wp;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"p83";RegisterClassW(&wc);HWND hw=CreateWindowW(wc.lpszClassName,L"p83",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);if(!hw)return 2;
 HMODULE m=LoadLibraryW(L"d3d9.dll");auto f=(PFN9ON12)GetProcAddress(m,"Direct3DCreate9On12");ARGS9ON12 a={};a.Enable9On12=TRUE;IDirect3D9*d3=f?f(D3D_SDK_VERSION,&a,1):nullptr;if(!d3)return 2;
 D3DPRESENT_PARAMETERS pp={};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=hw;pp.BackBufferFormat=D3DFMT_UNKNOWN;IDirect3DDevice9*d=nullptr;HRESULT h=d3->CreateDevice(0,D3DDEVTYPE_HAL,hw,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);if(FAILED(h)||!d)return 2;
 IUnknown*o=nullptr;h=d->QueryInterface(IID9ON12,(void**)&o);if(FAILED(h)||!o)return 2;rel(o);std::puts("d3d9on12=true");
 const char*vsS="struct I{float3 p:POSITION;};float4 main(I i):POSITION{return float4(i.p,1);}";const char*psS="float4 main():COLOR{return float4(1,1,1,1);}";ID3DBlob *bv=nullptr,*bp=nullptr;CK(comp(vsS,"main","vs_3_0",&bv),"compile vs");CK(comp(psS,"main","ps_3_0",&bp),"compile ps");
 IDirect3DVertexShader9 *vsA=nullptr,*vsB=nullptr;IDirect3DPixelShader9 *psA=nullptr,*psB=nullptr;CK(d->CreateVertexShader((DWORD*)bv->GetBufferPointer(),&vsA),"vsA");CK(d->CreateVertexShader((DWORD*)bv->GetBufferPointer(),&vsB),"vsB");CK(d->CreatePixelShader((DWORD*)bp->GetBufferPointer(),&psA),"psA");CK(d->CreatePixelShader((DWORD*)bp->GetBufferPointer(),&psB),"psB");rel(bv);rel(bp);
 D3DVERTEXELEMENT9 el[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};IDirect3DVertexDeclaration9 *dcA=nullptr,*dcB=nullptr;CK(d->CreateVertexDeclaration(el,&dcA),"declA");CK(d->CreateVertexDeclaration(el,&dcB),"declB");
 IDirect3DTexture9 *ta[10]={},*tb[10]={};for(int i=0;i<10;i++){CK(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&ta[i],nullptr),"ta");CK(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&tb[i],nullptr),"tb");}
 float vcA[48],pcA[16];for(int i=0;i<48;i++)vcA[i]=100.f+i;for(int i=0;i<16;i++)pcA[i]=-50.f-i;d->SetVertexDeclaration(dcA);d->SetVertexShader(vsA);d->SetPixelShader(psA);d->SetVertexShaderConstantF(0,vcA,12);d->SetPixelShaderConstantF(0,pcA,4);
 for(int i=0;i<10;i++){d->SetTexture(i,ta[i]);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);}
 Snap s;if(!capture(d,s)){std::puts("capture failed");return 3;}
 float zc[48]={},zp[16]={};d->SetVertexDeclaration(dcB);d->SetVertexShader(vsB);d->SetPixelShader(psB);d->SetVertexShaderConstantF(0,zc,12);d->SetPixelShaderConstantF(0,zp,4);for(int i=0;i<10;i++){d->SetTexture(i,tb[i]);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_MIRROR);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_MIRROR);d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);}apply(d,s);
 IDirect3DVertexDeclaration9*dc=nullptr;IDirect3DVertexShader9*vs=nullptr;IDirect3DPixelShader9*ps=nullptr;d->GetVertexDeclaration(&dc);d->GetVertexShader(&vs);d->GetPixelShader(&ps);if(dc!=dcA){std::puts("decl mismatch");fails++;}if(vs!=vsA){std::puts("vs mismatch");fails++;}if(ps!=psA){std::puts("ps mismatch");fails++;}rel(dc);rel(vs);rel(ps);
 float vg[48]={},pg[16]={};d->GetVertexShaderConstantF(0,vg,12);d->GetPixelShaderConstantF(0,pg,4);if(std::memcmp(vg,vcA,sizeof(vg))){std::puts("VS constants mismatch");fails++;}if(std::memcmp(pg,pcA,sizeof(pg))){std::puts("PS constants mismatch");fails++;}
 DWORD want[5]={D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_NONE};for(int i=0;i<10;i++){IDirect3DBaseTexture9*t=nullptr;d->GetTexture(i,&t);if(t!=(IDirect3DBaseTexture9*)ta[i]){std::printf("texture %d mismatch\n",i);fails++;}rel(t);for(int j=0;j<5;j++){DWORD v=0;d->GetSamplerState(i,ST[j],&v);if(v!=want[j]){std::printf("sampler %d/%d mismatch\n",i,j);fails++;}}}
 for(int i=0;i<10;i++){rel(ta[i]);rel(tb[i]);}rel(dcA);rel(dcB);rel(vsA);rel(vsB);rel(psA);rel(psB);rel(d);rel(d3);FreeLibrary(m);DestroyWindow(hw);if(fails)return 1;std::puts("PASS: explicit Phase 8.3 restore identical");return 0;
}
