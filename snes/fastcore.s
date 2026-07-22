;;; M4 hand-asm PDP-11/40 interpreter core (65816). Entry fast_step() executes
;;; ONE instruction on the C register globals (R[], PS, SR0) and returns 0 when
;;; it handled the instruction, or 0xFFFF ("fallback") to ask the C core
;;; (asmglue.c cpu_step -> c_step) to run it instead.
;;;
;;; Correctness by construction: the asm keeps its registers in the direct page
;;; (D -> fastregs) and only ever writes them back to the C globals on the
;;; HANDLED path (k_syncout). On any fallback it returns WITHOUT syncing, so all
;;; its side effects vanish and c_step re-runs the instruction from pristine
;;; state. It therefore handles only clean, main-RAM, non-trapping instructions
;;; and bails everything exotic (MMU on, odd/NXM/I-O addresses, opcodes it does
;;; not implement) to the proven C core. The only guest-memory WRITE happens
;;; last, after every operand is known to be clean RAM, so a bail never leaves a
;;; stray store behind (k_wr16/k_wr8 also refuse to write once bailf is set).
;;;
;;; DP layout (D = fastregs):
;;;   $00 R0 $02 R1 $04 R2 $06 R3 $08 R4 $0A R5 $0C R6(SP) $0E R7(PC)  $10 PS
;;;   $14 op      $16 srcval   $18 s_reg  $1A s_ea   $1C d_reg  $1E d_ea
;;;   $20 byte    $26-$28 SRAM far-ptr    $2A oreg   $2C oea    $2E spec
;;;   $30 regoff  $32 inc      $34 addr   $36 wval   $38 bailf  $3A m  $3C tmp
;;; PS flag bits: C=$01 V=$02 Z=$04 N=$08.  Guest RAM: addr -> bank $20+(a>>13),
;;; offset $6000|(a&$1FFE) (matches bus.c sram_ptr; MMU-off maps a<$E000 to RAM).

              .rtmodel version, "1"
              .rtmodel core, "*"

              .extern fastregs            ; uint16_t fastregs[32] (C, near)
              .extern R                   ; uint16_t R[8]   (C globals)
              .extern PS                  ; uint16_t PS
              .extern SR0                 ; uint16_t SR0

              .section farcode
              .public fast_step
;;; uint16_t fast_step(void)
fast_step:
              ;; ---- up-front bails (no processor state changed yet) ----
              lda     long:SR0
              and     ##1
              beq     fs_ck2
              brl     fs_bail0            ; MMU enabled -> C
fs_ck2:
              lda     long:R+14           ; PC
              lsr     a
              bcc     fs_ck3
              brl     fs_bail0            ; PC odd -> C
fs_ck3:
              cmp     ##0x7000
              bcc     fs_ckok
              brl     fs_bail0            ; PC >= 0xE000 (I/O region) -> C
fs_ckok:
              ;; ---- prologue: DBR=0, D=fastregs ----
              phb
              phd
              lda     ##0
              pha
              plb
              plb                         ; DBR = 0 (for any C calls; we make none)
              lda     ##fastregs
              tcd                         ; D -> register file
              jsl     k_syncin            ; C globals -> DP
              lda     ##0
              sta     0x38                ; bailf = 0

              ;; ---- fetch opcode (RAM, even, <0xE000 guaranteed) ----
              lda     0x0e
              jsl     k_rd16
              sta     0x14                ; op

              ;; ---- decode: MOV/MOVB have bits14-12 == 001 ----
              and     ##0x7000
              cmp     ##0x1000
              beq     fs_ismov
              brl     fs_bail1            ; not a handled opcode -> C
fs_ismov:
              ;; commit fetch, set byte flag
              inc     0x0e
              inc     0x0e
              lda     0x14
              and     ##0x8000
              beq     fs_word
              lda     ##1
              sta     0x20
              bra     fs_mov
fs_word:
              lda     ##0
              sta     0x20
fs_mov:
              ;; resolve src operand: spec = (op>>6) & 077
              lda     0x14
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              and     ##0x003f
              jsl     k_getea
              lda     0x2a
              sta     0x18                ; s_reg
              lda     0x2c
              sta     0x1a                ; s_ea
              jsl     k_loadsrc           ; A = src value (masked to width)
              sta     0x16

              ;; resolve dst operand: spec = op & 077
              lda     0x14
              and     ##0x003f
              jsl     k_getea
              lda     0x2a
              sta     0x1c                ; d_reg
              lda     0x2c
              sta     0x1e                ; d_ea

              lda     0x38
              bne     fs_bail1            ; any operand was exotic -> C

              ;; ---- MOV: store src value into dst ----
              lda     0x1c
              cmp     ##0xffff
              beq     mov_mem
              ;; dst is a register (d_reg = DP offset)
              lda     0x20
              beq     mov_regw
              ;; byte -> register: sign-extend low byte
              lda     0x16
              and     ##0x00ff
              bit     ##0x0080
              beq     mov_regb_st
              ora     ##0xff00
