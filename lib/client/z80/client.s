        ;; Minimal Z80 transaction backend for squid_client/base.h.
        ;; xcc 2.3.2 sdcccall(1): first two word arguments are HL and DE.
        .module squid_client_z80
        .optsdcc -mz80 sdcccall(1)

        .globl _squid_client_init
        .globl _squid_client_exchange
        .globl _squid_send
        .globl _squid_recv
        .globl _snet_link_is_up
        .globl _snet_link_epoch
        .globl __sdcc_call_bc

        .equ CLIENT_FD,       0
        .equ CLIENT_PACKET,   2
        .equ CLIENT_CAPACITY, 4
        .equ CLIENT_IDLE,     6
        .equ CLIENT_CONTEXT,  8

        ;; Exchange frame at IY: epoch, response size, destination word,
        ;; remaining. IX points at struct squid_client throughout.
        .equ LOCAL_EPOCH,     0
        .equ LOCAL_SIZE,      1
        .equ LOCAL_DEST,      2
        .equ LOCAL_REMAINING, 4
        .equ LOCAL_BYTES,     5

        .equ ERROR_ARGUMENT,  -1
        .equ ERROR_LINK,      -2
        .equ ERROR_IO,        -3
        .equ ERROR_PROTOCOL,  -4
        .equ ERROR_OVERFLOW,  -5
        .equ ERROR_CANCELLED, -6

        .area _CODE

;; void squid_client_init(client, fd, workspace, capacity, idle, context)
_squid_client_init::
        ld      a,h
        or      a,l
        jr      z,squid_client_init_discard
        push    ix
        ld      ix,#0
        add     ix,sp
        ld      (hl),e
        inc     hl
        ld      (hl),d
        inc     hl
        ld      e,4(ix)
        ld      d,5(ix)
        ld      (hl),e
        inc     hl
        ld      (hl),d
        inc     hl
        ld      e,6(ix)
        ld      d,7(ix)
        ld      (hl),e
        inc     hl
        ld      (hl),d
        inc     hl
        ld      e,8(ix)
        ld      d,9(ix)
        ld      (hl),e
        inc     hl
        ld      (hl),d
        inc     hl
        ld      e,10(ix)
        ld      d,11(ix)
        ld      (hl),e
        inc     hl
        ld      (hl),d
        pop     ix
squid_client_init_discard:
        pop     hl                      ; return address
        pop     bc                      ; workspace
        pop     bc                      ; capacity
        pop     bc                      ; idle
        pop     bc                      ; idle context
        jp      (hl)

;; Return 0 while the transaction may continue, otherwise a client error.
squid_client_alive:
        ld      l,CLIENT_IDLE(ix)
        ld      h,CLIENT_IDLE+1(ix)
        ld      a,h
        or      a,l
        jr      z,squid_client_alive_link
        ld      b,h
        ld      c,l
        ld      l,CLIENT_CONTEXT(ix)
        ld      h,CLIENT_CONTEXT+1(ix)
        call    __sdcc_call_bc
        ld      a,d
        or      a,e
        jr      z,squid_client_alive_link
        ld      de,#ERROR_CANCELLED
        ret
squid_client_alive_link:
        call    _snet_link_is_up
        or      a,a
        jr      z,squid_client_alive_down
        call    _snet_link_epoch
        cp      LOCAL_EPOCH(iy)
        jr      nz,squid_client_alive_down
        ld      de,#0
        ret
squid_client_alive_down:
        ld      de,#ERROR_LINK
        ret

;; Receive one byte at LOCAL_DEST, advancing that pointer on success.
squid_client_receive_one:
squid_client_receive_retry:
        ld      e,LOCAL_DEST(iy)
        ld      d,LOCAL_DEST+1(iy)
        ld      bc,#1
        push    bc
        ld      l,CLIENT_FD(ix)
        ld      h,CLIENT_FD+1(ix)
        call    _squid_recv
        bit     7,d
        jr      nz,squid_client_receive_error
        ld      a,d
        or      a,e
        jr      z,squid_client_receive_wait
        ld      l,LOCAL_DEST(iy)
        ld      h,LOCAL_DEST+1(iy)
        inc     hl
        ld      LOCAL_DEST(iy),l
        ld      LOCAL_DEST+1(iy),h
        ld      de,#0
        ret
squid_client_receive_wait:
        call    squid_client_alive
        ld      a,d
        or      a,e
        jr      z,squid_client_receive_retry
        ret
squid_client_receive_error:
        ld      de,#ERROR_IO
        ret

