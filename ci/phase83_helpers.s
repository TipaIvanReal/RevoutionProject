.intel_syntax noprefix
.code32
.section .text
.global _capture83
.global capture83
.global _restore83
.global restore83

.equ DEV,      0x00AEC004
.equ G_DECL,   0x00AEC804
.equ G_VS,     0x00AEC808
.equ G_PS,     0x00AEC80C
.equ G_SAMP,   0x00AEC810
.equ G_VC,     0x00AEC8E0
.equ G_PC,     0x00AEC9A0

_capture83:
capture83:
    push ebp
    push ebx
    push esi
    push edi
    mov esi, ecx
    xor eax, eax
    mov edi, esi
    mov ecx, 10
    rep stosd
    mov dword ptr [G_DECL], 0
    mov dword ptr [G_VS], 0
    mov dword ptr [G_PS], 0
    xor edi, edi
.Lcap_tex:
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    lea ecx, [esi + edi*4]
    push ecx
    push edi
    push eax
    call dword ptr [edx + 0x100]
    test eax, eax
    js .Lcap_fail
    inc edi
    cmp edi, 10
    jne .Lcap_tex
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push G_DECL
    push eax
    call dword ptr [edx + 0x160]
    test eax, eax
    js .Lcap_fail
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push G_VS
    push eax
    call dword ptr [edx + 0x174]
    test eax, eax
    js .Lcap_fail
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push G_PS
    push eax
    call dword ptr [edx + 0x1B0]
    test eax, eax
    js .Lcap_fail
    xor ebx, ebx
.Lcap_samp_outer:
    mov ebp, ebx
    lea ebp, [ebp + ebp*4]
    shl ebp, 2
    add ebp, G_SAMP
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push ebp
    push 1
    push ebx
    push eax
    call dword ptr [edx + 0x110]
    test eax, eax
    js .Lcap_fail
    add ebp, 4
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push ebp
    push 2
    push ebx
    push eax
    call dword ptr [edx + 0x110]
    test eax, eax
    js .Lcap_fail
    add ebp, 4
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push ebp
    push 5
    push ebx
    push eax
    call dword ptr [edx + 0x110]
    test eax, eax
    js .Lcap_fail
    add ebp, 4
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push ebp
    push 6
    push ebx
    push eax
    call dword ptr [edx + 0x110]
    test eax, eax
    js .Lcap_fail
    add ebp, 4
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push ebp
    push 7
    push ebx
    push eax
    call dword ptr [edx + 0x110]
    test eax, eax
    js .Lcap_fail
    inc ebx
    cmp ebx, 10
    jne .Lcap_samp_outer
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push 12
    push G_VC
    push 0
    push eax
    call dword ptr [edx + 0x17C]
    test eax, eax
    js .Lcap_fail
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push 4
    push G_PC
    push 0
    push eax
    call dword ptr [edx + 0x1B8]
    test eax, eax
    js .Lcap_fail
    mov eax, 1
    jmp .Lcap_out
.Lcap_fail:
    xor edi, edi
.Lcap_rel_tex:
    mov eax, dword ptr [esi + edi*4]
    test eax, eax
    jz .Lcap_rel_tex_next
    mov edx, dword ptr [eax]
    push eax
    call dword ptr [edx + 8]
    mov dword ptr [esi + edi*4], 0
.Lcap_rel_tex_next:
    inc edi
    cmp edi, 10
    jne .Lcap_rel_tex
    mov eax, dword ptr [G_DECL]
    test eax, eax
    jz .Lcap_rel_vs
    mov edx, dword ptr [eax]
    push eax
    call dword ptr [edx + 8]
    mov dword ptr [G_DECL], 0
.Lcap_rel_vs:
    mov eax, dword ptr [G_VS]
    test eax, eax
    jz .Lcap_rel_ps
    mov edx, dword ptr [eax]
    push eax
    call dword ptr [edx + 8]
    mov dword ptr [G_VS], 0
.Lcap_rel_ps:
    mov eax, dword ptr [G_PS]
    test eax, eax
    jz .Lcap_fail_ret
    mov edx, dword ptr [eax]
    push eax
    call dword ptr [edx + 8]
    mov dword ptr [G_PS], 0
.Lcap_fail_ret:
    xor eax, eax
.Lcap_out:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

.balign 16, 0x90
_restore83:
restore83:
    push ebp
    push ebx
    push esi
    push edi
    mov esi, ecx
    xor edi, edi
.Lres_tex:
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push dword ptr [esi + edi*4]
    push edi
    push eax
    call dword ptr [edx + 0x104]
    mov eax, dword ptr [esi + edi*4]
    test eax, eax
    jz .Lres_tex_next
    mov edx, dword ptr [eax]
    push eax
    call dword ptr [edx + 8]
    mov dword ptr [esi + edi*4], 0
.Lres_tex_next:
    inc edi
    cmp edi, 10
    jne .Lres_tex
    xor ebx, ebx
.Lres_samp_outer:
    mov ebp, ebx
    lea ebp, [ebp + ebp*4]
    shl ebp, 2
    add ebp, G_SAMP
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push dword ptr [ebp]
    push 1
    push ebx
    push eax
    call dword ptr [edx + 0x114]
    add ebp, 4
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push dword ptr [ebp]
    push 2
    push ebx
    push eax
    call dword ptr [edx + 0x114]
    add ebp, 4
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push dword ptr [ebp]
    push 5
    push ebx
    push eax
    call dword ptr [edx + 0x114]
    add ebp, 4
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push dword ptr [ebp]
    push 6
    push ebx
    push eax
    call dword ptr [edx + 0x114]
    add ebp, 4
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push dword ptr [ebp]
    push 7
    push ebx
    push eax
    call dword ptr [edx + 0x114]
    inc ebx
    cmp ebx, 10
    jne .Lres_samp_outer
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push 12
    push G_VC
    push 0
    push eax
    call dword ptr [edx + 0x178]
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push 4
    push G_PC
    push 0
    push eax
    call dword ptr [edx + 0x1B4]
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push dword ptr [G_DECL]
    push eax
    call dword ptr [edx + 0x15C]
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push dword ptr [G_VS]
    push eax
    call dword ptr [edx + 0x170]
    mov eax, dword ptr [DEV]
    mov edx, dword ptr [eax]
    push dword ptr [G_PS]
    push eax
    call dword ptr [edx + 0x1AC]
    mov eax, dword ptr [G_DECL]
    test eax, eax
    jz .Lres_rel_vs
    mov edx, dword ptr [eax]
    push eax
    call dword ptr [edx + 8]
    mov dword ptr [G_DECL], 0
.Lres_rel_vs:
    mov eax, dword ptr [G_VS]
    test eax, eax
    jz .Lres_rel_ps
    mov edx, dword ptr [eax]
    push eax
    call dword ptr [edx + 8]
    mov dword ptr [G_VS], 0
.Lres_rel_ps:
    mov eax, dword ptr [G_PS]
    test eax, eax
    jz .Lres_out
    mov edx, dword ptr [eax]
    push eax
    call dword ptr [edx + 8]
    mov dword ptr [G_PS], 0
.Lres_out:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
