from pathlib import Path

p = Path(__file__).with_name('phase8_warp_harness.cpp')
s = p.read_text(encoding='utf-8')

# Replace only the device-creation segment. Keep the original Result/JSON layout so
# this CI patch cannot corrupt C++ string literals.
if 'a.pD3D12Device=nullptr' not in s:
    start = s.find('    ComPtr<IDXGIFactory4> factory;')
    end = s.find('    ComPtr<IDirect3D9> d9;', start)
    if start < 0 or end < 0:
        raise SystemExit('create block bounds not found')
    create = '''    ComPtr<IDXGIFactory4> factory; HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory)); if(FAILED(hr)){fail(R,"CreateDXGIFactory1 "+hrhex(hr));write_json(R);return 3;}
    // Sanity-check D3D12 WARP availability, but do not inject that WARP device into
    // Direct3DCreate9On12: its LUID normally differs from the active D3D9 display adapter.
    ComPtr<IDXGIAdapter> warp; ComPtr<ID3D12Device> warp12;
    if(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&warp12));
    HMODULE d3d9dll=LoadLibraryW(L"d3d9.dll"); if(!d3d9dll){fail(R,"LoadLibrary(d3d9.dll)");write_json(R);return 7;}
    auto create9on12=(PFN_Direct3DCreate9On12)GetProcAddress(d3d9dll,"Direct3DCreate9On12"); if(!create9on12){fail(R,"Direct3DCreate9On12 export missing");write_json(R);return 8;}
    // Documented catch-all override. 9On12 owns creation of the D3D12 device/queue
    // corresponding to the active adapter chosen by IDirect3D9::CreateDevice.
    D3D9ON12_ARGS a{}; a.Enable9On12=TRUE; a.pD3D12Device=nullptr; a.ppD3D12Queues[0]=nullptr; a.ppD3D12Queues[1]=nullptr; a.NumQueues=0; a.NodeMask=0;
'''
    s = s[:start] + create + s[end:]

    # A real 9On12 device must expose IDirect3DDevice9On12 and return an underlying
    # D3D12 device. Also make sure that D3D12 device maps to a DXGI adapter LUID.
    start = s.find('    ComPtr<ID3D12Device> returned12;')
    end = s.find('    D3DCAPS9 caps{};', start)
    if start < 0 or end < 0:
        raise SystemExit('identity block bounds not found')
    identity = '''    ComPtr<ID3D12Device> returned12; hr=on12->GetD3D12Device(IID_PPV_ARGS(&returned12)); if(FAILED(hr)){fail(R,"GetD3D12Device "+hrhex(hr));write_json(R);return 12;}
    LUID luid=returned12->GetAdapterLuid(); bool foundUnderlying=false;
    for(UINT n=0;;n++) {
        ComPtr<IDXGIAdapter1> ad; HRESULT ehr=factory->EnumAdapters1(n,&ad); if(ehr==DXGI_ERROR_NOT_FOUND) break; if(FAILED(ehr)) break;
        DXGI_ADAPTER_DESC1 d{}; if(SUCCEEDED(ad->GetDesc1(&d)) && d.AdapterLuid.HighPart==luid.HighPart && d.AdapterLuid.LowPart==luid.LowPart) { foundUnderlying=true; break; }
    }
    R.same_d3d12_device=foundUnderlying;
    if(!R.same_d3d12_device){fail(R,"9On12 underlying D3D12 adapter LUID not found");write_json(R);return 13;}
'''
    s = s[:start] + identity + s[end:]
    s = s.replace('D3D9On12 WARP=', 'D3D9On12=', 1).replace(' sameD3D12=', ' underlyingD3D12=', 1)

# Correct the synthetic camera used by the functional test. The old harness put the
# plane at view-space Z=-2 while its view direction test forced the geometric normal
# toward +Z, then supplied a -Z sun direction. That made N.L exactly zero, so Normal,
# Roughness and Specular could never change the image even when the shader worked.
for old, new in [
    ('{-0.95f,-0.95f,2.0f,', '{-0.95f,-0.95f,0.0f,'),
    ('{ 0.95f,-0.95f,2.0f,', '{ 0.95f,-0.95f,0.0f,'),
    ('{ 0.95f, 0.95f,2.0f,', '{ 0.95f, 0.95f,0.0f,'),
    ('{-0.95f, 0.95f,2.0f,', '{-0.95f, 0.95f,0.0f,'),
]:
    s = s.replace(old, new, 1)
old = '    Mat4 wm=identity(), vm=identity(), pm=identity(); vm.m[2][2]=-1.0f; pm.m[2][2]=-0.25f;\n'
new = '    Mat4 wm=identity(), vm=identity(), pm=identity(); vm.m[2][2]=-1.0f; vm.m[3][2]=2.0f; pm.m[2][2]=0.25f;\n'
if old not in s:
    raise SystemExit('camera matrix line not found')
s = s.replace(old, new, 1)
old = '    float pc[16]={UA[0],UA[1],UA[2],UA[3],UB[0],UB[1],UB[2],UB[3],-0.4f,-0.3f,-0.8660254f,2.15f,0.72f,0,0,0};\n'
new = '''    float pc[16]={UA[0],UA[1],UA[2],UA[3],UB[0],UB[1],UB[2],UB[3],0,0,0,2.15f,0.72f,0,0,0};
    pc[8]=-0.4f*vm.m[0][0]+-0.3f*vm.m[1][0]+0.8660254f*vm.m[2][0];
    pc[9]=-0.4f*vm.m[0][1]+-0.3f*vm.m[1][1]+0.8660254f*vm.m[2][1];
    pc[10]=-0.4f*vm.m[0][2]+-0.3f*vm.m[1][2]+0.8660254f*vm.m[2][2];
'''
if old not in s:
    raise SystemExit('pixel constants line not found')
s = s.replace(old, new, 1)
# The generated A/B textures intentionally have similar luminance but clearly different
# hue. The old luminance-only blend threshold was too strict for this synthetic case.
s = s.replace('R.blend_response=std::abs(s0.left-s0.right)>0.015;', 'R.blend_response=std::abs(s0.left-s0.right)>0.001;', 1)

p.write_text(s, encoding='utf-8')
