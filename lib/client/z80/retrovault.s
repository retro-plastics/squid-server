        ;; Retro Vault plugin public API for Z80.
        .module squid_client_z80_retrovault
        .optsdcc -mz80 sdcccall(1)

        .globl _squid_client_retro_list
        .globl _squid_client_retro_search
        .globl _squid_client_retro_list_next
        .globl _squid_client_retro_info
        .globl _squid_client_retro_info_next
        .globl _squid_client_retro_download
        .globl _squid_client_text_size
        .globl _squid_client_response

        .equ CLIENT_PACKET,   2
        .equ ERROR_ARGUMENT, -1
        .equ ERROR_PROTOCOL, -4
        .equ RETRO_LIST,       1
        .equ RETRO_SEARCH,     2
        .equ RETRO_INFO,       3
        .equ RETRO_DOWNLOAD,   4

        .area _CODE

;; HL=text. Return its counted u8 length in DE or ERROR_ARGUMENT.
retrovault_text_size:
        ld      a,#255
        push    af
        inc     sp
        call    _squid_client_text_size
        ret

;; DE=request size. Return HL=packet+1 and carry clear, or carry set.
;; Public request functions store client at -2(ix).
retrovault_request_begin:
        ld      a,-1(ix)
        or      a,-2(ix)
        jr      z,retrovault_request_bad
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)                  ; packet
        ld      a,b
        or      a,c
        jr      z,retrovault_request_bad
        inc     hl
        ld      a,(hl)
        inc     hl
        ld      h,(hl)
        ld      l,a                     ; capacity
        or      a,a
        sbc     hl,de
        jr      c,retrovault_request_bad
        ld      h,b
        ld      l,c
        inc     hl
        or      a,a
        ret
retrovault_request_bad:
        scf
        ret

;; DE=request size, A=opcode, B=minimum, HL=&response_size.
retrovault_response:
        push    hl
        ld      c,a
        push    bc
        ld      l,-2(ix)
        ld      h,-1(ix)
        call    _squid_client_response
        ret

;; DE=page. Store cursor, count, record pointer, and response end. The page and
;; info structs deliberately have the same seven-byte layout.
retrovault_store_page:
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        ld      h,b
        ld      l,c
        inc     hl
        inc     hl                      ; response byte 2
        ld      bc,#3
        ldir                            ; cursor + count
        ld      a,l                    ; packet+5
        ld      (de),a
        inc     de
        ld      a,h
        ld      (de),a
        inc     de
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        ld      l,c
        ld      h,b
        ld      c,-6(ix)
        ld      b,-5(ix)
        add     hl,bc
        ld      a,l
        ld      (de),a
        inc     de
        ld      a,h
        ld      (de),a
        ret

;; int squid_client_retro_list(client, platform, model, cursor, page)
_squid_client_retro_list::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 platform
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,9(ix)
        or      a,8(ix)                 ; page
        jp      z,retrovault_list_argument
        ld      l,-4(ix)
        ld      h,-3(ix)
        call    retrovault_text_size
        bit     7,d
        jp      nz,retrovault_list_finish
        push    de                      ; -8 platform size
        ld      l,4(ix)
        ld      h,5(ix)
        call    retrovault_text_size
        bit     7,d
        jp      nz,retrovault_list_finish
        push    de                      ; -10 model size

        ld      hl,#4
        ld      e,-8(ix)
        ld      d,#0
        add     hl,de
        ld      a,-10(ix)
        or      a,a
        jr      z,retrovault_list_size_done
        inc     hl
        ld      e,a
        ld      d,#0
        add     hl,de
retrovault_list_size_done:
        ex      de,hl                   ; request size
        call    retrovault_request_begin
        jp      c,retrovault_list_argument
        ld      (hl),#RETRO_LIST
        inc     hl
        ld      a,6(ix)
        ld      (hl),a
        inc     hl
        ld      a,7(ix)
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
        ld      a,b
        or      a,c
        jr      z,retrovault_list_platform_done
        ldir
