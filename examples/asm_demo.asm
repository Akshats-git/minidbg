; asm_demo.asm - assembly in the style a small compiler emits (NASM syntax).
;
; Equivalent source:
;     fn square(n)       { return n * n; }
;     fn sum_squares(n)  { total = 0; i = 1; while (i <= n) { total += square(i); i += 1; } return total; }
;     fn main()          { print(sum_squares(4)); }
;
; Every source function is a global label, and every function sets up a
; "push rbp / mov rbp, rsp" frame, so minidbg's bt works. Note there are NO
; "type function" or "size" directives: NASM then emits plain no-type, size-0
; symbols, and minidbg has to cope with that.

default rel
extern printf

section .data
fmt: db "sum of squares 1..4 = %ld", 10, 0

section .text

global square
square:
    push rbp
    mov rbp, rsp
    mov rax, rdi            ; argument n arrives in rdi
    imul rax, rdi
    pop rbp
    ret

global sum_squares
sum_squares:
    push rbp
    mov rbp, rsp
    sub rsp, 32             ; [rbp-8] = total, [rbp-16] = i, [rbp-24] = n
    mov [rbp-24], rdi
    mov qword [rbp-8], 0
    mov qword [rbp-16], 1
.loop:
    mov rax, [rbp-16]
    cmp rax, [rbp-24]
    jg .done
    mov rdi, [rbp-16]
    call square
    add [rbp-8], rax
    inc qword [rbp-16]
    jmp .loop
.done:
    mov rax, [rbp-8]
    leave
    ret

global main
main:
    push rbp
    mov rbp, rsp
    mov rdi, 4
    call sum_squares
    lea rdi, [rel fmt]
    mov rsi, rax
    xor eax, eax
    call printf wrt ..plt   ; wrt ..plt keeps it working in a PIE executable
    xor eax, eax
    pop rbp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
