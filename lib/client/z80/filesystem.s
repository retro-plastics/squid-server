        ;; Rooted filesystem plugin public API for Z80.
        .module squid_client_z80_filesystem
        .optsdcc -mz80 sdcccall(1)

        .globl _squid_client_fs_stat
        .globl _squid_client_fs_list
        .globl _squid_client_fs_list_next
        .globl _squid_client_fs_read
        .globl _squid_client_fs_write
        .globl _squid_client_fs_mkdir
        .globl _squid_client_fs_delete
        .globl _squid_client_fs_rename
        .globl _squid_client_text_size
        .globl _squid_client_response

        .equ CLIENT_PACKET,   2
        .equ CLIENT_CAPACITY, 4
        .equ ERROR_ARGUMENT, -1
        .equ ERROR_PROTOCOL, -4
        .equ FS_PATH_MAX,    240
        .equ FS_STAT,          1
        .equ FS_LIST,          2
        .equ FS_READ,          3
        .equ FS_WRITE,         4
        .equ FS_MKDIR,         5
        .equ FS_DELETE,        6
        .equ FS_RENAME,        7

        .area _CODE

;; All request helpers below assume IX is the public function frame and that
;; client and path pointers are stored at -2 and -4 respectively.

;; A=allow empty. Return path byte count in DE or ERROR_ARGUMENT.
filesystem_path_size:
        push    af                      ; preserve allow-empty in B
        ld      a,#FS_PATH_MAX
        push    af
        inc     sp                      ; packed u8 argument
        ld      l,-4(ix)
        ld      h,-3(ix)
        call    _squid_client_text_size
        pop     bc                      ; B=allow-empty
        bit     7,d
        ret     nz
        ld      a,d
        or      a,e
        ret     nz
        ld      a,b
        or      a,a
        ret     nz
        ld      de,#ERROR_ARGUMENT
        ret

;; DE=request size. Return HL=client->packet+1 with carry clear, or carry set.
filesystem_request_begin:
        ld      a,-1(ix)
        or      a,-2(ix)
        jr      z,filesystem_request_bad
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      c,(hl)                  ; packet
        inc     hl
        ld      b,(hl)
        ld      a,b
        or      a,c
        jr      z,filesystem_request_bad
        inc     hl
        ld      a,(hl)                  ; capacity
        inc     hl
        ld      h,(hl)
        ld      l,a
        or      a,a
        sbc     hl,de
        jr      c,filesystem_request_bad
        ld      h,b
        ld      l,c
        inc     hl
        or      a,a                     ; clear carry
        ret
filesystem_request_bad:
        scf
        ret

;; DE=request size, A=opcode, B=minimum response, HL=&response_size or zero.
filesystem_response:
        push    hl
        ld      c,a
        push    bc
        ld      l,-2(ix)
        ld      h,-1(ix)
        call    _squid_client_response
        ret

;; int squid_client_fs_stat(client, path, value)
_squid_client_fs_stat::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 path
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,5(ix)
        or      a,4(ix)                 ; value
        jp      z,filesystem_stat_argument
        ld      a,#1
        call    filesystem_path_size
        bit     7,d
        jp      nz,filesystem_stat_finish
        push    de                      ; -8 path size
        ld      hl,#2
        add     hl,de
        ex      de,hl                   ; DE=request size
        call    filesystem_request_begin
        jp      c,filesystem_stat_argument
        ld      (hl),#FS_STAT
        inc     hl
        ld      a,-8(ix)
        ld      (hl),a
        inc     hl
        ex      de,hl                   ; DE=destination
        ld      l,-4(ix)
        ld      h,-3(ix)                ; HL=path
        ld      c,-8(ix)
        ld      b,#0
        ld      a,b
        or      a,c
        jr      z,filesystem_stat_path_done
        ldir
