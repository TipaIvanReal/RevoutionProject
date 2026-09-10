.intel_syntax noprefix
.code32
.section .text
.extern _capture83
.extern _restore83
.global _capture83_c
.global _restore83_c

_capture83_c:
    mov ecx, dword ptr [esp + 4]
    jmp _capture83

_restore83_c:
    mov ecx, dword ptr [esp + 4]
    jmp _restore83