retrovault_list_platform_done:
        ld      a,-10(ix)
        or      a,a
        jr      z,retrovault_list_request_done
        ex      de,hl
        ld      (hl),a
        inc     hl
        ex      de,hl
        ld      l,4(ix)
        ld      h,5(ix)
        ld      c,-10(ix)
        ld      b,#0
        ldir
retrovault_list_request_done:
        push    ix
        pop     hl
        ld      de,#-6
        add     hl,de
        ld      e,-8(ix)
        ld      d,#0
        ld      bc,#4
        ex      de,hl
        add     hl,bc
        ld      a,-10(ix)
        or      a,a
        jr      z,retrovault_list_call
        inc     hl
        ld      c,a
        ld      b,#0
        add     hl,bc
retrovault_list_call:
        ex      de,hl
        ld      a,#RETRO_LIST
        ld      b,#5
        call    retrovault_response
        ld      a,d
        or      a,e
        jr      nz,retrovault_list_finish
        ld      e,8(ix)
        ld      d,9(ix)
        call    retrovault_store_page
        ld      de,#0
        jr      retrovault_list_finish
retrovault_list_argument:
        ld      de,#ERROR_ARGUMENT
retrovault_list_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc                      ; model
        pop     bc                      ; cursor
        pop     bc                      ; page
        jp      (hl)

;; int squid_client_retro_search(client, platform, model, query, cursor, page)
_squid_client_retro_search::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 platform
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,11(ix)
        or      a,10(ix)                ; page
        jp      z,retrovault_search_argument
        ld      l,-4(ix)
        ld      h,-3(ix)
        call    retrovault_text_size
        bit     7,d
        jp      nz,retrovault_search_finish
        push    de                      ; -8 platform size
        ld      l,4(ix)
        ld      h,5(ix)
        call    retrovault_text_size
        bit     7,d
        jp      nz,retrovault_search_finish
        push    de                      ; -10 model size
        ld      l,6(ix)
        ld      h,7(ix)
        call    retrovault_text_size
        bit     7,d
        jp      nz,retrovault_search_finish
        push    de                      ; -12 query size

        ld      hl,#5
        ld      e,-8(ix)
        ld      d,#0
        add     hl,de
        ld      e,-12(ix)
        ld      d,#0
        add     hl,de
        ld      a,-10(ix)
        or      a,a
        jr      z,retrovault_search_size_done
        inc     hl
        ld      e,a
        ld      d,#0
        add     hl,de
retrovault_search_size_done:
        ex      de,hl
        call    retrovault_request_begin
        jp      c,retrovault_search_argument
        ld      (hl),#RETRO_SEARCH
        inc     hl
        ld      a,8(ix)
        ld      (hl),a
        inc     hl
        ld      a,9(ix)
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
        ld      a,b
        or      a,c
        jr      z,retrovault_search_platform_done
        ldir
retrovault_search_platform_done:
        ex      de,hl
        ld      a,-12(ix)
        ld      (hl),a
        inc     hl
        ex      de,hl
        ld      l,6(ix)
        ld      h,7(ix)
        ld      c,-12(ix)
        ld      b,#0
        ld      a,b
        or      a,c
        jr      z,retrovault_search_query_done
        ldir
retrovault_search_query_done:
        ld      a,-10(ix)
        or      a,a
        jr      z,retrovault_search_request_done
        ex      de,hl
        ld      (hl),a
        inc     hl
        ex      de,hl
        ld      l,4(ix)
        ld      h,5(ix)
        ld      c,-10(ix)
        ld      b,#0
        ldir
retrovault_search_request_done:
        push    ix
        pop     hl
        ld      de,#-6
        add     hl,de
        ld      e,-8(ix)
        ld      d,#0
        ld      c,-12(ix)
        ld      b,#0
        ex      de,hl
        add     hl,bc
        ld      bc,#5
        add     hl,bc
        ld      a,-10(ix)
        or      a,a
        jr      z,retrovault_search_call
        inc     hl
        ld      c,a
        ld      b,#0
        add     hl,bc
