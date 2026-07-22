;;; M4 ceiling probe: a minimal PDP-11 interpreter hot loop in hand 65816,
;;; with the PDP-11 register file in the direct page (D -> fastregs). Covers
;;; exactly the boot RAM-clear hot loop's opcodes (MOV imm->reg, CLR (Rn)+,
;;; CMP reg,reg, BCS, BR) with real fetch/decode/dispatch + real SRAM guest
;;; memory access. Measures what the integrated asm core can do vs the 1.2 KIPS
;;; C interpreter, before we build the full core.
;;;
;;; DP layout (D = &fastregs, a bank-0 near global):
;;;   $00 R0  $02 R1  $04 R2  $06 R3  $08 R4  $0A R5  $0C R6  $0E R7(PC)  $10 PS
;;;   $12-$14 far-ptr scratch (off,off,bank)   $16 addr   $18 op   $1A tmp
;;;   $1C icount-lo  $1E icount-hi
;;; PS flag bits: N=$08 Z=$04 V=$02 C=$01
;;; Guest RAM in cart SRAM: addr A -> bank $20+(A>>13), off $6000|(A&$1FFF).

              .rtmodel version, "1"
              .rtmodel core, "*"

              .extern fastregs            ; uint16_t fastregs[16] (C, near)

              .section farcode
              .public fast_step
;;; uint16_t fast_step(void) -- execute ONE PDP-11 instruction on the C
;;; register globals (R[], PS, SR0). Returns 0 if handled here, 0xFFFF to ask
;;; the C fallback (asmglue.c cpu_step) to run it. A1: handle nothing yet, so
;;; the whole 68-check suite runs through the proven C core and stays green.
fast_step:
              lda     ##0xffff            ; ST_FALLBACK
              rtl

              .public fast_run
;;; void fast_run(void) -- never returns; updates mailbox $0102/$0104 icount.
fast_run:
              php
              phb
              phd
              rep     #0x30               ; 16-bit A/X/Y
              lda     ##0x0000
              pha
              plb                         ; DBR = 0 (mailbox writes)
              plb
              lda     ##fastregs
              tcd                         ; D -> register file

              lda     ##0
              sta     0x1c                ; icount lo
              sta     0x1e                ; icount hi

;;; ------------------------------------------------------------ main loop
next:
              ;; fetch word at PC (R7 = dp $0E)
              lda     0x0e
              jsl     gread               ; A = mem[PC]
              sta     0x18                ; op
              inc     0x0e
              inc     0x0e

              ;; ---- decode by masking the few opcodes we handle ----
              lda     0x18
              and     ##0xf000
              cmp     ##0x1000            ; MOV
              bne     dc_1
              brl     do_mov
dc_1:         cmp     ##0x2000            ; CMP
              bne     dc_2
              brl     do_cmp
dc_2:         lda     0x18
              and     ##0xffc0
              cmp     ##0x0a00            ; CLR (op & $FFC0)
              bne     dc_3
              brl     do_clr
dc_3:         lda     0x18
              and     ##0xff00
              cmp     ##0x0100            ; BR (op & $FF00)
              bne     dc_4
              brl     do_br
dc_4:         cmp     ##0x8700            ; BCS
              bne     dc_bad
              brl     do_bcs
dc_bad:       brl     unhandled

;;; ---- MOV imm->reg (src = immediate via PC autoinc, dst = mode0 reg) ----
do_mov:
              ;; immediate value at PC
              lda     0x0e
              jsl     gread
              sta     0x1a                ; value
              inc     0x0e
              inc     0x0e
              ;; dst reg index = (op & 7); DP offset = reg*2
              lda     0x18
              and     ##0x0007
              asl     a
              tax
              lda     0x1a
              sta     0x00,x              ; reg[dst] = value
              jsl     setnz               ; N/Z from A, V=0
              brl     tick

;;; ---- CLR (Rn)+ : EA=reg[n]; reg[n]+=2; mem[EA]=0; Z set ----
do_clr:
              lda     0x18
              and     ##0x0007
              asl     a
              tax
              lda     0x00,x              ; EA = reg[n]
              sta     0x16
              inc     0x00,x
              inc     0x00,x              ; reg[n] += 2 (word autoinc)
              lda     ##0
              ldy     0x16
              jsl     gwrite              ; mem[EA] = 0  (addr in Y, val in A)
              ;; flags: Z=1 N=0 V=0 C=0
              lda     0x10
              and     ##0xfff0
              ora     ##0x0004
              sta     0x10
              brl     tick