filesystem_stat_path_done:
        push    ix
        pop     hl
        ld      de,#-6
        add     hl,de
        ld      de,#0
        ld      e,-8(ix)
        inc     de
        inc     de
        ld      a,#FS_STAT
        ld      b,#13
        call    filesystem_response
        ld      a,d
        or      a,e
        jr      nz,filesystem_stat_finish
        ld      a,-5(ix)
        or      a,a
        jr      nz,filesystem_stat_protocol
        ld      a,-6(ix)
        cp      a,#13
        jr      nz,filesystem_stat_protocol

        ld      e,4(ix)                 ; destination value
        ld      d,5(ix)
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
        ld      bc,#11                  ; packed struct equals wire fields
        ldir
        ld      de,#0
        jr      filesystem_stat_finish
filesystem_stat_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      filesystem_stat_finish
filesystem_stat_argument:
        ld      de,#ERROR_ARGUMENT
filesystem_stat_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc                      ; value pointer
        jp      (hl)

;; int squid_client_fs_list(client, path, cursor, page)
_squid_client_fs_list::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 path
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,7(ix)
        or      a,6(ix)                 ; page
        jp      z,filesystem_list_argument
        ld      a,#1
        call    filesystem_path_size
        bit     7,d
        jp      nz,filesystem_list_finish
        push    de                      ; -8 path size
        ld      hl,#4
        add     hl,de
        ex      de,hl
        call    filesystem_request_begin
        jp      c,filesystem_list_argument
        ld      (hl),#FS_LIST
        inc     hl
        ld      a,4(ix)                 ; cursor
        ld      (hl),a
        inc     hl
        ld      a,5(ix)
        ld      (hl),a
        inc     hl
        ld      a,-8(ix)
        ld      (hl),a
        inc     hl
        ex      de,hl                   ; DE=destination
        ld      l,-4(ix)
        ld      h,-3(ix)
        ld      c,-8(ix)
        ld      b,#0
        ld      a,b
        or      a,c
        jr      z,filesystem_list_path_done
        ldir
filesystem_list_path_done:
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
        ld      a,#FS_LIST
        ld      b,#5
        call    filesystem_response
        ld      a,d
        or      a,e
        jr      nz,filesystem_list_finish

        ;; next_cursor and entry_count are already packed for the public page.
        ld      e,6(ix)
        ld      d,7(ix)                 ; page
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)                  ; packet
        ld      h,b
        ld      l,c
        inc     hl
        inc     hl                      ; response byte 2
        ld      bc,#3
        ldir                            ; page cursor + count
        ;; DE now points to page->next, HL to packet+5.
        ld      a,l
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
        add     hl,bc                   ; response end
        ld      a,l
        ld      (de),a
        inc     de
        ld      a,h
        ld      (de),a
        ld      de,#0
        jr      filesystem_list_finish
filesystem_list_argument:
        ld      de,#ERROR_ARGUMENT
filesystem_list_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc                      ; cursor
        pop     bc                      ; page
        jp      (hl)

;; int squid_client_fs_list_next(page, entry)
_squid_client_fs_list_next::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 page
        push    de                      ; -4 entry
        ld      a,-1(ix)
        or      a,-2(ix)
        jp      z,filesystem_list_next_argument
        ld      a,-3(ix)
        or      a,-4(ix)
        jp      z,filesystem_list_next_argument
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      a,(hl)                  ; entries_left
        or      a,a
        jr      nz,filesystem_list_next_item

        inc     hl                      ; next pointer
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        inc     hl                      ; end pointer
        ld      c,(hl)
        inc     hl
        ld      b,(hl)
        ld      a,e
        cp      a,c
        jr      nz,filesystem_list_next_protocol
        ld      a,d
        cp      a,b
        jr      nz,filesystem_list_next_protocol
        ld      de,#0
        jr      filesystem_list_next_finish

