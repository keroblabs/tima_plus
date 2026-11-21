        .section .vectors,"a"
rom_vectors:
        .long   __reset_ssp,reset
        .rept   254
        .long   fault
        .endr

        .section .text.boot,"ax"
        .globl  reset
reset:
        move.w  #0x2700,%sr
        | While executing from permanent ROM, drive PA4 low to expose RAM.
        move.b  #0x10,0xeffffe
        move.b  #0x10,0xefe7fe
        move.b  #0,0xeffffe
        movea.l #__stack_top,%sp
        move.l  #0x52554e21,__mailbox

        lea     rom_vectors,%a0
        movea.l #0,%a1
        move.w  #255,%d0
.Lvector:
        move.l  (%a0)+,(%a1)+
        dbra    %d0,.Lvector

        lea     __data_load,%a0
        lea     __data_start,%a1
        lea     __data_end,%a2
.Lcopy:
        cmpa.l  %a2,%a1
        beq.s   .Lbss
        move.b  (%a0)+,(%a1)+
        bra.s   .Lcopy

.Lbss:
        lea     __bss_start,%a1
        lea     __bss_end,%a2
.Lclear:
        cmpa.l  %a2,%a1
        beq.s   .Lcall
        clr.b   (%a1)+
        bra.s   .Lclear

.Lcall:
        jsr     tima_boot
        tst.l   %d0
        bne.s   failed
halt:
        stop    #0x2700
        bra.s   halt
failed:
        move.l  #0x4641494c,__mailbox
        bra.s   halt
fault:
        move.l  #0x54524150,__mailbox
        bra.s   halt
