        ;; Echo plugin public API for Z80.
        .module squid_client_z80_echo
        .optsdcc -mz80 sdcccall(1)

        .globl _squid_client_echo
        .globl _squid_client_copy
        .globl _squid_client_exchange

        .equ CLIENT_PACKET,   2
        .equ CLIENT_CAPACITY, 4
        .equ ERROR_ARGUMENT, -1

        .area _CODE

;; int squid_client_echo(client, data, size, reply)
;; client=HL, data=DE, stack=[size:u8][reply:pointer].
_squid_client_echo::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2: client
        push    de                      ; -4: data

        ld      a,-1(ix)
        or      a,-2(ix)
        jr      z,squid_client_echo_argument
        ld      a,-3(ix)
        or      a,-4(ix)
        jr      z,squid_client_echo_argument
        ld      a,4(ix)                 ; size
        or      a,a
        jr      z,squid_client_echo_argument
        ld      a,6(ix)                 ; reply pointer high
        or      a,5(ix)
        jr      z,squid_client_echo_argument

        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)                  ; packet
        inc     hl
        ld      d,(hl)
        ld      a,d
        or      a,e
        jr      z,squid_client_echo_argument
        inc     hl                      ; client capacity
        ld      b,(hl)
        inc     hl
        ld      a,(hl)
        or      a,a
        jr      nz,squid_client_echo_capacity_word
        ld      a,b
        cp      a,4(ix)
        jr      c,squid_client_echo_argument
        jr      squid_client_echo_copy
squid_client_echo_capacity_word:
        ;; A non-zero high byte is invalid for the 255-byte packet protocol.
        jr      squid_client_echo_argument

squid_client_echo_copy:
        ex      de,hl                   ; HL=packet
        inc     hl                      ; request payload
        ld      e,-4(ix)
        ld      d,-3(ix)
        ld      c,4(ix)
        ld      b,#0
        push    bc
        call    _squid_client_copy

        ld      l,-2(ix)
        ld      h,-1(ix)
        ld      e,4(ix)
        ld      d,#0
        call    _squid_client_exchange
        bit     7,d
        jr      nz,squid_client_echo_finish

        ld      l,5(ix)                 ; reply->data = client->packet
        ld      h,6(ix)
        push    de                      ; save response size
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
        pop     hl                      ; L=response size
        ld      a,l
        ld      (de),a
        ld      de,#0
        jr      squid_client_echo_finish

squid_client_echo_argument:
        ld      de,#ERROR_ARGUMENT
squid_client_echo_finish:
        ld      sp,ix
        pop     ix
        pop     hl                      ; return address
        pop     bc                      ; size + reply low
        inc     sp                      ; reply high
        jp      (hl)