filesystem_list_next_item:
        inc     hl                      ; page->next
        ld      e,(hl)
        inc     hl
        ld      d,(hl)                  ; current
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)                  ; end
        ld      a,d
        or      a,e
        jr      z,filesystem_list_next_protocol
        ld      a,b
        or      a,c
        jr      z,filesystem_list_next_protocol
        push    de                      ; -6 current
        push    bc                      ; -8 end
        ld      h,b
        ld      l,c
        or      a,a
        sbc     hl,de                   ; remaining=end-current
        jr      c,filesystem_list_next_protocol
        ld      de,#6
        or      a,a
        sbc     hl,de                   ; remaining after fixed fields
        jr      c,filesystem_list_next_protocol

        ld      e,-6(ix)
        ld      d,-5(ix)
        ex      de,hl                   ; HL=current, DE=remaining-6
        inc     hl
        ld      c,(hl)                  ; name size
        ld      b,#0
        push    bc                      ; -10 name size
        ex      de,hl                   ; HL=remaining-6
        or      a,a
        sbc     hl,bc
        jr      c,filesystem_list_next_protocol

        ;; entry.type and entry.name
        ld      c,-6(ix)
        ld      b,-5(ix)                ; current
        ld      l,-4(ix)
        ld      h,-3(ix)                ; entry
        ld      a,(bc)
        ld      (hl),a
        inc     bc
        ld      a,(bc)                  ; name size
        inc     bc                      ; name data
        inc     hl
        ld      (hl),c
        inc     hl
        ld      (hl),b
        inc     hl
        ld      a,-10(ix)
        ld      (hl),a
        inc     hl                      ; entry size destination
        push    hl
        ld      l,c
        ld      h,b
        ld      e,-10(ix)
        ld      d,#0
        add     hl,de                   ; current+2+name
        pop     de
        ld      b,#4
filesystem_list_next_size:
        ld      a,(hl)
        inc     hl
        ld      (de),a
        inc     de
        djnz    filesystem_list_next_size

        ;; HL is now current + 6 + name_size: the next record.
        ex      de,hl                   ; DE=next
        ld      l,-2(ix)
        ld      h,-1(ix)                ; page
        inc     hl
        inc     hl
        dec     (hl)                    ; entries_left
        inc     hl
        ld      (hl),e
        inc     hl
        ld      (hl),d                  ; page->next
        ld      de,#1
        jr      filesystem_list_next_finish

filesystem_list_next_protocol:
        ld      sp,ix                   ; discard any temporary pushes
        ld      de,#ERROR_PROTOCOL
        jr      filesystem_list_next_epilogue
filesystem_list_next_argument:
        ld      de,#ERROR_ARGUMENT
filesystem_list_next_finish:
        ld      sp,ix
filesystem_list_next_epilogue:
        pop     ix
        ret

;; int squid_client_fs_read(client, path, offset, maximum_bytes, chunk)
_squid_client_fs_read::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 path
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,10(ix)
        or      a,9(ix)                 ; chunk
        jp      z,filesystem_read_argument
        xor     a,a                     ; non-empty path
        call    filesystem_path_size
        bit     7,d
        jp      nz,filesystem_read_finish
        push    de                      ; -8 path size
        ld      hl,#7
        add     hl,de
        ex      de,hl
        call    filesystem_request_begin
        jp      c,filesystem_read_argument

        ld      (hl),#FS_READ
        inc     hl
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
        inc     hl
        ld      a,8(ix)
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
        jr      z,filesystem_read_path_done
        ldir
filesystem_read_path_done:
        push    ix
        pop     hl
        ld      de,#-6
        add     hl,de
        ld      e,-8(ix)
        ld      d,#0
        ld      bc,#7
        ex      de,hl
        add     hl,bc
        ex      de,hl
        ld      a,#FS_READ
        ld      b,#11
        call    filesystem_response
        ld      a,d
        or      a,e
        jr      nz,filesystem_read_finish

        ;; Validate exact [11-byte header][data].
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
        ld      a,(hl)                  ; data size
        ld      c,a
        ld      b,#0
        ld      hl,#11
        add     hl,bc
        ld      a,h
        cp      a,-5(ix)
        jr      nz,filesystem_read_protocol
        ld      a,l
        cp      a,-6(ix)
        jr      nz,filesystem_read_protocol

        ;; Copy offset and total size, then return a zero-copy data slice.
        ld      e,9(ix)
        ld      d,10(ix)                ; chunk
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
        ld      bc,#8
        ldir
        ;; HL=packet+10, DE=chunk+8.
        inc     hl                      ; packet+11 data
        ld      a,l
        ld      (de),a
        inc     de
        ld      a,h
        ld      (de),a
        inc     de
        dec     hl
        ld      a,(hl)                  ; response data size
        ld      (de),a
        ld      de,#0
        jr      filesystem_read_finish