mov_regb_st:
              ldx     0x1c
              sta     0x00,x
              bra     mov_flags
mov_regw:
              lda     0x16
              ldx     0x1c
              sta     0x00,x
              bra     mov_flags
mov_mem:
              lda     0x16
              sta     0x36                ; value to write
              lda     0x20
              beq     mov_memw
              lda     0x1e
              jsl     k_wr8
              bra     mov_flags
mov_memw:
              lda     0x1e
              jsl     k_wr16
mov_flags:
              lda     0x38
              bne     fs_bail1            ; store refused (exotic dst) -> C
              lda     0x16
              jsl     k_setnz             ; N,Z from src; V=0; C kept

              ;; ---- handled: write DP regs back and return 0 ----
              jsl     k_syncout
              pld
              plb
              lda     ##0
              rtl

fs_bail1:                                 ; fallback after prologue (globals intact)
              pld
              plb
fs_bail0:                                 ; fallback before prologue
              lda     ##0xffff
              rtl

;;; ================= helpers (jsr/rts, all within farcode bank) =============

;;; k_syncin / k_syncout: C globals <-> DP register file (R0..R7, PS).
k_syncin:
              lda     long:R+0
              sta     0x00
              lda     long:R+2
              sta     0x02
              lda     long:R+4
              sta     0x04
              lda     long:R+6
              sta     0x06
              lda     long:R+8
              sta     0x08
              lda     long:R+10
              sta     0x0a
              lda     long:R+12
              sta     0x0c
              lda     long:R+14
              sta     0x0e
              lda     long:PS
              sta     0x10
              rtl
k_syncout:
              lda     0x00
              sta     long:R+0
              lda     0x02
              sta     long:R+2
              lda     0x04
              sta     long:R+4
              lda     0x06
              sta     long:R+6
              lda     0x08
              sta     long:R+8
              lda     0x0a
              sta     long:R+10
              lda     0x0c
              sta     long:R+12
              lda     0x0e
              sta     long:R+14
              lda     0x10
              sta     long:PS
              rtl

;;; k_getea: A = 6-bit operand spec, $20 = byte flag.
;;; out: $2A oreg = DP reg offset for register-direct, else 0xFFFF; $2C oea = EA.
;;; Applies autoincrement/decrement side effects to the DP registers.
k_getea:
              sta     0x2e                ; spec
              and     ##0x0007
              asl     a
              sta     0x30                ; regoff = r*2
              lda     0x2e
              lsr     a
              lsr     a
              lsr     a
              and     ##0x0007            ; m
              bne     ge_mem
              lda     0x30                ; mode 0: register direct
              sta     0x2a
              rtl
ge_mem:
              sta     0x3a                ; m
              lda     ##2
              sta     0x32                ; inc = 2
              lda     0x20
              beq     ge_incd
              lda     0x2e
              and     ##0x0007
              cmp     ##6
              bcs     ge_incd
              lda     ##1
              sta     0x32                ; byte & r<6 -> inc = 1
ge_incd:
              lda     ##0xffff
              sta     0x2a                ; mark memory operand
              lda     0x3a
              cmp     ##1
              beq     ge_m1
              cmp     ##2
              beq     ge_m2
              cmp     ##3
              beq     ge_m3
              cmp     ##4
              beq     ge_m4
              cmp     ##5
              beq     ge_m5
              cmp     ##6
              beq     ge_m6
ge_m7:                                    ; index deferred: EA = mem[R[r]+X]
              jsl     k_fetchw
              ldx     0x30
              clc
              adc     0x00,x
              jsl     k_rd16
              sta     0x2c
              rtl
ge_m6:                                    ; indexed: EA = R[r] + X
              jsl     k_fetchw
              ldx     0x30
              clc
              adc     0x00,x
              sta     0x2c
              rtl
ge_m1:                                    ; register deferred: EA = R[r]
              ldx     0x30
              lda     0x00,x
              sta     0x2c
              rtl
ge_m2:                                    ; autoincrement: EA=R[r]; R[r]+=inc
              ldx     0x30
              lda     0x00,x
              sta     0x2c
              clc
              adc     0x32
              sta     0x00,x
              rtl
ge_m3:                                    ; autoinc deferred: EA=mem[R[r]]; R[r]+=2
              ldx     0x30
              lda     0x00,x
              jsl     k_rd16
              sta     0x2c
              ldx     0x30
              inc     0x00,x
              inc     0x00,x
              rtl
ge_m4:                                    ; autodecrement: R[r]-=inc; EA=R[r]
              ldx     0x30
              lda     0x00,x
              sec
              sbc     0x32
              sta     0x00,x
              sta     0x2c
              rtl
ge_m5:                                    ; autodec deferred: R[r]-=2; EA=mem[R[r]]
              ldx     0x30
              lda     0x00,x
              sec
              sbc     ##2
              sta     0x00,x
              jsl     k_rd16
              sta     0x2c
              rtl

;;; k_fetchw: A = mem[PC]; PC += 2 (PC is code in RAM).
k_fetchw:
              lda     0x0e
              jsl     k_rd16
              inc     0x0e
              inc     0x0e
              rtl

