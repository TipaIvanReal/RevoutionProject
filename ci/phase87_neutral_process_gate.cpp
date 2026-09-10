#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

extern "C" HRESULT __cdecl make_one_c(IDirect3DDevice9*, DWORD, IDirect3DTexture9**);
extern "C" int __cdecl convert_or_default_c(unsigned char*, void*, DWORD, DWORD, IDirect3DTexture9**);

static int g_fail=0;
static void fail(const char* s){ ++g_fail; std::printf("FAIL: %s\n",s); }
template<class T> static void rel(T*&p){ if(p){ p->Release(); p=nullptr; } }
static LRESULT CALLBACK W(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}

#pragma section(".p8bss",read,write)
__declspec(allocate(".p8bss")) volatile unsigned char gP8LowImageMap[0x700000] = {};

static IDirect3DTexture9*& G(DWORD va){ return *(IDirect3DTexture9**)va; }

static DWORD read_texel(IDirect3DTexture9* t){
    if(!t){ fail("read_texel null"); return 0; }
    D3DLOCKED_RECT lr={};
    HRESULT hr=t->LockRect(0,&lr,nullptr,D3DLOCK_READONLY);
    if(FAILED(hr)||!lr.pBits){ fail("LockRect default texture"); return 0; }
    DWORD c=*(DWORD*)lr.pBits;
    t->UnlockRect(0);
    return c;
}

struct FakeTex8 { void** vt; IDirect3DTexture9* proxy; bool failQI; };
static HRESULT __stdcall fake_qi(void* self, REFIID iid, void** out){
    FakeTex8* f=(FakeTex8*)self;
    if(!out) return E_POINTER;
    *out=nullptr;
    if(f->failQI) return E_NOINTERFACE;
    if(iid==IID_IDirect3DTexture9 && f->proxy){ f->proxy->AddRef(); *out=f->proxy; return S_OK; }
    return E_NOINTERFACE;
}
static ULONG __stdcall fake_addref(void*){return 2;}
static ULONG __stdcall fake_release(void*){return 1;}

static bool check_default(unsigned char* entry, DWORD srcOff, DWORD outOff, IDirect3DTexture9* want, const char* tag){
    if(*(void**)(entry+srcOff)!=nullptr){ std::printf("%s source not null\n",tag); return false; }
    if(*(IDirect3DTexture9**)(entry+outOff)!=want){ std::printf("%s output mismatch\n",tag); return false; }
    return true;
}

