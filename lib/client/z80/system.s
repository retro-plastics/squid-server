        ;; System-identification plugin public API for Z80.
        .module squid_client_z80_system
        .optsdcc -mz80 sdcccall(1)

        .globl _squid_client_system_id
        .globl _squid_client_exchange

        .equ CLIENT_PACKET,   2
        .equ CLIENT_CAPACITY, 4
        .equ ERROR_ARGUMENT, -1

        .area _CODE

;; int squid_client_system_id(client, reply)
;; client=HL, reply=DE.
_squid_client_system_id::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2: client
        push    de                      ; -4: reply
        ld      a,-1(ix)
        or      a,-2(ix)
        jr      z,squid_client_system_argument
        ld      a,-3(ix)
        or      a,-4(ix)
        jr      z,squid_client_system_argument

        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)                  ; packet
        inc     hl
        ld      d,(hl)
        ld      a,d
        or      a,e
        jr      z,squid_client_system_argument
        inc     hl                      ; capacity
        ld      a,(hl)
        inc     hl
        ld      h,(hl)
        ld      l,a
        ld      bc,#2
        or      a,a
        sbc     hl,bc
        jr      c,squid_client_system_argument

        ex      de,hl                   ; packet
        inc     hl
        ld      (hl),#'i'
        inc     hl
        ld      (hl),#'d'
        ld      l,-2(ix)
        ld      h,-1(ix)
        ld      de,#2
        call    _squid_client_exchange
        bit     7,d
        jr      nz,squid_client_system_finish

        ld      l,-4(ix)                ; reply
        ld      h,-3(ix)
        push    de
        ld      e,-2(ix)
        ld      d,-1(ix)
        ex      de,hl                   ; HL=client, DE=reply
        inc     hl
        inc     hl
        ld      a,(hl)
        ld      (de),a
        inc     hl
        inc     de
        ld      a,(hl)
        ld      (de),a
        inc     de
        pop     hl
        ld      a,l
        ld      (de),a
        ld      de,#0
        jr      squid_client_system_finish

squid_client_system_argument:
        ld      de,#ERROR_ARGUMENT
squid_client_system_finish:
        ld      sp,ix
        pop     ix
        ret