retrovault_search_call:
        ex      de,hl
        ld      a,#RETRO_SEARCH
        ld      b,#5
        call    retrovault_response
        ld      a,d
        or      a,e
        jr      nz,retrovault_search_finish
        ld      e,10(ix)
        ld      d,11(ix)
        call    retrovault_store_page
        ld      de,#0
        jr      retrovault_search_finish
retrovault_search_argument:
        ld      de,#ERROR_ARGUMENT
retrovault_search_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc                      ; model
        pop     bc                      ; query
        pop     bc                      ; cursor
        pop     bc                      ; page
        jp      (hl)

;; int squid_client_retro_list_next(page, entry)
_squid_client_retro_list_next::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 page
        push    de                      ; -4 entry
        ld      a,-1(ix)
        or      a,-2(ix)
        jp      z,retrovault_list_next_argument
        ld      a,-3(ix)
        or      a,-4(ix)
        jp      z,retrovault_list_next_argument
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      a,(hl)                  ; entries_left
        or      a,a
        jr      nz,retrovault_list_next_item
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        ld      a,e
        cp      a,c
        jp      nz,retrovault_list_next_protocol
        ld      a,d
        cp      a,b
        jp      nz,retrovault_list_next_protocol
        ld      de,#0
        jp      retrovault_list_next_finish

retrovault_list_next_item:
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)                  ; current
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)                  ; end
        ld      a,d
        or      a,e
        jp      z,retrovault_list_next_protocol
        ld      a,b
        or      a,c
        jp      z,retrovault_list_next_protocol
        push    de                      ; -6 current
        push    bc                      ; -8 end
        ld      h,b
        ld      l,c
        or      a,a
        sbc     hl,de                   ; remaining
        jp      c,retrovault_list_next_protocol
        ld      de,#2
        or      a,a
        sbc     hl,de                   ; remaining-2
        jp      c,retrovault_list_next_protocol
        ld      e,-6(ix)
        ld      d,-5(ix)
        ex      de,hl                   ; HL=current, DE=remaining-2
        ld      c,(hl)                  ; id size
        ld      b,#0
        push    bc                      ; -10 id size
        ex      de,hl
        or      a,a
        sbc     hl,bc
        jp      c,retrovault_list_next_protocol

        ;; Locate and validate the name.
        ld      l,-6(ix)
        ld      h,-5(ix)
        inc     hl
        ld      e,-10(ix)
        ld      d,#0
        add     hl,de                   ; name length byte
        ld      c,(hl)
        ld      b,#0
        push    bc                      ; -12 name size
        ;; remaining-2-id is still needed; recompute simply from end-current.
        ld      l,-8(ix)
        ld      h,-7(ix)
        ld      e,-6(ix)
        ld      d,-5(ix)
        or      a,a
        sbc     hl,de
        ld      e,-10(ix)
        ld      d,#0
        ld      bc,#2
        or      a,a
        sbc     hl,bc
        or      a,a
        sbc     hl,de
        ld      e,-12(ix)
        ld      d,#0
        or      a,a
        sbc     hl,de
        jp      c,retrovault_list_next_protocol

        ;; entry.id = { current+1, id_size }
        ld      l,-4(ix)
        ld      h,-3(ix)                ; entry
        ld      e,-6(ix)
        ld      d,-5(ix)
        inc     de
        ld      (hl),e
        inc     hl
        ld      (hl),d
        inc     hl
        ld      a,-10(ix)
        ld      (hl),a
        inc     hl
        ;; entry.name = { current+2+id_size, name_size }
        inc     de                      ; current+2
        push    hl                      ; destination entry.name.data
        ex      de,hl
        ld      e,-10(ix)
        ld      d,#0
        add     hl,de
        ex      de,hl                   ; DE=name data
        pop     hl
        ld      (hl),e
        inc     hl
        ld      (hl),d
        inc     hl
        ld      a,-12(ix)
        ld      (hl),a

        ;; next = name data + name size.
        ex      de,hl
        ld      e,-12(ix)
        ld      d,#0
        add     hl,de
        ex      de,hl                   ; DE=next
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        dec     (hl)
        inc     hl
        ld      (hl),e
        inc     hl
        ld      (hl),d
        ld      de,#1
        jr      retrovault_list_next_finish