;;; k_loadsrc: value of the src operand -> A (masked to width). Uses $18/$1A/$20.
k_loadsrc:
              lda     0x18
              cmp     ##0xffff
              beq     ls_mem
              ldx     0x18                ; register
              lda     0x00,x
              ldx     0x20
              beq     ls_ret
              and     ##0x00ff
ls_ret:
              rtl
ls_mem:
              lda     0x20
              bne     ls_byte
              lda     0x1a
              jsl     k_rd16
              rtl
ls_byte:
              lda     0x1a
              jsl     k_rd8
              and     ##0x00ff
              rtl

;;; k_rd16: A = guest addr -> A = word. Clean RAM only; else bailf=1, returns 0.
k_rd16:
              sta     0x34
              lsr     a
              bcs     rd16_bail           ; odd
              lda     0x34
              cmp     ##0xe000
              bcs     rd16_bail           ; I/O / high
              lda     0x34
              and     ##0x1ffe
              ora     ##0x6000
              sta     0x26
              lda     0x34
              xba
              and     ##0x00ff
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              clc
              adc     ##0x0020
              sta     0x28
              lda     [0x26]
              rtl
rd16_bail:
              lda     ##1
              sta     0x38
              lda     ##0
              rtl

;;; k_rd8: A = guest addr -> A = byte (0..0xFF). Clean RAM only; else bailf.
k_rd8:
              sta     0x34
              cmp     ##0xe000
              bcs     rd8_bail
              lda     0x34
              and     ##0x1ffe
              ora     ##0x6000
              sta     0x26
              lda     0x34
              xba
              and     ##0x00ff
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              clc
              adc     ##0x0020
              sta     0x28
              lda     [0x26]              ; word
              pha
              lda     0x34
              and     ##1
              bne     rd8_hi
              pla
              and     ##0x00ff
              rtl
rd8_hi:
              pla
              xba
              and     ##0x00ff
              rtl
rd8_bail:
              lda     ##1
              sta     0x38
              lda     ##0
              rtl

;;; k_wr16: A = guest addr, $36 = value. Writes only clean RAM and only while
;;; not already bailing; otherwise sets bailf and writes nothing.
k_wr16:
              ldx     0x38
              bne     wr16_skip
              sta     0x34
              lsr     a
              bcs     wr16_bad
              lda     0x34
              cmp     ##0xe000
              bcs     wr16_bad
              lda     0x34
              and     ##0x1ffe
              ora     ##0x6000
              sta     0x26
              lda     0x34
              xba
              and     ##0x00ff
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              clc
              adc     ##0x0020
              sta     0x28
              lda     0x36
              sta     [0x26]
              rtl
wr16_bad:
              lda     ##1
              sta     0x38
wr16_skip:
              rtl

;;; k_wr8: A = guest addr, $36 = value (low byte). Clean RAM only; guards bailf.
k_wr8:
              ldx     0x38
              bne     wr8_skip
              sta     0x34
              cmp     ##0xe000
              bcs     wr8_bad
              lda     0x34
              and     ##0x1ffe
              ora     ##0x6000
              sta     0x26
              lda     0x34
              xba
              and     ##0x00ff
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              lsr     a
              clc
              adc     ##0x0020
              sta     0x28
              lda     [0x26]              ; old word
              pha
              lda     0x34
              and     ##1
              bne     wr8_hi
              pla                         ; low byte := value
              and     ##0xff00
              ora     0x36
              sta     [0x26]
              rtl
wr8_hi:                                   ; high byte := value
              pla
              and     ##0x00ff
              sta     0x3c
              lda     0x36
              and     ##0x00ff
              xba
              ora     0x3c
              sta     [0x26]
              rtl
wr8_bad:
              lda     ##1
              sta     0x38
wr8_skip:
              rtl

;;; k_setnz: A = value, $20 = byte. Sets PS N,Z from the value; clears V; keeps C.
k_setnz:
              tay                         ; Y = raw value
              lda     0x10
              and     ##0xfff1            ; clear N,Z,V
              sta     0x10
              tya
              ldx     0x20
              beq     snz_word
              and     ##0x00ff            ; byte width
              tay
              bit     ##0x0080
              beq     snz_z
              lda     0x10
              ora     ##0x0008            ; N
              sta     0x10
              bra     snz_z
snz_word:
              tay
              bit     ##0x8000
              beq     snz_z
              lda     0x10
              ora     ##0x0008            ; N
              sta     0x10
snz_z:
              tya
              bne     snz_ret
              lda     0x10
              ora     ##0x0004            ; Z
              sta     0x10
snz_ret:
              rtl

;;; ======================================================================
;;; fast_run: the original M4 ceiling probe (benchfast target). Kept as-is;
;;; its internal labels are distinct from the fast_step helpers above.
;;; ======================================================================
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

;;; ============ helpers (fast_run) ============
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
              beq     snz_z0
              bpl     snz_done
              lda     0x10
              ora     ##0x0008            ; N
              sta     0x10
              rtl
snz_z0:
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