;; int squid_client_exchange(client, request_size)
_squid_client_exchange::
        ld      a,h
        or      a,l
        jp      z,squid_client_argument
        push    ix
        push    iy
        push    hl
        pop     ix
        ld      hl,#-LOCAL_BYTES
        add     hl,sp
        ld      sp,hl
        ld      iy,#0
        add     iy,sp

        ;; Validate workspace, capacity 1..255, and request 1..capacity.
        ld      l,CLIENT_PACKET(ix)
        ld      h,CLIENT_PACKET+1(ix)
        ld      a,h
        or      a,l
        jp      z,squid_client_argument_frame
        ld      a,CLIENT_CAPACITY+1(ix)
        or      a,a
        jp      nz,squid_client_argument_frame
        ld      a,CLIENT_CAPACITY(ix)
        or      a,a
        jp      z,squid_client_argument_frame
        ld      a,d
        or      a,a
        jp      nz,squid_client_argument_frame
        ld      a,e
        or      a,a
        jp      z,squid_client_argument_frame
        ld      b,a
        ld      a,CLIENT_CAPACITY(ix)
        cp      b
        jp      c,squid_client_argument_frame
        ld      LOCAL_REMAINING(iy),b   ; request size until send completes

        call    _snet_link_is_up
        or      a,a
        jp      z,squid_client_link_error
        call    _snet_link_epoch
        ld      LOCAL_EPOCH(iy),a

        ;; Prefix the in-place request and enqueue it in one atomic send.
        ld      l,CLIENT_PACKET(ix)
        ld      h,CLIENT_PACKET+1(ix)
        ld      a,LOCAL_REMAINING(iy)
        ld      (hl),a
        ld      c,a
        ld      b,#0
        inc     bc                      ; 255-byte request becomes word 256
        push    bc
        ex      de,hl                   ; DE = packet
        ld      l,CLIENT_FD(ix)
        ld      h,CLIENT_FD+1(ix)
        call    _squid_send
        bit     7,d
        jp      nz,squid_client_io_error

        ;; Read the one-byte response length.
        push    iy
        pop     hl
        inc     hl
        ld      LOCAL_DEST(iy),l
        ld      LOCAL_DEST+1(iy),h
        call    squid_client_receive_one
        ld      a,d
        or      a,e
        jr      nz,squid_client_finish
        ld      a,LOCAL_SIZE(iy)
        or      a,a
        jp      z,squid_client_protocol_error

        ;; The caller controls the workspace size. Oversized replies are
        ;; drained so the next transaction still starts on a packet boundary.
        ld      b,a
        ld      a,CLIENT_CAPACITY(ix)
        cp      b
        jr      c,squid_client_overflow_drain
        ld      LOCAL_REMAINING(iy),b
        ld      l,CLIENT_PACKET(ix)
        ld      h,CLIENT_PACKET+1(ix)
        ld      LOCAL_DEST(iy),l
        ld      LOCAL_DEST+1(iy),h

squid_client_body_loop:
        call    squid_client_receive_one
        ld      a,d
        or      a,e
        jr      nz,squid_client_finish
        dec     LOCAL_REMAINING(iy)
        jr      nz,squid_client_body_loop
        ld      e,LOCAL_SIZE(iy)
        ld      d,#0
        jr      squid_client_finish

squid_client_overflow_drain:
        ld      LOCAL_REMAINING(iy),b
squid_client_overflow_loop:
        ld      l,CLIENT_PACKET(ix)
        ld      h,CLIENT_PACKET+1(ix)
        ld      LOCAL_DEST(iy),l
        ld      LOCAL_DEST+1(iy),h
        call    squid_client_receive_one
        ld      a,d
        or      a,e
        jr      nz,squid_client_finish
        dec     LOCAL_REMAINING(iy)
        jr      nz,squid_client_overflow_loop
        jr      squid_client_overflow_error

squid_client_argument_frame:
        ld      de,#ERROR_ARGUMENT
        jr      squid_client_finish
squid_client_link_error:
        ld      de,#ERROR_LINK
        jr      squid_client_finish
squid_client_io_error:
        ld      de,#ERROR_IO
        jr      squid_client_finish
squid_client_protocol_error:
        ld      de,#ERROR_PROTOCOL
        jr      squid_client_finish
squid_client_overflow_error:
        ld      de,#ERROR_OVERFLOW

squid_client_finish:
        ld      sp,iy
        ld      hl,#LOCAL_BYTES
        add     hl,sp
        ld      sp,hl
        pop     iy
        pop     ix
        ret

squid_client_argument:
        ld      de,#ERROR_ARGUMENT
        ret
