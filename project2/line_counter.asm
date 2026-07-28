; =============================================================
; line_counter.asm
; x86-64 Linux (NASM) assembly program.
;
; Reads "sensor_readings.txt" from the current directory,
; traverses it character by character, and reports:
;   Total records: X   (total number of lines)
;   Valid records: Y   (lines that contain at least one
;                       non-whitespace, non-newline character)
;
; Handles both Unix (LF) and Windows (CRLF) line endings, and
; reports a clear error message if the file cannot be opened
; or read.
; =============================================================

%define SYS_READ   0
%define SYS_WRITE  1
%define SYS_OPEN   2
%define SYS_CLOSE  3
%define SYS_EXIT   60

%define O_RDONLY   0

%define BUF_SIZE   65536      ; read buffer size

section .bss
    file_buf   resb BUF_SIZE  ; holds bytes read from the file
    num_str    resb 24        ; scratch space for integer->ASCII conversion

section .data
    filename        db "sensor_readings.txt", 0

    err_open_msg     db "Error: could not open sensor_readings.txt", 10
    err_open_len     equ $ - err_open_msg

    err_read_msg     db "Error: failed while reading sensor_readings.txt", 10
    err_read_len     equ $ - err_read_msg

    total_label      db "Total records: "
    total_label_len  equ $ - total_label

    valid_label      db "Valid records: "
    valid_label_len  equ $ - valid_label

    newline          db 10

section .text
    global _start

_start:
    ; ---------------------------------------------------------
    ; Step 1: open the file (read-only)
    ; ---------------------------------------------------------
    mov     rax, SYS_OPEN
    lea     rdi, [rel filename]
    mov     rsi, O_RDONLY
    xor     rdx, rdx
    syscall

    cmp     rax, 0
    jl      .open_error          ; negative return value => open() failed

    mov     r12, rax             ; r12 = file descriptor (kept across calls)

    ; Counters, kept in callee-safe registers across the read loop
    xor     r13, r13             ; r13 = total_records
    xor     r14, r14             ; r14 = valid_records
    xor     r15, r15             ; r15 = "current line has data" flag (0/1)

    ; ---------------------------------------------------------
    ; Step 2: read the file in chunks and traverse it
    ;          character by character
    ; ---------------------------------------------------------
.read_loop:
    mov     rax, SYS_READ
    mov     rdi, r12
    lea     rsi, [rel file_buf]
    mov     rdx, BUF_SIZE
    syscall

    cmp     rax, 0
    jl      .read_error           ; negative => read() failed
    je      .end_of_file          ; 0 bytes read => EOF

    mov     rbx, rax              ; rbx = number of bytes actually read
    xor     rcx, rcx              ; rcx = index into file_buf

.char_loop:
    cmp     rcx, rbx
    jge     .read_loop            ; consumed this chunk, read more

    lea     rsi, [rel file_buf]
    movzx   eax, byte [rsi + rcx] ; al = current character

    cmp     al, 10                ; is it '\n' (LF)?
    je      .found_newline

    cmp     al, 13                ; CR from a CRLF pair: not a "real" char,
    je      .skip_char            ; ignore it (LF that follows ends the line)

    cmp     al, 32                ; treat space/tab as "no data by itself"
    je      .skip_char
    cmp     al, 9
    je      .skip_char

    ; any other byte counts as real data on this line
    mov     r15, 1

.skip_char:
    inc     rcx
    jmp     .char_loop

.found_newline:
    ; Conditional logic: line boundary detected.
    inc     r13                    ; total_records++
    cmp     r15, 1
    jne     .no_data_this_line
    inc     r14                    ; valid_records++ (line had real data)
.no_data_this_line:
    xor     r15, r15               ; reset flag for next line
    inc     rcx
    jmp     .char_loop

.end_of_file:
    ; If the file does not end with a trailing newline but the last
    ; line still has pending characters, count that final line too.
    cmp     r15, 1
    jne     .no_trailing_partial
    inc     r13
    inc     r14
.no_trailing_partial:

    ; also, if file ended and r13 counted 0 total but flag never set
    ; (empty file) -> total stays 0, which is correct.

    ; ---------------------------------------------------------
    ; Step 3: close the file
    ; ---------------------------------------------------------
    mov     rax, SYS_CLOSE
    mov     rdi, r12
    syscall

    ; ---------------------------------------------------------
    ; Step 4: print results
    ;   "Total records: X\n"
    ;   "Valid records: Y\n"
    ; ---------------------------------------------------------
    lea     rdi, [rel total_label]
    mov     rsi, total_label_len
    call    print_buf

    mov     rdi, r13
    call    print_number
    call    print_newline

    lea     rdi, [rel valid_label]
    mov     rsi, valid_label_len
    call    print_buf

    mov     rdi, r14
    call    print_number
    call    print_newline

    ; ---------------------------------------------------------
    ; Step 5: exit(0)
    ; ---------------------------------------------------------
    mov     rax, SYS_EXIT
    xor     rdi, rdi
    syscall

.open_error:
    lea     rsi, [rel err_open_msg]
    mov     rdx, err_open_len
    mov     rax, SYS_WRITE
    mov     rdi, 2                 ; stderr
    syscall
    mov     rax, SYS_EXIT
    mov     rdi, 1
    syscall

.read_error:
    lea     rsi, [rel err_read_msg]
    mov     rdx, err_read_len
    mov     rax, SYS_WRITE
    mov     rdi, 2                 ; stderr
    syscall
    mov     rax, SYS_CLOSE
    mov     rdi, r12
    syscall
    mov     rax, SYS_EXIT
    mov     rdi, 1
    syscall

; =============================================================
; print_buf: write(1, rdi=buf, rsi=len)
; =============================================================
print_buf:
    push    rax
    push    rdi
    push    rsi
    mov     rdx, rsi
    mov     rsi, rdi
    mov     rax, SYS_WRITE
    mov     rdi, 1
    syscall
    pop     rsi
    pop     rdi
    pop     rax
    ret

; print_newline: writes a single '\n' to stdout
; =============================================================
print_newline:
    push    rax
    push    rdi
    push    rsi
    push    rdx
    mov     rax, SYS_WRITE
    mov     rdi, 1
    lea     rsi, [rel newline]
    mov     rdx, 1
    syscall
    pop     rdx
    pop     rsi
    pop     rdi
    pop     rax
    ret

; print_number: converts the unsigned integer in rdi to ASCII
; decimal and writes it to stdout. Handles 0 correctly.
; =============================================================
print_number:
    push    rax
    push    rbx
    push    rcx
    push    rdx
    push    rdi
    push    rsi

    lea     rsi, [rel num_str + 23]  ; write digits back-to-front
    mov     byte [rsi], 0
    mov     rax, rdi
    mov     rbx, 10
    xor     rcx, rcx                 ; digit counter

    cmp     rax, 0
    jne     .convert_loop
    ; special case: value is 0
    dec     rsi
    mov     byte [rsi], '0'
    inc     rcx
    jmp     .print_it

.convert_loop:
    cmp     rax, 0
    je      .print_it
    xor     rdx, rdx
    div     rbx                      ; rax = rax/10, rdx = remainder
    add     dl, '0'
    dec     rsi
    mov     [rsi], dl
    inc     rcx
    jmp     .convert_loop

.print_it:
    mov     rax, SYS_WRITE
    mov     rdi, 1
    mov     rdx, rcx
    syscall

    pop     rsi
    pop     rdi
    pop     rdx
    pop     rcx
    pop     rbx
    pop     rax
    ret
