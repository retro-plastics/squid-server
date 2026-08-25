        ;; Raw TCP proxy plugin public API for Z80.
        .module squid_client_z80_tcp_proxy
        .optsdcc -mz80 sdcccall(1)

        .globl _squid_client_tcp_connect
        .globl _squid_client_tcp_write
        .globl _squid_client_tcp_read
        .globl _squid_client_tcp_close
        .globl _squid_client_tcp_status
        .globl _squid_client_text_size
        .globl _squid_client_response

        .equ CLIENT_PACKET,   2
        .equ ERROR_ARGUMENT, -1
        .equ ERROR_PROTOCOL, -4
        .equ TCP_CONNECT,      1
        .equ TCP_WRITE,        2
        .equ TCP_READ,         3
        .equ TCP_CLOSE,        4
        .equ TCP_STATUS,       5
        .equ TCP_HOST_MAX,   240
        .equ TCP_WAIT_MAX, 10000

        .area _CODE

;; DE=request size, client stored at -2(ix). Return packet+1 in HL/carry clear.
tcp_request_begin:
        ld      a,-1(ix)
        or      a,-2(ix)
        jr      z,tcp_request_bad
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)                  ; packet
        ld      a,b
        or      a,c
        jr      z,tcp_request_bad
        inc     hl
        ld      a,(hl)
        inc     hl
        ld      h,(hl)
        ld      l,a                     ; capacity
        or      a,a
        sbc     hl,de
        jr      c,tcp_request_bad
        ld      h,b
        ld      l,c
        inc     hl
        or      a,a
        ret
tcp_request_bad:
        scf
        ret

;; DE=request size, A=opcode, B=minimum, HL=&response_size.
tcp_response:
        push    hl
        ld      c,a
        push    bc
        ld      l,-2(ix)
        ld      h,-1(ix)
        call    _squid_client_response
        ret

;; int squid_client_tcp_connect(client, host, port, family)
_squid_client_tcp_connect::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 host
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,7(ix)
        or      a,6(ix)                 ; family
        jp      z,tcp_connect_argument
        ld      a,5(ix)
        or      a,4(ix)                 ; port
        jp      z,tcp_connect_argument
        ld      a,#TCP_HOST_MAX
        push    af
        inc     sp
        ld      l,-4(ix)
        ld      h,-3(ix)
        call    _squid_client_text_size
        bit     7,d
        jp      nz,tcp_connect_finish
        ld      a,d
        or      a,e
        jp      z,tcp_connect_argument
        push    de                      ; -8 host size
        ld      hl,#4
        add     hl,de
        ex      de,hl
        call    tcp_request_begin
        jp      c,tcp_connect_argument
        ld      (hl),#TCP_CONNECT
        inc     hl
        ld      a,4(ix)
        ld      (hl),a
        inc     hl
        ld      a,5(ix)
        ld      (hl),a
        inc     hl
        ld      a,-8(ix)
        ld      (hl),a
        inc     hl
        ex      de,hl
        ld      l,-4(ix)
        ld      h,-3(ix)
        ld      c,-8(ix)
        ld      b,#0
        ldir
        push    ix
        pop     hl
        ld      de,#-6
        add     hl,de
        ld      e,-8(ix)
        ld      d,#0
        ld      bc,#4
        ex      de,hl
        add     hl,bc
        ex      de,hl
        ld      a,#TCP_CONNECT
        ld      b,#3
        call    tcp_response
        ld      a,d
        or      a,e
        jr      nz,tcp_connect_finish
        ld      a,-5(ix)
        or      a,a
        jr      nz,tcp_connect_protocol
        ld      a,-6(ix)
        cp      a,#3
        jr      nz,tcp_connect_protocol
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl
        inc     hl
        inc     hl
        ld      a,(hl)
        cp      a,#4
        jr      z,tcp_connect_family_ok
        cp      a,#6
        jr      nz,tcp_connect_protocol
tcp_connect_family_ok:
        ld      e,6(ix)
        ld      d,7(ix)
        ld      (de),a
        ld      de,#0
        jr      tcp_connect_finish
tcp_connect_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      tcp_connect_finish
tcp_connect_argument:
        ld      de,#ERROR_ARGUMENT
tcp_connect_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc                      ; port
        pop     bc                      ; family
        jp      (hl)

;; int squid_client_tcp_write(client, data, size, written)
_squid_client_tcp_write::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 data
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,-3(ix)
        or      a,-4(ix)
        jp      z,tcp_write_argument
        ld      a,4(ix)
        or      a,a
        jp      z,tcp_write_argument
        ld      a,6(ix)
        or      a,5(ix)
        jp      z,tcp_write_argument
        ld      e,4(ix)
        ld      d,#0
        inc     de
        inc     de
        call    tcp_request_begin
        jp      c,tcp_write_argument
        ld      (hl),#TCP_WRITE
        inc     hl
        ld      a,4(ix)
        ld      (hl),a
        inc     hl
        ex      de,hl
        ld      l,-4(ix)
        ld      h,-3(ix)
        ld      c,4(ix)
        ld      b,#0
        ldir
        push    ix
        pop     hl
        ld      de,#-6
        add     hl,de
        ld      e,4(ix)
        ld      d,#0
        inc     de
        inc     de
        ld      a,#TCP_WRITE
        ld      b,#3
        call    tcp_response
        ld      a,d
        or      a,e
        jr      nz,tcp_write_finish
        ld      a,-5(ix)
        or      a,a
        jr      nz,tcp_write_protocol
        ld      a,-6(ix)
        cp      a,#3
        jr      nz,tcp_write_protocol
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl
        inc     hl
        inc     hl
        ld      a,4(ix)
        cp      a,(hl)
        jr      c,tcp_write_protocol
        ld      e,5(ix)
        ld      d,6(ix)
        ld      a,(hl)
        ld      (de),a
        ld      de,#0
        jr      tcp_write_finish