filesystem_read_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      filesystem_read_finish
filesystem_read_argument:
        ld      de,#ERROR_ARGUMENT
filesystem_read_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc
        pop     bc
        pop     bc
        inc     sp                      ; 7 bytes of packed arguments
        jp      (hl)

;; int squid_client_fs_write(client, path, offset, flags, data, size, written)
_squid_client_fs_write::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 path
        ld      bc,#0
        push    bc                      ; -6 response size
        ld      a,13(ix)
        or      a,12(ix)                ; written
        jp      z,filesystem_write_argument
        ld      a,11(ix)                ; size
        or      a,a
        jr      z,filesystem_write_data_ok
        ld      a,10(ix)
        or      a,9(ix)                 ; data
        jp      z,filesystem_write_argument
filesystem_write_data_ok:
        xor     a,a
        call    filesystem_path_size
        bit     7,d
        jp      nz,filesystem_write_finish
        push    de                      ; -8 path size
        ld      hl,#8
        add     hl,de
        ld      c,11(ix)
        ld      b,#0
        add     hl,bc
        ex      de,hl                   ; request size
        call    filesystem_request_begin
        jp      c,filesystem_write_argument

        ld      (hl),#FS_WRITE
        inc     hl
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
        inc     hl
        ld      a,8(ix)                 ; flags
        ld      (hl),a
        inc     hl
        ld      a,-8(ix)
        ld      (hl),a
        inc     hl                      ; path destination
        ex      de,hl
        ld      l,-4(ix)
        ld      h,-3(ix)
        ld      c,-8(ix)
        ld      b,#0
        ld      a,b
        or      a,c
        jr      z,filesystem_write_path_done
        ldir
filesystem_write_path_done:
        ex      de,hl                   ; request after path
        ld      a,11(ix)
        ld      (hl),a                  ; data length
        inc     hl
        ex      de,hl                   ; DE=data destination
        ld      l,9(ix)
        ld      h,10(ix)
        ld      c,11(ix)
        ld      b,#0
        ld      a,b
        or      a,c
        jr      z,filesystem_write_data_done
        ldir
filesystem_write_data_done:
        push    ix
        pop     hl
        ld      de,#-6
        add     hl,de
        ld      e,-8(ix)
        ld      d,#0
        ld      bc,#8
        ex      de,hl
        add     hl,bc
        ld      c,11(ix)
        ld      b,#0
        add     hl,bc
        ex      de,hl
        ld      a,#FS_WRITE
        ld      b,#7
        call    filesystem_response
        ld      a,d
        or      a,e
        jr      nz,filesystem_write_finish
        ld      a,-5(ix)
        or      a,a
        jr      nz,filesystem_write_protocol
        ld      a,-6(ix)
        cp      a,#7
        jr      nz,filesystem_write_protocol

        ;; Acknowledged offset must match and acknowledged size cannot grow.
        ld      l,-2(ix)
        ld      h,-1(ix)
        inc     hl
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl                   ; packet
        inc     hl
        inc     hl                      ; response offset
        push    ix
        pop     de
        ld      bc,#4
        ex      de,hl
        add     hl,bc
        ex      de,hl                   ; DE=&input offset, HL=response offset
        ld      b,#4
filesystem_write_offset_check:
        ld      a,(de)
        cp      a,(hl)
        jr      nz,filesystem_write_protocol
        inc     de
        inc     hl
        djnz    filesystem_write_offset_check
        ld      a,11(ix)
        cp      a,(hl)                  ; response bytes_written
        jr      c,filesystem_write_protocol
        ld      e,12(ix)
        ld      d,13(ix)
        ld      a,(hl)
        ld      (de),a
        ld      de,#0
        jr      filesystem_write_finish