int main(){
    std::printf("Phase 8.7 exact neutral-process helper gate x86\n");
    gP8LowImageMap[0]=0; gP8LowImageMap[sizeof(gP8LowImageMap)-1]=0;
    MEMORY_BASIC_INFORMATION mbi={};
    if(!VirtualQuery((void*)0x00AEA180,&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT){
        std::printf("exact low map missing base=%p state=0x%lx\n",GetModuleHandleW(nullptr),(unsigned long)mbi.State); return 2;
    }
    std::printf("test_image_base=%p exact_globals_mapped=true\n",GetModuleHandleW(nullptr));
    std::memcpy((void*)0x00AEB988,&IID_IDirect3DTexture9,sizeof(GUID));

    WNDCLASSW wc={}; wc.lpfnWndProc=W; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"p87"; RegisterClassW(&wc);
    HWND h=CreateWindowW(wc.lpszClassName,L"p87",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    if(!h){ fail("CreateWindow"); return 2; }
    IDirect3D9* d3=Direct3DCreate9(D3D_SDK_VERSION); if(!d3){fail("Direct3DCreate9");return 2;}
    D3DPRESENT_PARAMETERS pp={}; pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow=h; pp.BackBufferWidth=64; pp.BackBufferHeight=64; pp.BackBufferFormat=D3DFMT_UNKNOWN;
    IDirect3DDevice9* dev=nullptr;
    HRESULT hr=d3->CreateDevice(0,D3DDEVTYPE_HAL,h,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr)) hr=d3->CreateDevice(0,D3DDEVTYPE_REF,h,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr)||!dev){ std::printf("CreateDevice hr=0x%08lx\n",(unsigned long)hr); return 2; }

    struct Def { DWORD addr,color,srcOff,outOff; const char* name; } defs[]={
        {0x00AEA180,0xFF8080FF,0x08,0x1C,"normal"},
        {0x00AEA184,0xFFFFFFFF,0x0C,0x20,"ao"},
        {0x00AEA188,0xFFA6A6A6,0x10,0x24,"rough"},
        {0x00AEA18C,0xFF000000,0x14,0x28,"spec"},
    };
    for(auto& d:defs){
        G(d.addr)=nullptr;
        hr=make_one_c(dev,d.color,&G(d.addr));
        std::printf("make_one %-6s hr=0x%08lx ptr=%p texel=0x%08lx\n",d.name,(unsigned long)hr,G(d.addr),(unsigned long)read_texel(G(d.addr)));
        if(FAILED(hr)||!G(d.addr)) fail("make_one failed");
        else if(read_texel(G(d.addr))!=d.color) fail("default texture color mismatch");
    }

    // Missing source map must become neutral default instead of failing the material.
    for(auto& d:defs){
        unsigned char entry[0x30]={};
        int ok=convert_or_default_c(entry,nullptr,d.srcOff,d.outOff,&G(d.addr));
        std::printf("missing %-6s ok=%d src=%p out=%p default=%p\n",d.name,ok,*(void**)(entry+d.srcOff),*(void**)(entry+d.outOff),G(d.addr));
        if(!ok||!check_default(entry,d.srcOff,d.outOff,G(d.addr),d.name)) fail("missing map did not use default");
        IDirect3DTexture9* held=*(IDirect3DTexture9**)(entry+d.outOff); rel(held);
    }

    // A real wrapper-like source whose QI succeeds must win over the neutral default.
    IDirect3DTexture9* real=nullptr;
    hr=make_one_c(dev,0xFF3366CC,&real); if(FAILED(hr)||!real){fail("real texture create");return 3;}
    void* vt[3]={(void*)&fake_qi,(void*)&fake_addref,(void*)&fake_release};
    FakeTex8 good={vt,real,false};
    {
        unsigned char entry[0x30]={};
        int ok=convert_or_default_c(entry,&good,0x08,0x1C,&G(0x00AEA180));
        std::printf("real-QI normal ok=%d src=%p out=%p real=%p\n",ok,*(void**)(entry+0x08),*(void**)(entry+0x1C),real);
        if(!ok || *(void**)(entry+0x08)!=&good || *(IDirect3DTexture9**)(entry+0x1C)!=real) fail("real QI path mismatch");
        IDirect3DTexture9* held=*(IDirect3DTexture9**)(entry+0x1C); rel(held);
    }

    // Existing source whose QI fails must also fall back safely, while preserving source pointer for cache validation.
    FakeTex8 bad={vt,real,true};
    {
        unsigned char entry[0x30]={};
        int ok=convert_or_default_c(entry,&bad,0x10,0x24,&G(0x00AEA188));
        std::printf("failed-QI rough ok=%d src=%p out=%p default=%p\n",ok,*(void**)(entry+0x10),*(void**)(entry+0x24),G(0x00AEA188));
        if(!ok || *(void**)(entry+0x10)!=&bad || *(IDirect3DTexture9**)(entry+0x24)!=G(0x00AEA188)) fail("failed QI did not use rough default");
        IDirect3DTexture9* held=*(IDirect3DTexture9**)(entry+0x24); rel(held);
    }

    // No global default must fail closed.
    {
        unsigned char entry[0x30]={}; IDirect3DTexture9* none=nullptr;
        int ok=convert_or_default_c(entry,nullptr,0x14,0x28,&none);
        std::printf("no-default ok=%d\n",ok);
        if(ok) fail("missing global default did not fail closed");
    }

    rel(real);
    for(auto& d:defs){ IDirect3DTexture9* p=G(d.addr); rel(p); G(d.addr)=nullptr; }
    rel(dev); rel(d3); DestroyWindow(h);
    if(g_fail){ std::printf("TOTAL FAILURES=%d\n",g_fail); return 1; }
    std::printf("PASS: exact Phase 8.7 helper creates neutral maps and preserves real-map QI path\n");
    return 0;
}
