        | Test-only boot code for an 800K floppy read by the original Plus ROM.
        .text
        .word   0x4c4b
        bra.w   probe
        .word   0x4418          | The Plus ROM calls bbEntry for version $44xx.
        .space  0x82            | Fill the header through offset $89.

probe:
        move.w  #0x2700,%sr
        cmpi.b  #0x0f,0xc00019  | Odd-byte interrupt vector register after reset.
        bne.s   halt
        cmpi.b  #0xff,0xc00018  | Even byte is undriven.
        bne.s   halt
        cmpi.w  #0xffff,0xc00018
        bne.s   halt
        move.b  #0x73,0xc00019
        cmpi.b  #0x73,0xc00019
        bne.s   halt

        move.b  #0x04,0xc00015  | Enable UART-B transmission.
        lea     message(%pc),%a0
next_byte:
        move.b  (%a0)+,%d0
        beq.s   halt
wait_ready:
        btst    #2,0xc00013
        beq.s   wait_ready
        move.b  %d0,0xc00017
        bra.s   next_byte

halt:
        stop    #0x2700
        bra.s   halt

message:
        .ascii  "MAC_PLUS_DUART_PASS\n"
        .byte   0