filesystem_write_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      filesystem_write_finish
filesystem_write_argument:
        ld      de,#ERROR_ARGUMENT
filesystem_write_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc
        pop     bc
        pop     bc
        pop     bc
        pop     bc                      ; 10 bytes of packed arguments
        jp      (hl)

;; mkdir and delete have the same request and ABI.
_squid_client_fs_mkdir::
        ld      a,#FS_MKDIR
        jr      filesystem_simple_path
_squid_client_fs_delete::
        ld      a,#FS_DELETE
filesystem_simple_path:
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 path
        ld      c,a
        ld      b,#0
        push    bc                      ; -6 opcode
        ld      bc,#0
        push    bc                      ; -8 response size
        xor     a,a
        call    filesystem_path_size
        bit     7,d
        jr      nz,filesystem_simple_finish
        push    de                      ; -10 path size
        ld      hl,#2
        add     hl,de
        ex      de,hl
        call    filesystem_request_begin
        jr      c,filesystem_simple_argument
        ld      a,-6(ix)
        ld      (hl),a
        inc     hl
        ld      a,-10(ix)
        ld      (hl),a
        inc     hl
        ex      de,hl
        ld      l,-4(ix)
        ld      h,-3(ix)
        ld      c,-10(ix)
        ld      b,#0
        ldir
        push    ix
        pop     hl
        ld      de,#-8
        add     hl,de
        ld      e,-10(ix)
        ld      d,#0
        inc     de
        inc     de
        ld      a,-6(ix)
        ld      b,#2
        call    filesystem_response
        ld      a,d
        or      a,e
        jr      nz,filesystem_simple_finish
        ld      a,-7(ix)
        or      a,a
        jr      nz,filesystem_simple_protocol
        ld      a,-8(ix)
        cp      a,#2
        jr      nz,filesystem_simple_protocol
        ld      de,#0
        jr      filesystem_simple_finish
filesystem_simple_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      filesystem_simple_finish
filesystem_simple_argument:
        ld      de,#ERROR_ARGUMENT
filesystem_simple_finish:
        ld      sp,ix
        pop     ix
        ret

;; int squid_client_fs_rename(client, old_path, new_path)
_squid_client_fs_rename::
        push    ix
        ld      ix,#0
        add     ix,sp
        push    hl                      ; -2 client
        push    de                      ; -4 old path
        ld      bc,#0
        push    bc                      ; -6 response size
        xor     a,a
        call    filesystem_path_size
        bit     7,d
        jp      nz,filesystem_rename_finish
        push    de                      ; -8 old size

        ld      a,#FS_PATH_MAX
        push    af
        inc     sp
        ld      l,4(ix)
        ld      h,5(ix)                 ; new path
        call    _squid_client_text_size
        bit     7,d
        jp      nz,filesystem_rename_finish
        ld      a,d
        or      a,e
        jp      z,filesystem_rename_argument
        push    de                      ; -10 new size
        ld      hl,#3
        add     hl,de
        ld      e,-8(ix)
        ld      d,#0
        add     hl,de
        ex      de,hl                   ; request size
        call    filesystem_request_begin
        jp      c,filesystem_rename_argument

        ld      (hl),#FS_RENAME
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
        ld      bc,#3
        add     hl,bc
        ex      de,hl
        ld      a,#FS_RENAME
        ld      b,#2
        call    filesystem_response
        ld      a,d
        or      a,e
        jr      nz,filesystem_rename_finish
        ld      a,-5(ix)
        or      a,a
        jr      nz,filesystem_rename_protocol
        ld      a,-6(ix)
        cp      a,#2
        jr      nz,filesystem_rename_protocol
        ld      de,#0
        jr      filesystem_rename_finish
filesystem_rename_protocol:
        ld      de,#ERROR_PROTOCOL
        jr      filesystem_rename_finish
filesystem_rename_argument:
        ld      de,#ERROR_ARGUMENT
filesystem_rename_finish:
        ld      sp,ix
        pop     ix
        pop     hl
        pop     bc                      ; new path
        jp      (hl)
