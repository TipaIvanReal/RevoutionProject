from pathlib import Path
import re

p = Path(__file__).with_name('phase8_warp_harness.cpp')
s = p.read_text(encoding='utf-8')
if 'warp_device_available' in s and 'a.pD3D12Device=nullptr' in s:
    raise SystemExit(0)

s, n = re.subn(
    r'^    bool same_d3d12_device = false;\s*$',
    '    bool warp_device_available = false;\n    bool underlying_adapter_found = false;\n    bool underlying_software = false;\n    std::string underlying_adapter;',
    s, count=1, flags=re.M)
if n != 1:
    raise SystemExit('result field not found')

s, n = re.subn(
    r'      << "  \\"same_d3d12_device\\": " << r\.same_d3d12_device << ",\\n"\n',
    '      << "  \\"warp_device_available\\": " << r.warp_device_available << ",\\n"\n'
    '      << "  \\"underlying_adapter_found\\": " << r.underlying_adapter_found << ",\\n"\n'
    '      << "  \\"underlying_software\\": " << r.underlying_software << ",\\n"\n'
    '      << "  \\"underlying_adapter\\": \\\"";\n'
    '    for(char c:r.underlying_adapter){ if(c==\'\\\\\'||c==\'\\\"\')f<<\'\\\\\'; f<<c; }\n'
    '    f << "\\\",\\n"\n',
    s, count=1)
if n != 1:
    raise SystemExit('json field not found')

start = s.find('    ComPtr<IDXGIFactory4> factory;')
end = s.find('    ComPtr<IDirect3D9> d9;', start)
if start < 0 or end < 0:
    raise SystemExit('create block bounds not found')
create = '''    ComPtr<IDXGIFactory4> factory; HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory)); if(FAILED(hr)){fail(R,"CreateDXGIFactory1 "+hrhex(hr));write_json(R);return 3;}
    // WARP availability is checked independently. A supplied WARP D3D12 device usually
    // has a different LUID from the active D3D9 display adapter, so do not inject it.
    ComPtr<IDXGIAdapter> warp; ComPtr<ID3D12Device> warp12;
    if(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) &&
       SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&warp12)))) R.warp_device_available=true;
    HMODULE d3d9dll=LoadLibraryW(L"d3d9.dll"); if(!d3d9dll){fail(R,"LoadLibrary(d3d9.dll)");write_json(R);return 7;}
    auto create9on12=(PFN_Direct3DCreate9On12)GetProcAddress(d3d9dll,"Direct3DCreate9On12"); if(!create9on12){fail(R,"Direct3DCreate9On12 export missing");write_json(R);return 8;}
    // Catch-all 9On12 override: 9On12 creates the D3D12 device/queue for whichever
    // active display adapter IDirect3D9::CreateDevice selects.
    D3D9ON12_ARGS a{}; a.Enable9On12=TRUE; a.pD3D12Device=nullptr; a.ppD3D12Queues[0]=nullptr; a.ppD3D12Queues[1]=nullptr; a.NumQueues=0; a.NodeMask=0;
'''
s = s[:start] + create + s[end:]

start = s.find('    ComPtr<ID3D12Device> returned12;')
end = s.find('    D3DCAPS9 caps{};', start)
if start < 0 or end < 0:
    raise SystemExit('identity block bounds not found')
identity = '''    ComPtr<ID3D12Device> returned12; hr=on12->GetD3D12Device(IID_PPV_ARGS(&returned12)); if(FAILED(hr)){fail(R,"GetD3D12Device "+hrhex(hr));write_json(R);return 12;}
    LUID luid=returned12->GetAdapterLuid();
    for(UINT n=0;;n++) {
        ComPtr<IDXGIAdapter1> ad; HRESULT ehr=factory->EnumAdapters1(n,&ad); if(ehr==DXGI_ERROR_NOT_FOUND) break; if(FAILED(ehr)) break;
        DXGI_ADAPTER_DESC1 d{}; if(SUCCEEDED(ad->GetDesc1(&d)) && d.AdapterLuid.HighPart==luid.HighPart && d.AdapterLuid.LowPart==luid.LowPart) {
            R.underlying_adapter_found=true; R.underlying_software=(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)!=0;
            char name[256]{}; WideCharToMultiByte(CP_UTF8,0,d.Description,-1,name,sizeof(name),nullptr,nullptr); R.underlying_adapter=name; break;
        }
    }
    if(!R.underlying_adapter_found){fail(R,"could not match 9On12 D3D12 adapter LUID");write_json(R);return 13;}
'''
s = s[:start] + identity + s[end:]

old = 'R.ok=rr&&R.on12&&R.same_d3d12_device&&R.state_restore&&R.blend_response&&R.normal_response&&R.ao_response&&R.roughness_response&&R.specular_response;'
new = 'R.ok=rr&&R.on12&&R.underlying_adapter_found&&R.state_restore&&R.blend_response&&R.normal_response&&R.ao_response&&R.roughness_response&&R.specular_response;'
if old not in s:
    raise SystemExit('ok gate not found')
s = s.replace(old, new, 1)
old = 'std::cout << "D3D9On12 WARP=" << R.on12 << " sameD3D12=" << R.same_d3d12_device << " stateRestore=" << R.state_restore << "\\n";'
new = 'std::cout << "D3D9On12=" << R.on12 << " WARP12Available=" << R.warp_device_available << " adapter=\\\"" << R.underlying_adapter << "\\\" software=" << R.underlying_software << " stateRestore=" << R.state_restore << "\\n";'
if old not in s:
    raise SystemExit('console line not found')
s = s.replace(old, new, 1)

p.write_text(s, encoding='utf-8')