retrovault_list_next_protocol:
        ld      sp,ix
        ld      de,#ERROR_PROTOCOL
        jr      retrovault_list_next_epilogue
retrovault_list_next_argument:
        ld      de,#ERROR_ARGUMENT
retrovault_list_next_finish:
        ld      sp,ix
retrovault_list_next_epilogue:
        pop     ix
        ret

;; int squid_client_retro_info(client, package_id, cursor, page)
_squid_client_retro_info::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 package id
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,7(ix)
        or      a,6(ix)                 ; page
        jp      z,retrovault_info_argument
        ld      l,-4(ix)
        ld      h,-3(ix)
        call    retrovault_text_size
        bit     7,d
        jp      nz,retrovault_info_finish
        ld      a,d
        or      a,e
        jp      z,retrovault_info_argument
        push    de                      ; -8 id size
        ld      hl,#4
        add     hl,de
        ex      de,hl
        call    retrovault_request_begin
        jp      c,retrovault_info_argument
        ld      (hl),#RETRO_INFO
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
        ld      a,#RETRO_INFO
        ld      b,#5
        call    retrovault_response
        ld      a,d
        or      a,e
        jr      nz,retrovault_info_finish
        ld      e,6(ix)
        ld      d,7(ix)
        call    retrovault_store_page
        ld      de,#0
        jr      retrovault_info_finish
retrovault_info_argument:
        ld      de,#ERROR_ARGUMENT
retrovault_info_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc                      ; cursor
        pop     bc                      ; page
        jp      (hl)

;; int squid_client_retro_info_next(page, value)
_squid_client_retro_info_next::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 page
        push    de                      ; -4 value
        ld      a,-1(ix)
        or      a,-2(ix)
        jp      z,retrovault_info_next_argument
        ld      a,-3(ix)
        or      a,-4(ix)
        jp      z,retrovault_info_next_argument
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      a,(hl)                  ; values_left
        or      a,a
        jr      nz,retrovault_info_next_item
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        ld      a,e
        cp      a,c
        jr      nz,retrovault_info_next_protocol
        ld      a,d
        cp      a,b
        jr      nz,retrovault_info_next_protocol
        ld      de,#0
        jr      retrovault_info_next_finish

retrovault_info_next_item:
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)                  ; current
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)                  ; end
        ld      a,d
        or      a,e
        jr      z,retrovault_info_next_protocol
        ld      a,b
        or      a,c
        jr      z,retrovault_info_next_protocol
        push    de                      ; -6 current
        ld      h,b
        ld      l,c
        or      a,a
        sbc     hl,de                   ; remaining
        jr      c,retrovault_info_next_protocol
        ld      bc,#2
        or      a,a
        sbc     hl,bc
        jr      c,retrovault_info_next_protocol
        ld      e,-6(ix)
        ld      d,-5(ix)
        ex      de,hl
        inc     hl
        ld      c,(hl)                  ; value size
        ld      b,#0
        ex      de,hl                   ; HL=remaining-2
        or      a,a
        sbc     hl,bc
        jr      c,retrovault_info_next_protocol

        ld      c,-6(ix)
        ld      b,-5(ix)                ; current
        ld      l,-4(ix)
        ld      h,-3(ix)                ; value output
        ld      a,(bc)                  ; type
        ld      (hl),a
        inc     bc
        ld      a,(bc)                  ; size
        inc     bc                      ; data
        inc     hl
        ld      (hl),c
        inc     hl
        ld      (hl),b
        inc     hl
        ld      (hl),a
        ld      e,a
        ld      d,#0
        ld      h,b
        ld      l,c
        add     hl,de                   ; next
        ex      de,hl
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        dec     (hl)
        inc     hl
        ld      (hl),e
        inc     hl
        ld      (hl),d
        ld      de,#1
        jr      retrovault_info_next_finish

