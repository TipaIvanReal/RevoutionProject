.intel_syntax noprefix
.equ G_NORMAL, 0x00AEA180
.equ G_AO,     0x00AEA184
.equ G_ROUGH,  0x00AEA188
.equ G_SPEC,   0x00AEA18C
.equ D9DEV,    0x00AEC004
.equ CACHE_RESET, 0x00AEA090
.equ IID_TEX9, 0x00AEB988

.global neutral_init
.global convert_or_default
.global make_one
.global fb_normal
.global fb_ao
.global fb_rough
.global fb_spec

neutral_init:
    push ebp
    push ebx
    push esi
    push edi
    mov eax, CACHE_RESET
    call eax
    mov eax, dword ptr [G_NORMAL]
    test eax,eax
    jz ni_ao
    mov ecx,dword ptr [eax]
    push eax
    call dword ptr [ecx+8]
    mov dword ptr [G_NORMAL],0
ni_ao:
    mov eax, dword ptr [G_AO]
    test eax,eax
    jz ni_rough
    mov ecx,dword ptr [eax]
    push eax
    call dword ptr [ecx+8]
    mov dword ptr [G_AO],0
ni_rough:
    mov eax, dword ptr [G_ROUGH]
    test eax,eax
    jz ni_spec
    mov ecx,dword ptr [eax]
    push eax
    call dword ptr [ecx+8]
    mov dword ptr [G_ROUGH],0
ni_spec:
    mov eax, dword ptr [G_SPEC]
    test eax,eax
    jz ni_create
    mov ecx,dword ptr [eax]
    push eax
    call dword ptr [ecx+8]
    mov dword ptr [G_SPEC],0
ni_create:
    mov esi, dword ptr [D9DEV]
    test esi,esi
    jz ni_done
    push G_NORMAL
    push 0xFF8080FF
    push esi
    call make_one
    add esp,12
    push G_AO
    push 0xFFFFFFFF
    push esi
    call make_one
    add esp,12
    push G_ROUGH
    push 0xFFA6A6A6
    push esi
    call make_one
    add esp,12
    push G_SPEC
    push 0xFF000000
    push esi
    call make_one
    add esp,12
ni_done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

make_one:
    push ebp
    mov ebp,esp
    push ebx
    push esi
    push edi
    sub esp,12
    mov esi,dword ptr [ebp+8]
    mov edi,dword ptr [ebp+16]
    mov dword ptr [edi],0
    mov dword ptr [ebp-16],0
    mov dword ptr [ebp-24],0
    mov dword ptr [ebp-20],0
    test esi,esi
    jz mo_fail
    mov ebx,dword ptr [esi]
    push 0
    lea eax,[ebp-16]
    push eax
    push 1
    push 21
    push 0
    push 1
    push 1
    push 1
    push esi
    call dword ptr [ebx+0x5C]
    test eax,eax
    js mo_fail
    mov esi,dword ptr [ebp-16]
    test esi,esi
    jz mo_fail
    mov ebx,dword ptr [esi]
    push 0
    push 0
    lea eax,[ebp-24]
    push eax
    push 0
    push esi
    call dword ptr [ebx+0x4C]
    test eax,eax
    js mo_release
    mov eax,dword ptr [ebp-20]
    test eax,eax
    jz mo_unlock_release
    mov edx,dword ptr [ebp+12]
    mov dword ptr [eax],edx
    mov ebx,dword ptr [esi]
    push 0
    push esi
    call dword ptr [ebx+0x50]
    test eax,eax
    js mo_release
    mov dword ptr [edi],esi
    xor eax,eax
    jmp mo_done
mo_unlock_release:
    mov ebx,dword ptr [esi]
    push 0
    push esi
    call dword ptr [ebx+0x50]
mo_release:
    mov ebx,dword ptr [esi]
    push esi
    call dword ptr [ebx+8]
mo_fail:
    mov dword ptr [edi],0
    mov eax,0x80004005
mo_done:
    add esp,12
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

convert_or_default:
    push ebp
    mov ebp,esp
    push ebx
    push esi
    push edi
    sub esp,4
    mov esi,dword ptr [ebp+8]
    mov ebx,dword ptr [ebp+12]
    mov ecx,dword ptr [ebp+16]
    mov edx,dword ptr [ebp+20]
    mov edi,dword ptr [ebp+24]
    mov dword ptr [esi+ecx],ebx
    mov dword ptr [esi+edx],0
    mov dword ptr [ebp-16],0
    test ebx,ebx
    jz cod_default
    mov eax,dword ptr [ebx]
    lea ecx,[ebp-16]
    push ecx
    push IID_TEX9
    push ebx
    call dword ptr [eax]
    test eax,eax
    js cod_default
    mov eax,dword ptr [ebp-16]
    test eax,eax
    jz cod_default
    mov edx,dword ptr [ebp+20]
    mov dword ptr [esi+edx],eax
    mov eax,1
    jmp cod_done
cod_default:
    mov eax,dword ptr [edi]
    test eax,eax
    jz cod_fail
    mov edx,dword ptr [ebp+20]
    mov dword ptr [esi+edx],eax
    mov ecx,dword ptr [eax]
    push eax
    call dword ptr [ecx+4]
    mov eax,1
    jmp cod_done
cod_fail:
    xor eax,eax
cod_done:
    add esp,4
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

fb_normal:
    push G_NORMAL
    push 0x1C
    push 0x08
    mov eax,dword ptr [esp+16]
    push eax
    push esi
    call convert_or_default
    add esp,20
    ret
fb_ao:
    push G_AO
    push 0x20
    push 0x0C
    mov eax,dword ptr [esp+16]
    push eax
    push esi
    call convert_or_default
    add esp,20
    ret
fb_rough:
    push G_ROUGH
    push 0x24
    push 0x10
    mov eax,dword ptr [esp+16]
    push eax
    push esi
    call convert_or_default
    add esp,20
    ret
fb_spec:
    push G_SPEC
    push 0x28
    push 0x14
    mov eax,dword ptr [esp+16]
    push eax
    push esi
    call convert_or_default
    add esp,20
    ret
