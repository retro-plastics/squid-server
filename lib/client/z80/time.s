        ;; Time plugin public API for Z80.
        .module squid_client_z80_time
        .optsdcc -mz80 sdcccall(1)

        .globl _squid_client_time_get
        .globl _squid_client_response

        .equ CLIENT_PACKET,   2
        .equ CLIENT_CAPACITY, 4
        .equ ERROR_ARGUMENT, -1
        .equ ERROR_PROTOCOL, -4
        .equ TIME_GET,         1
        .equ TIME_REPLY_SIZE, 16

        .area _CODE

;; int squid_client_time_get(client, mode, value)
;; client=HL, stack=[mode:u8][value:pointer].
_squid_client_time_get::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2: client
        ld      bc,#0
        push    bc                      ; -4: response size

        ld      a,-1(ix)
        or      a,-2(ix)
        jp      z,squid_client_time_argument
        ld      a,6(ix)
        or      a,5(ix)                 ; value
        jp      z,squid_client_time_argument
        ld      a,4(ix)                 ; mode: UTC=0, local=1
        cp      a,#2
        jp      nc,squid_client_time_argument

        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)                  ; packet
        inc     hl
        ld      d,(hl)
        ld      a,d
        or      a,e
        jp      z,squid_client_time_argument
        inc     hl                      ; capacity
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        ld      h,b
        ld      l,c
        ld      de,#2
        or      a,a
        sbc     hl,de
        jp      c,squid_client_time_argument

        ;; request = [GET][mode]
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl
        inc     hl
        ld      (hl),#TIME_GET
        inc     hl
        ld      a,4(ix)
        ld      (hl),a

        push    ix
        pop     hl
        ld      de,#-4
        add     hl,de
        push    hl                      ; &response_size
        ld      bc,#0x1001              ; minimum=16, opcode=GET
        push    bc
        ld      de,#2
        ld      l,-2(ix)
        ld      h,-1(ix)
        call    _squid_client_response
        ld      a,d
        or      a,e
        jr      nz,squid_client_time_finish
        ld      a,-3(ix)
        or      a,a
        jr      nz,squid_client_time_protocol
        ld      a,-4(ix)
        cp      a,#TIME_REPLY_SIZE
        jr      nz,squid_client_time_protocol

        ;; Copy and decode the fixed response into squid_client_time_t.
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl                   ; response
        inc     hl
        inc     hl                      ; response byte 2
        ld      e,5(ix)
        ld      d,6(ix)                 ; value
        ld      bc,#7                   ; year + month..second
        ldir

        ld      a,(hl)                  ; weekday/DST flags
        inc     hl
        ld      c,a
        and     a,#0x07
        ld      (de),a                  ; weekday, offset 7
        inc     de
        ld      a,c
        and     a,#0x80
        jr      z,squid_client_time_no_dst
        ld      a,#1
squid_client_time_no_dst:
        ld      (de),a                  ; daylight_saving, offset 8
        inc     de
        ld      b,#6                    ; UTC offset + Unix seconds
squid_client_time_tail:
        ld      a,(hl)
        inc     hl
        ld      (de),a
        inc     de
        djnz    squid_client_time_tail
        ld      de,#0
        jr      squid_client_time_finish

squid_client_time_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      squid_client_time_finish
squid_client_time_argument:
        ld      de,#ERROR_ARGUMENT
squid_client_time_finish:
        ld      sp,ix
        pop     ix
        pop     hl                      ; return address
        pop     bc                      ; mode + value low
        inc     sp                      ; value high
        jp      (hl)
