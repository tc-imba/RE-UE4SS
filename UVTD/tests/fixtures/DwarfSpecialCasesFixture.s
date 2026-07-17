    .section .debug_abbrev,"",@progbits
    .uleb128 1
    .uleb128 0x11
    .byte 1
    .uleb128 0x03
    .uleb128 0x08
    .byte 0
    .byte 0

    .uleb128 2
    .uleb128 0x39
    .byte 1
    .uleb128 0x03
    .uleb128 0x08
    .byte 0
    .byte 0

    .uleb128 3
    .uleb128 0x13
    .byte 0
    .uleb128 0x03
    .uleb128 0x08
    .uleb128 0x3c
    .uleb128 0x19
    .byte 0
    .byte 0

    .uleb128 4
    .uleb128 0x13
    .byte 1
    .uleb128 0x47
    .uleb128 0x13
    .uleb128 0x0b
    .uleb128 0x0b
    .byte 0
    .byte 0

    .uleb128 5
    .uleb128 0x0d
    .byte 0
    .uleb128 0x03
    .uleb128 0x08
    .uleb128 0x49
    .uleb128 0x13
    .uleb128 0x38
    .uleb128 0x0b
    .byte 0
    .byte 0

    .uleb128 6
    .uleb128 0x13
    .byte 1
    .uleb128 0x03
    .uleb128 0x08
    .uleb128 0x0b
    .uleb128 0x0b
    .byte 0
    .byte 0

    .uleb128 7
    .uleb128 0x0d
    .byte 0
    .uleb128 0x03
    .uleb128 0x08
    .uleb128 0x49
    .uleb128 0x13
    .uleb128 0x38
    .uleb128 0x18
    .byte 0
    .byte 0

    .uleb128 8
    .uleb128 0x24
    .byte 0
    .uleb128 0x03
    .uleb128 0x08
    .uleb128 0x3e
    .uleb128 0x0b
    .uleb128 0x0b
    .uleb128 0x0b
    .byte 0
    .byte 0

    .byte 0

    .section .debug_info,"",@progbits
.Lcu_begin:
    .long .Lcu_end - .Lversion
.Lversion:
    .short 4
    .long 0
    .byte 8

    .uleb128 1
    .asciz "DwarfSpecialCasesFixture.s"

    .uleb128 2
    .asciz "SpecNamespace"
.Lspecified_decl:
    .uleb128 3
    .asciz "Specified"
    .byte 0

    .uleb128 4
    .long .Lspecified_decl - .Lcu_begin
    .byte 4
    .uleb128 5
    .asciz "Value"
    .long .Lint_type - .Lcu_begin
    .byte 0
    .byte 0

    .uleb128 6
    .asciz "DynamicLocation"
    .byte 4
    .uleb128 7
    .asciz "Value"
    .long .Lint_type - .Lcu_begin
    .uleb128 1
    .byte 0x06
    .byte 0

.Lint_type:
    .uleb128 8
    .asciz "int"
    .byte 0x05
    .byte 4

    .byte 0
.Lcu_end:

    .section .note.GNU-stack,"",@progbits
