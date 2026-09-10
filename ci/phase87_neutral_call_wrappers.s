.intel_syntax noprefix
.code32
.section .text
.extern make_one
.extern convert_or_default
.global _make_one_c
.global _convert_or_default_c

_make_one_c:
    jmp make_one

_convert_or_default_c:
    jmp convert_or_default
