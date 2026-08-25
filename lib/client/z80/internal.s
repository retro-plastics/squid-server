        ;; Shared internal helpers for the typed Z80 plugin clients.
        ;; xcc 2.3.2 sdcccall(1): word arguments use HL then DE; remaining
        ;; arguments are packed on the stack and removed by the callee.
        .module squid_client_z80_internal
        .optsdcc -mz80 sdcccall(1)

        .globl _squid_client_read_u16
        .globl _squid_client_read_u32
        .globl _squid_client_write_u16
        .globl _squid_client_write_u32
        .globl _squid_client_text_size
        .globl _squid_client_copy
        .globl _squid_client_response
        .globl _squid_client_exchange

        .equ CLIENT_PACKET, 2
        .equ ERROR_ARGUMENT, -1
        .equ ERROR_PROTOCOL, -4

        .area _CODE

;; uint16_t squid_client_read_u16(bytes)
;; Return value: DE.
_squid_client_read_u16::
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ret

;; uint32_t squid_client_read_u32(bytes)
;; Return value: DEHL, least-significant word first in DE.
_squid_client_read_u32::
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        inc     hl
        ld      a,(hl)
        inc     hl
        ld      h,(hl)
        ld      l,a
        ret

;; void squid_client_write_u16(bytes, value)
_squid_client_write_u16::
        ld      (hl),e
        inc     hl
        ld      (hl),d
        ret

;; void squid_client_write_u32(bytes, value)
;; The four-byte value is at 4(ix)..7(ix) after saving IX.
_squid_client_write_u32::
        push    ix
        ld      ix,#0
        add     ix,sp
        ld      a,4(ix)
        ld      (hl),a
        inc     hl
        ld      a,5(ix)
        ld      (hl),a
        inc     hl
        ld      a,6(ix)
        ld      (hl),a
        inc     hl
        ld      a,7(ix)
        ld      (hl),a
        pop     ix
        pop     hl                      ; return address
        pop     bc                      ; low value word
        pop     bc                      ; high value word
        jp      (hl)

;; int squid_client_text_size(text, maximum)
;; A null text pointer is the empty string. Counted plugin strings cannot
;; exceed 255 bytes, so an 8-bit counter plus wrap detection is sufficient.
_squid_client_text_size::
        push    ix
        ld      ix,#0
        add     ix,sp
        ld      b,4(ix)                 ; packed u8 maximum
        ld      c,#0
        ld      a,h
        or      a,l
        jr      z,squid_client_text_done
squid_client_text_loop:
        ld      a,(hl)
        or      a,a
        jr      z,squid_client_text_done
        inc     hl
        inc     c
        jr      z,squid_client_text_too_long
        ld      a,b
        cp      a,c
        jr      nc,squid_client_text_loop
squid_client_text_too_long:
        ld      de,#ERROR_ARGUMENT
        jr      squid_client_text_finish
squid_client_text_done:
        ld      e,c
        ld      d,#0
squid_client_text_finish:
        pop     ix
        pop     hl                      ; return address
        inc     sp                      ; packed u8 maximum
        jp      (hl)

;; void squid_client_copy(destination, source, size)
_squid_client_copy::
        push    ix
        ld      ix,#0
        add     ix,sp
        ld      c,4(ix)
        ld      b,5(ix)
        ld      a,b
        or      a,c
        jr      z,squid_client_copy_done
        ex      de,hl                    ; LDIR copies (HL) to (DE)
        ldir
squid_client_copy_done:
        pop     ix
        pop     hl                      ; return address
        pop     bc                      ; size
        jp      (hl)

;; int squid_client_response(client, request_size, opcode, minimum, out_size)
_squid_client_response::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; preserve client across exchange
        call    _squid_client_exchange
        pop     hl
        bit     7,d
        jr      nz,squid_client_response_finish

        ;; A successful exchange is at most 255 bytes, and must contain the
        ;; common [opcode|0x80][status] response prefix.
        ld      a,d
        or      a,a
        jr      nz,squid_client_response_have_prefix
        ld      a,e
        cp      a,#2
        jr      c,squid_client_response_protocol
squid_client_response_have_prefix:
        inc     hl
        inc     hl                      ; client->packet
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        ld      a,4(ix)                 ; packed opcode
        or      a,#0x80
        ld      l,a
        ld      a,(bc)
        cp      a,l
        jr      nz,squid_client_response_protocol

        inc     bc
        ld      a,(bc)                  ; plugin status
        or      a,a
        jr      z,squid_client_response_check_size
        ld      e,a
        ld      d,#0
        jr      squid_client_response_finish

squid_client_response_check_size:
        ld      a,e
        cp      a,5(ix)                 ; packed minimum size
        jr      c,squid_client_response_protocol
        ld      l,6(ix)                 ; optional returned-size pointer
        ld      h,7(ix)
        ld      a,h
        or      a,l
        jr      z,squid_client_response_ok
        ld      (hl),e
        inc     hl
        ld      (hl),d
squid_client_response_ok:
        ld      de,#0
        jr      squid_client_response_finish

squid_client_response_protocol:
        ld      de,#ERROR_PROTOCOL
squid_client_response_finish:
        pop     ix
        pop     hl                      ; return address
        pop     bc                      ; opcode + minimum size
        pop     bc                      ; returned-size pointer
        jp      (hl)
