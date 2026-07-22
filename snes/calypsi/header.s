;;; SNES cartridge header and interrupt vectors for snespdp11.
;;; Cloned from snesmac/snes/calypsi/header.s (Calypsi stock snes-HiROM shape).
;;; ROM+RAM+battery, 256 KB SRAM requested — Phase B wants the guest's 128 KB
;;; in cart SRAM if Mesen grants it (snesmac precedent says HiROM does).

              .rtmodel version, "1"
              .rtmodel core, "*"

;;; Extended header $FFB0-$FFBF (licensee byte $33 => present)
              .section snesheaderextended, root
              .ascii  "  "           ; maker code
              .ascii  "SPDP"         ; game code
              .byte   0,0,0,0,0,0,0  ; reserved
              .byte   0              ; expansion RAM size
              .byte   0              ; special version
              .byte   0              ; cartridge sub-type

;;; Header $FFC0-$FFDF
              .section snesheader, root
              .ascii  "SNESPDP11 UNIX V6    "   ; 21-byte title
              .byte   0x21           ; map mode: HiROM, slow
              .byte   0x02           ; cartridge type: ROM+RAM+battery
              .byte   0x0c           ; ROM size: 4 MB (room for the 2 MB v6 root)
              .byte   0x08           ; RAM size: 256 KB requested
              .byte   0x01           ; country: USA (NTSC)
              .byte   0x33           ; licensee: extended header present
              .byte   0x00           ; version
              .word   0xaaaa         ; checksum complement (dummy, patched by raw2sfc.py)
              .word   0x5555         ; checksum (dummy)

;;; Native + emulation vectors.
              .section snesvec1, root
              .word   0, 0                  ; $FFE0,$FFE2 unused
              .word   __vector_stub         ; $FFE4 native COP
              .word   __vector_stub         ; $FFE6 native BRK
              .word   __vector_stub         ; $FFE8 native ABORT
              .word   __vector_stub         ; $FFEA native NMI
              .word   0                     ; $FFEC unused
              .word   __vector_stub         ; $FFEE native IRQ
              .word   0, 0                  ; $FFF0,$FFF2 unused
              .word   __vector_stub         ; $FFF4 emulation COP
              .word   0                     ; $FFF6 unused
              .word   __vector_stub         ; $FFF8 emulation ABORT
              .word   __vector_stub         ; $FFFA emulation NMI

              .section snesvec2, root
              .word   __vector_stub         ; $FFFE emulation IRQ/BRK

              .section code, root
              .public __vector_stub
__vector_stub:
              rti
