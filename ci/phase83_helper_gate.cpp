#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

extern "C" int capture83();
extern "C" void restore83();
static int call_capture(void* p){int r=0;__asm{mov ecx,p call capture83 mov r,eax}return r;}
static void call_restore(void* p){__asm{mov ecx,p call restore83}}

template<class T>static void R(T*&p){if(p){p->Release();p=nullptr;}}
static LRESULT CALLBACK W(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
#define MAXQ 2
struct A9{BOOL Enable9On12;IUnknown*pD3D12Device;IUnknown*ppD3D12Queues[MAXQ];UINT NumQueues;UINT NodeMask;};
typedef IDirect3D9*(WINAPI*PFN9)(UINT,A9*,UINT);
static const GUID IID9={0xe7fda234,0xb589,0x4049,{0x94,0x0d,0x88,0x78,0x97,0x75,0x31,0xc8}};
struct V{float x,y,z;DWORD c;float u0,v0,u1,v1;};
static const char*VS="struct I{float3 p:POSITION0;float4 c:COLOR0;float2 a:TEXCOORD0;float2 b:TEXCOORD1;};float4 M(I i):POSITION0{return float4(i.p,1);}";
static const char*PS="float4 M():COLOR0{return float4(.4,.5,.6,1);}";
static const D3DSAMPLERSTATETYPE ST[5]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MAGFILTER,D3DSAMP_MINFILTER,D3DSAMP_MIPFILTER};
struct Expected{IDirect3DBaseTexture9*t[10];IDirect3DVertexDeclaration9*decl;IDirect3DVertexShader9*vs;IDirect3DPixelShader9*ps;DWORD s[10][5];float vc[48],pc[16];};
static HRESULT comp(const char*s,const char*e,const char*t,ID3DBlob**o){ID3DBlob*x=nullptr;HRESULT h=D3DCompile(s,strlen(s),nullptr,nullptr,nullptr,e,t,0,0,o,&x);if(x){std::printf("%s\n",(char*)x->GetBufferPointer());x->Release();}return h;}
static void save(IDirect3DDevice9*d,Expected&w){memset(&w,0,sizeof(w));for(int i=0;i<10;i++)d->GetTexture(i,&w.t[i]);d->GetVertexDeclaration(&w.decl);d->GetVertexShader(&w.vs);d->GetPixelShader(&w.ps);for(int i=0;i<10;i++)for(int j=0;j<5;j++)d->GetSamplerState(i,ST[j],&w.s[i][j]);d->GetVertexShaderConstantF(0,w.vc,12);d->GetPixelShaderConstantF(0,w.pc,4);}
static void frees(Expected&w){for(auto&p:w.t)R(p);R(w.decl);R(w.vs);R(w.ps);}
static int check(IDirect3DDevice9*d,Expected&w){int n=0;auto bad=[&](const char*x){++n;std::printf("MISMATCH: %s\n",x);};IDirect3DVertexDeclaration9*decl=nullptr;d->GetVertexDeclaration(&decl);if(decl!=w.decl)bad("declaration");R(decl);IDirect3DVertexShader9*vs=nullptr;d->GetVertexShader(&vs);if(vs!=w.vs)bad("VS");R(vs);IDirect3DPixelShader9*ps=nullptr;d->GetPixelShader(&ps);if(ps!=w.ps)bad("PS");R(ps);for(int i=0;i<10;i++){IDirect3DBaseTexture9*t=nullptr;d->GetTexture(i,&t);if(t!=w.t[i])bad("texture");R(t);for(int j=0;j<5;j++){DWORD x=0;d->GetSamplerState(i,ST[j],&x);if(x!=w.s[i][j])bad("sampler");}}float vc[48],pc[16];d->GetVertexShaderConstantF(0,vc,12);d->GetPixelShaderConstantF(0,pc,4);if(memcmp(vc,w.vc,sizeof(vc)))bad("VS constants");if(memcmp(pc,w.pc,sizeof(pc)))bad("PS constants");return n;}
int main(){
 std::printf("Phase8.3 exact helper gate x86\n");
 void* low=VirtualAlloc((void*)0x00AEC000,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(low!=(void*)0x00AEC000){std::printf("VirtualAlloc low globals failed %p err=%lu\n",low,GetLastError());return 2;}
 WNDCLASSW wc={};wc.lpfnWndProc=W;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"p83";RegisterClassW(&wc);HWND h=CreateWindowW(wc.lpszClassName,L"p83",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
 HMODULE m=LoadLibraryW(L"d3d9.dll");auto fn=(PFN9)GetProcAddress(m,"Direct3DCreate9On12");A9 a={};a.Enable9On12=TRUE;IDirect3D9*d3=fn?fn(D3D_SDK_VERSION,&a,1):nullptr;if(!d3)return 2;D3DPRESENT_PARAMETERS pp={};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=h;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_UNKNOWN;IDirect3DDevice9*d=nullptr;if(FAILED(d3->CreateDevice(0,D3DDEVTYPE_HAL,h,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d)))return 2;IUnknown*q=nullptr;if(FAILED(d->QueryInterface(IID9,(void**)&q)))return 2;R(q);std::printf("d3d9on12=true\n");*(IDirect3DDevice9**)0x00AEC004=d;
 ID3DBlob*bv=nullptr,*bp=nullptr;if(FAILED(comp(VS,"M","vs_3_0",&bv))||FAILED(comp(PS,"M","ps_3_0",&bp)))return 2;IDirect3DVertexShader9*pvs=nullptr;IDirect3DPixelShader9*pps=nullptr;d->CreateVertexShader((DWORD*)bv->GetBufferPointer(),&pvs);d->CreatePixelShader((DWORD*)bp->GetBufferPointer(),&pps);R(bv);R(bp);D3DVERTEXELEMENT9 e[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};IDirect3DVertexDeclaration9*pdecl=nullptr;d->CreateVertexDeclaration(e,&pdecl);
 V vv[3]={{-.5f,-.5f,.5f,0xffffffff,0,1,0,1},{0,.5f,.5f,0xffffffff,.5f,0,.5f,0},{.5f,-.5f,.5f,0xffffffff,1,1,1,1}};WORD ii[3]={0,1,2};IDirect3DVertexBuffer9*vb=nullptr;IDirect3DIndexBuffer9*ib=nullptr;d->CreateVertexBuffer(sizeof(vv),0,0,D3DPOOL_DEFAULT,&vb,nullptr);d->CreateIndexBuffer(sizeof(ii),0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr);void*p=nullptr;vb->Lock(0,0,&p,0);memcpy(p,vv,sizeof(vv));vb->Unlock();ib->Lock(0,0,&p,0);memcpy(p,ii,sizeof(ii));ib->Unlock();IDirect3DTexture9*t0[10]={},*t1[10]={};for(int i=0;i<10;i++){d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t0[i],nullptr);d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t1[i],nullptr);}
 d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX2);d->SetStreamSource(0,vb,0,sizeof(V));d->SetIndices(ib);for(int i=0;i<10;i++){d->SetTexture(i,t0[i]);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);}float vc0[48],pc0[16];for(int i=0;i<48;i++)vc0[i]=100+i*.25f;for(int i=0;i<16;i++)pc0[i]=-10-i*.5f;d->SetVertexShaderConstantF(0,vc0,12);d->SetPixelShaderConstantF(0,pc0,4);Expected want={};save(d,want);
 IDirect3DBaseTexture9*snap[10]={};int ok=call_capture(snap);if(!ok){std::printf("FAIL: machine capture helper returned 0\n");return 1;}d->SetVertexDeclaration(pdecl);d->SetVertexShader(pvs);d->SetPixelShader(pps);float vc1[48]={};for(int i=0;i<48;i++)vc1[i]=i+1;float pc1[16]={1,1,0,0,1,1,0,0,.2f,.4f,-.7f,1,.7f,0,0,0};d->SetVertexShaderConstantF(0,vc1,12);d->SetPixelShaderConstantF(0,pc1,4);for(int i=0;i<10;i++){d->SetTexture(i,t1[i]);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);}d->BeginScene();d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1);d->EndScene();call_restore(snap);int mism=check(d,want);std::printf("mismatch_count=%d\n",mism);
 frees(want);for(int i=0;i<10;i++){R(t0[i]);R(t1[i]);}R(vb);R(ib);R(pdecl);R(pvs);R(pps);*(IDirect3DDevice9**)0x00AEC004=nullptr;R(d);R(d3);FreeLibrary(m);DestroyWindow(h);VirtualFree(low,0,MEM_RELEASE);if(mism){std::printf("FAIL: exact machine helper leaked state\n");return 1;}std::printf("PASS: exact Phase8.3 machine helper restores all touched D3D9 state\n");return 0;
}
