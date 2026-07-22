;; SNES HiROM memory description for snespdp11 (adapted from snesmac's).
;;  - $0100-$01FF of bank-0 RAM reserved for the Mesen-Lua test mailbox
;;    (fixed addresses; see core-c/tests/test_cpu.c PDP11_SNES block)
;;  - far RAM = WRAM $7E2000..$7EFFFF: guest PDP-11 memory (32 KB in the
;;    cputest build) + host emulator state. Phase B moves the guest's full
;;    128 KB to cart SRAM and reshapes this.
(define memories
  '((memory DirectPage
            (address (#x0 . #xff))
            (type RAM)
            (section registers ztiny tiny))
    (memory LoRAM
            (address (#x200 . #x1fff))
            (type RAM)
            (qualifier near)
            (section stack data znear near))
    (memory HiRAM
            (address (#x7e2000 . #x7effff))
            (type RAM)
            (qualifier far)
            (section heap zfar far huge))
    ;; WRAM bank $7F: the rk1 swap-device buffer (62 KB, own named section —
    ;; the linker will not split the zfar group across banks)
    (memory HiRAM2
            (address (#x7f0000 . #x7fffff))
            (type RAM)
            (qualifier far)
            (section rk1buf))

    (memory LoROM
            (address (#x8000 . #xffaf))
            (type ROM)
            (qualifier near)
            (scatter-to LoROM-store)
            (section code compactcode cdata cnear switch itiny idata inear data_init_table))

    (memory HeaderExtended
            (address (#xffb0 . #xffbf))
            (type ROM)
            (qualifier near)
            (section snesheaderextended)
            (scatter-to HeaderExt-store))

    (memory HeaderBasic
            (address (#xffc0 . #xffdf))
            (type ROM)
            (qualifier near)
            (section snesheader)
            (scatter-to Header-store))

    (memory Vector
            (address (#xffe0 . #xffff))
            (type ROM)
            (qualifier near)
            (scatter-to Vector-store)
            (section (snesvec1 #xffe0) (reset #xfffc) (snesvec2 #xfffe)))

    (memory HiROM-c0
            (address (#xc00000 . #xc0ffff))
            (type ROM)
            (qualifier far)
            (section farcode cdata cnear cfar chuge switch ifar ihuge
                     (LoROM-store #xc08000)
                     (HeaderExt-store #xc0ffb0)
                     (Header-store #xc0ffc0)
                     (Vector-store #xc0ffe0)))

    (memory HiROM
            (address (#xc10000 . #xc7ffff))
            (type ROM)
            (qualifier far)
            (section farcode cdata cnear cfar chuge switch ifar ihuge))

    (block stack (size #x1800))
    (base-address _DirectPageStart DirectPage 0)
    (base-address _NearBaseAddress LoRAM 0)
    ))
