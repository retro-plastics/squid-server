        ;; ZX Spectrum 48K + Interface 1 software serial platform.
        ;; 115200 baud, 8 data bits, no parity, 2 stop bits, RTS/CTS.
        ;;
        ;; The cycle-counted receive technique and 20-byte CTS overrun buffer
        ;; are based on Tomaž Štih, "The YX Kernel for ZX Spectrum", B.Sc.
        ;; thesis (2016), sections 9.3.1-9.3.7. Used with author permission.
        .module squid_client_spectrum_if1
        .optsdcc -mz80 sdcccall(1)

        .globl _squid_client_spectrum_if1_send_char
        .globl _squid_client_spectrum_if1_recv_char
        .globl _squid_client_spectrum_if1_get_tick
        .globl _squid_client_spectrum_if1_reset

        .equ IF1_CONTROL,       0xef
        .equ IF1_DATA,          0xf7
        .equ IF1_CTS_ON,        0xff
        .equ IF1_CTS_OFF,       0xef
        .equ IF1_START,         0xff
        .equ IF1_STOP,          0xfe
        .equ IF1_RECEIVE_BYTES, 20
        .equ SPECTRUM_FRAMES,   0x5c78

        .area _BSS
if1_receive_buffer:
        .ds     IF1_RECEIVE_BYTES
if1_receive_position:
        .ds     1
if1_receive_count:
        .ds     1
if1_saved_sp:
        .ds     2
if1_restore_interrupts:
        .ds     1

        .area _CODE

;; Preserve the caller's interrupt state around a cycle-counted operation.
if1_disable_interrupts:
        ld      a,i
        di
        ld      a,#0
        jp      po,if1_interrupt_state_saved
        inc     a
if1_interrupt_state_saved:
        ld      (if1_restore_interrupts),a
        ret

if1_restore_interrupt_state:
        ld      a,(if1_restore_interrupts)
        or      a,a
        ret     z
        ei
        ret

;; int send_char(uint8_t value); value arrives in A, int returns in DE.
_squid_client_spectrum_if1_send_char::
        ld      e,a
        call    if1_disable_interrupts

        ;; Stop inbound bytes while this half-duplex burst is transmitted.
        ld      a,#IF1_CTS_OFF
        out     (IF1_CONTROL),a

        ;; IF1 levels are inverted. A=ff is the start/logical-zero level;
        ;; A=fe is logical one and the stop level. Each data interval is 31T.
        ld      b,#1
        ld      c,#IF1_START
        ld      d,e
        ld      a,c
        out     (IF1_DATA),a

        srl     d
        rla
        xor     a,b
        nop
        out     (IF1_DATA),a
        srl     d
        rla
        xor     a,b
        nop
        out     (IF1_DATA),a
        srl     d
        rla
        xor     a,b
        nop
        out     (IF1_DATA),a
        srl     d
        rla
        xor     a,b
        nop
        out     (IF1_DATA),a
        srl     d
        rla
        xor     a,b
        nop
        out     (IF1_DATA),a
        srl     d
        rla
        xor     a,b
        nop
        out     (IF1_DATA),a
        srl     d
        rla
        xor     a,b
        nop
        out     (IF1_DATA),a
        srl     d
        rla
        xor     a,b
        nop
        out     (IF1_DATA),a

        ;; Start the stop level after 30T, then retain it while returning.
        ld      a,#IF1_STOP
        inc     d
        nop
        nop
        out     (IF1_DATA),a

        call    if1_restore_interrupt_state
        ld      de,#0
        ret

;; Refill the private 20-byte buffer. The host may finish a complete UART FIFO
;; after CTS changes. This backend explicitly negotiates the compatible
;; 16-byte payload, whose compact libsquid wire frame is at most 20 bytes.
if1_receive_refill:
        call    if1_disable_interrupts
        xor     a
        ld      (if1_receive_position),a
        ld      (if1_receive_count),a

        ld      (if1_saved_sp),sp
        ld      hl,#if1_receive_buffer
        ld      b,#IF1_RECEIVE_BYTES
        ld      c,#IF1_DATA
        ld      de,#0x8000
        ld      sp,#if1_start_returns
        ld      a,#IF1_CTS_ON
        out     (IF1_CONTROL),a

if1_start_bit:
        ;; A taken RET uses the next table word as a constant-time branch.
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m
        in      a,(c)
        ret     m

        ;; No start bit: stop the sender and publish the bytes collected.
        ld      a,#IF1_CTS_OFF
        out     (IF1_CONTROL),a
        jp      if1_receive_publish

if1_data_bits:
        ;; Centre the first sample after the variable start-bit detection.
        cp      #0
        dec     a
        nop
        nop

        ;; Eight 31T sample intervals, least-significant bit first.
        in      a,(IF1_DATA)
        rla
        rr      e
        nop
        in      a,(IF1_DATA)
        rla
        rr      e
        nop
        in      a,(IF1_DATA)
        rla
        rr      e
        nop
        in      a,(IF1_DATA)
        rla
        rr      e
        cp      #0
        in      a,(IF1_DATA)
        rla
        rr      e
        cp      #0
        in      a,(IF1_DATA)
        rla
        rr      e
        cp      #0
        in      a,(IF1_DATA)
        rla
        rr      e
        cp      #0
        in      a,(IF1_DATA)
        rla
        rr      e

        ld      a,e
        cpl
        ld      (hl),a
        inc     hl
        ld      a,#IF1_CTS_OFF
        out     (IF1_CONTROL),a
        djnz    if1_receive_more
        jr      if1_receive_publish

if1_receive_more:
        jp      if1_start_bit

if1_receive_publish:
        ld      sp,(if1_saved_sp)
        ld      a,#IF1_RECEIVE_BYTES
        sub     a,b
        ld      (if1_receive_count),a
        call    if1_restore_interrupt_state
        ret

if1_start_returns:
        .dw     if1_data_bits,if1_data_bits,if1_data_bits,if1_data_bits
        .dw     if1_data_bits,if1_data_bits,if1_data_bits,if1_data_bits
        .dw     if1_data_bits,if1_data_bits,if1_data_bits,if1_data_bits
        .dw     if1_data_bits,if1_data_bits,if1_data_bits,if1_data_bits
        .dw     if1_data_bits,if1_data_bits,if1_data_bits,if1_data_bits

;; int recv_char(void); returns the next byte in DE, or -1 when none is ready.
_squid_client_spectrum_if1_recv_char::
        ld      a,(if1_receive_position)
        ld      c,a
        ld      a,(if1_receive_count)
        cp      a,c
        jr      nz,if1_receive_ready
        call    if1_receive_refill
        ld      a,(if1_receive_count)
        or      a,a
        jr      z,if1_receive_empty
        ld      c,#0

if1_receive_ready:
        ld      l,c
        ld      h,#0
        ld      de,#if1_receive_buffer
        add     hl,de
        ld      e,(hl)
        ld      d,#0
        inc     c
        ld      a,c
        ld      (if1_receive_position),a
        ret

if1_receive_empty:
        ld      de,#-1
        ret

;; uint8_t get_tick(void); Spectrum ROM FRAMES low byte advances at 50 Hz.
_squid_client_spectrum_if1_get_tick::
        ld      a,(SPECTRUM_FRAMES)
        ret

_squid_client_spectrum_if1_reset::
        xor     a
        ld      (if1_receive_position),a
        ld      (if1_receive_count),a
        ld      a,#IF1_CTS_OFF
        out     (IF1_CONTROL),a
        ld      a,#IF1_STOP
        out     (IF1_DATA),a
        ret