;;; ---- CMP reg,reg : src-dst, set N,Z,C(=borrow) ----
do_cmp:
              lda     0x18
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              and     ##0x0007            ; src reg
              asl     a
              tax
              lda     0x00,x              ; A = reg[src]
              sta     0x1a
              lda     0x18
              and     ##0x0007            ; dst reg
              asl     a
              tax
              lda     0x1a
              sec
              sbc     0x00,x              ; A = src - dst; 65816 C = !borrow
              tay                         ; Y = result (carry preserved)
              lda     ##0
              rol     a                   ; A = 65816 carry (0/1)
              eor     ##0x0001            ; PDP C = borrow = !hwcarry
              sta     0x1a                ; PDP C bit
              lda     0x10
              and     ##0xfff0            ; clear N,Z,V,C
              ora     0x1a                ; set C
              sta     0x10
              tya                         ; result -> sets 65816 N,Z
              beq     cmp_z
              bmi     cmp_n
              brl     tick
cmp_n:
              lda     0x10
              ora     ##0x0008            ; N
              sta     0x10
              brl     tick
cmp_z:
              lda     0x10
              ora     ##0x0004            ; Z
              sta     0x10
              brl     tick

;;; ---- BR : PC += 2*sext(op[7:0]) ----
do_br:
              lda     0x18
              and     ##0x00ff
              jsl     sext8
              asl     a
              clc
              adc     0x0e
              sta     0x0e
              brl     tick

;;; ---- BCS : branch if C ----
do_bcs:
              lda     0x10
              and     ##0x0001
              bne     bcs_take
              brl     tick                ; C clear -> no branch
bcs_take:
              lda     0x18
              and     ##0x00ff
              jsl     sext8
              asl     a
              clc
              adc     0x0e
              sta     0x0e
              brl     tick

;;; ---- per-instruction tail: bump icount, publish every 1024 ----
tick:
              inc     0x1c
              bne     nocarry
              inc     0x1e
nocarry:
              lda     0x1c
              and     ##0x03ff
              beq     tick_pub
              brl     next
tick_pub:
              lda     0x1c
              sta     long:0x000102
              lda     0x1e
              sta     long:0x000104
              brl     next

unhandled:
              lda     ##0xdead
              sta     long:0x000100
u_spin:       bra     u_spin

;;; ============ helpers ============
;;; gread: A = guest addr -> A = word. Uses X, ptr scratch $12-$15.
gread:
              sta     0x16                ; addr
              and     ##0x1fff
              ora     ##0x6000
              sta     0x12                ; off (16-bit)
              lda     0x16
              xba                         ; high byte -> low
              and     ##0x00e0
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              lsr     a                   ; (addr>>13) in 0..7
              clc
              adc     ##0x0020            ; bank $20+n
              sta     0x14                ; bank byte (+pad)
              lda     [0x12]              ; read 16-bit far
              rtl

;;; gwrite: addr in Y, value in A -> mem[addr]=A. Uses ptr scratch.
gwrite:
              sta     0x1a                ; value
              tya
              sta     0x16                ; addr
              and     ##0x1fff
              ora     ##0x6000
              sta     0x12
              lda     0x16
              xba
              and     ##0x00e0
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              clc
              adc     ##0x0020
              sta     0x14
              lda     0x1a
              sta     [0x12]              ; write 16-bit far
              rtl

;;; setnz: N,Z from A (16-bit); V cleared. Leaves A intact-ish.
setnz:
              tay                         ; save value
              lda     0x10
              and     ##0xfff1            ; clear N,Z,V (keep C)
              sta     0x10
              tya
              beq     snz_z
              bpl     snz_done
              lda     0x10
              ora     ##0x0008            ; N
              sta     0x10
              rtl
snz_z:
              lda     0x10
              ora     ##0x0004            ; Z
              sta     0x10
snz_done:
              rtl

;;; sext8: A low byte -> sign-extended 16-bit in A.
sext8:
              and     ##0x00ff
              cmp     ##0x0080
              bcc     sx_done
              ora     ##0xff00
sx_done:
              rtl