retrovault_info_next_protocol:
        ld      sp,ix
        ld      de,#ERROR_PROTOCOL
        jr      retrovault_info_next_epilogue
retrovault_info_next_argument:
        ld      de,#ERROR_ARGUMENT
retrovault_info_next_finish:
        ld      sp,ix
retrovault_info_next_epilogue:
        pop     ix
        ret

;; int squid_client_retro_download(client, package_id, download_id,
;;                                  offset, maximum_bytes, chunk)
_squid_client_retro_download::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 package id
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,12(ix)
        or      a,11(ix)                ; chunk
        jp      z,retrovault_download_argument
        ld      l,-4(ix)
        ld      h,-3(ix)
        call    retrovault_text_size
        bit     7,d
        jp      nz,retrovault_download_finish
        ld      a,d
        or      a,e
        jp      z,retrovault_download_argument
        push    de                      ; -8 package size
        ld      l,4(ix)
        ld      h,5(ix)
        call    retrovault_text_size
        bit     7,d
        jp      nz,retrovault_download_finish
        ld      a,d
        or      a,e
        jp      z,retrovault_download_argument
        push    de                      ; -10 download size
        ld      hl,#8
        ld      e,-8(ix)
        ld      d,#0
        add     hl,de
        ld      e,-10(ix)
        ld      d,#0
        add     hl,de
        ex      de,hl
        call    retrovault_request_begin
        jp      c,retrovault_download_argument

        ld      (hl),#RETRO_DOWNLOAD
        inc     hl
        ld      a,6(ix)
        ld      (hl),a
        inc     hl
        ld      a,7(ix)
        ld      (hl),a
        inc     hl
        ld      a,8(ix)
        ld      (hl),a
        inc     hl
        ld      a,9(ix)
        ld      (hl),a
        inc     hl
        ld      a,10(ix)
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
        ex      de,hl
        ld      a,-10(ix)
        ld      (hl),a
        inc     hl
        ex      de,hl
        ld      l,4(ix)
        ld      h,5(ix)
        ld      c,-10(ix)
        ld      b,#0
        ldir

        push    ix
        pop     hl
        ld      de,#-6
        add     hl,de
        ld      e,-8(ix)
        ld      d,#0
        ld      c,-10(ix)
        ld      b,#0
        ex      de,hl
        add     hl,bc
        ld      bc,#8
        add     hl,bc
        ex      de,hl
        ld      a,#RETRO_DOWNLOAD
        ld      b,#11
        call    retrovault_response
        ld      a,d
        or      a,e
        jr      nz,retrovault_download_finish

        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl                   ; packet
        ld      de,#10
        add     hl,de
        ld      a,(hl)
        ld      c,a
        ld      b,#0
        ld      hl,#11
        add     hl,bc
        ld      a,h
        cp      a,-5(ix)
        jr      nz,retrovault_download_protocol
        ld      a,l
        cp      a,-6(ix)
        jr      nz,retrovault_download_protocol

        ld      e,11(ix)
        ld      d,12(ix)                ; chunk
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        ld      h,b
        ld      l,c
        inc     hl
        inc     hl
        ld      bc,#8
        ldir
        inc     hl                      ; data at packet+11
        ld      a,l
        ld      (de),a
        inc     de
        ld      a,h
        ld      (de),a
        inc     de
        dec     hl
        ld      a,(hl)
        ld      (de),a
        ld      de,#0
        jr      retrovault_download_finish
retrovault_download_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      retrovault_download_finish
retrovault_download_argument:
        ld      de,#ERROR_ARGUMENT
retrovault_download_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc
        pop     bc
        pop     bc
        pop     bc
        inc     sp                      ; 9 packed argument bytes
        jp      (hl)