tcp_write_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      tcp_write_finish
tcp_write_argument:
        ld      de,#ERROR_ARGUMENT
tcp_write_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc                      ; size + written low
        inc     sp                      ; written high
        jp      (hl)

;; int squid_client_tcp_read(client, maximum_wait_ms, maximum_bytes, chunk)
;; client=HL, maximum_wait_ms=DE, stack=[maximum_bytes:u8][chunk:pointer].
_squid_client_tcp_read::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 maximum wait
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,6(ix)
        or      a,5(ix)                 ; chunk
        jp      z,tcp_read_argument
        ld      l,-4(ix)
        ld      h,-3(ix)
        ld      de,#TCP_WAIT_MAX
        or      a,a
        sbc     hl,de
        jp      c,tcp_read_wait_ok
        jp      z,tcp_read_wait_ok
        jp      tcp_read_argument
tcp_read_wait_ok:
        ld      de,#4
        call    tcp_request_begin
        jp      c,tcp_read_argument
        ld      (hl),#TCP_READ
        inc     hl
        ld      a,-4(ix)
        ld      (hl),a
        inc     hl
        ld      a,-3(ix)
        ld      (hl),a
        inc     hl
        ld      a,4(ix)
        ld      (hl),a
        push    ix
        pop     hl
        ld      de,#-6
        add     hl,de
        ld      de,#4
        ld      a,#TCP_READ
        ld      b,#4
        call    tcp_response
        ld      a,d
        or      a,e
        jr      nz,tcp_read_finish

        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl                   ; packet
        inc     hl
        inc     hl                      ; flags
        ld      a,(hl)
        and     a,#0xfe
        jr      nz,tcp_read_protocol
        inc     hl
        ld      a,(hl)                  ; data size
        ld      c,a
        ld      b,#0
        ld      hl,#4
        add     hl,bc
        ld      a,h
        cp      a,-5(ix)
        jr      nz,tcp_read_protocol
        ld      a,l
        cp      a,-6(ix)
        jr      nz,tcp_read_protocol

        ;; chunk.eof and zero-copy data slice.
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl                   ; packet
        inc     hl
        inc     hl
        ld      a,(hl)                  ; flags
        and     a,#1
        ld      e,5(ix)
        ld      d,6(ix)                 ; chunk
        ld      (de),a
        inc     de
        inc     hl
        inc     hl                      ; packet+4 data
        ld      a,l
        ld      (de),a
        inc     de
        ld      a,h
        ld      (de),a
        inc     de
        dec     hl                      ; response data length at packet+3
        ld      a,(hl)
        ld      (de),a
        ld      de,#0
        jr      tcp_read_finish
tcp_read_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      tcp_read_finish
tcp_read_argument:
        ld      de,#ERROR_ARGUMENT
tcp_read_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc                      ; maximum_bytes + chunk low
        inc     sp                      ; chunk high
        jp      (hl)

;; int squid_client_tcp_close(client)
_squid_client_tcp_close::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        ld      bc,#0
        push    bc                      ; -4 response size
        ld      de,#1
        call    tcp_request_begin
        jr      c,tcp_close_argument
        ld      (hl),#TCP_CLOSE
        push    ix
        pop     hl
        ld      de,#-4
        add     hl,de
        ld      de,#1
        ld      a,#TCP_CLOSE
        ld      b,#2
        call    tcp_response
        ld      a,d
        or      a,e
        jr      nz,tcp_close_finish
        ld      a,-3(ix)
        or      a,a
        jr      nz,tcp_close_protocol
        ld      a,-4(ix)
        cp      a,#2
        jr      nz,tcp_close_protocol
        ld      de,#0
        jr      tcp_close_finish
tcp_close_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      tcp_close_finish
tcp_close_argument:
        ld      de,#ERROR_ARGUMENT
tcp_close_finish:
        ld      sp,ix
        pop     ix
        ret

;; int squid_client_tcp_status(client, connected)
_squid_client_tcp_status::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 connected
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,-3(ix)
        or      a,-4(ix)
        jr      z,tcp_status_argument
        ld      de,#1
        call    tcp_request_begin
        jr      c,tcp_status_argument
        ld      (hl),#TCP_STATUS
        push    ix
        pop     hl
        ld      de,#-6
        add     hl,de
        ld      de,#1
        ld      a,#TCP_STATUS
        ld      b,#3
        call    tcp_response
        ld      a,d
        or      a,e
        jr      nz,tcp_status_finish
        ld      a,-5(ix)
        or      a,a
        jr      nz,tcp_status_protocol
        ld      a,-6(ix)
        cp      a,#3
        jr      nz,tcp_status_protocol
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl
        inc     hl
        inc     hl
        ld      a,(hl)
        cp      a,#2
        jr      nc,tcp_status_protocol
        ld      e,-4(ix)
        ld      d,-3(ix)
        ld      (de),a
        ld      de,#0
        jr      tcp_status_finish
tcp_status_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      tcp_status_finish
tcp_status_argument:
        ld      de,#ERROR_ARGUMENT
tcp_status_finish:
        ld      sp,ix
        pop     ix
        ret
