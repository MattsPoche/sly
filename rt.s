	// sly calling convention:
	// closure | continuation | args
	// %rdi    | %rsi         | stack
	// stack args shifted into:
	// %rax, %rbx, %rcx, %rdx, %r8, %r9, %r10, %r11, %r12, %r13
	// special registers:
	// %r15 = default exception handler
	// %r14 = rest arg

	.set sys_read, 0
	.set sys_write, 1
	.set sys_open, 2
	.set sys_close, 3
	.set sys_stat, 4
	.set sys_fstat, 5
	.set sys_lstat, 6
	.set sys_poll, 7
	.set sys_lseek, 8
	.set sys_mmap, 9
	.set sys_mprotect, 10
	.set sys_munmap, 11
	.set sys_brk, 12
	.set sys_exit, 60

	// sly constants
	.set st_ptr,   0x0
	.set st_null,  0x1
	.set st_imm,   0x2
	.set st_false, 0x3
	.set st_true,  0x4
	.set TAG_MASK, 0x7

	.set SLY_VOID, 0
	.set SLY_NULL, st_null
	.set SLY_FALSE, st_false
	.set SLY_TRUE, st_true

	.set tt_pair, 0
	.set tt_byte, 1
	.set tt_int, 2
	.set tt_float, 3
	.set tt_symbol, 4
	.set tt_record, 5
	.set tt_byte_vector, 6
	.set tt_vector, 7
	.set tt_string, 8
	.set tt_closure, 9

	.set imm_int, 0
	.set imm_byte, 1
	.set imm_float, 2

	.macro null_p reg
	cmpq $SLY_NULL, \reg
	jne NULL_F\@
	movq $SLY_TRUE, \reg
	jmp NULL_C\@
	NULL_F\@:
	movq $SLY_FALSE, \reg
	NULL_C\@:
	.endm

	.macro void_p reg
	cmpq $SLY_VOID, \reg
	jne VOID_F\@
	movq $SLY_TRUE, \reg
	jmp VOID_C\@
	VOID_F\@:
	movq $SLY_FALSE, \reg
	VOID_C\@:
	.endm

	.macro imm_p reg
	andq $TAG_MASK, \reg
	cmpq $st_imm, \reg
	jne IMM_F\@
	movq $SLY_TRUE, \reg
	jmp IMM_C\@
	IMM_F\@:
	movq $SLY_FALSE, \reg
	IMM_C\@:
	.endm

	.macro get_ptr reg
	andq $~TAG_MASK, \reg
	.endm

	.macro get_imm reg
	sarq $32, \reg
	.endm

	.macro typeof reg
	get_ptr \reg
	movq (\reg), \reg
	.endm

	.macro throw_except
	jmp *(%r15)
	.endm

	.data
SLYCLOSURE_exception_handler:
	.quad 0
SLYCLOSURE_make_record:
	.quad 0
SLYCLOSURE_record:
	.quad 0
SLYCLOSURE_exit:
	.quad 0
SLYCLOSURE_int_p:
	.quad 0
SLYCLOSURE_float_p:
	.quad 0
SLYCLOSURE_add2:
	.quad 0
imm_float:
	.quad 0x428a000002000002
imm_int:
	.quad 0x4500000002
x:
	.quad tt_int
	.quad 420
ktmp:
	.quad 0
fmt_str:
	.asciz "%d\n"

	.text
	.global _start
	.global SLYCLOSURE_exception_handler
	.global SLYCLOSURE_make_record
	.global SLYCLOSURE_exit
_start:
	pushq %rbp
	movq %rsp, %rbp
 	leaq exit_ok(%rip), %rsi
	pushq %rsi
	leaq exit_err(%rip), %rsi
	pushq %rsi
	leaq make_record(%rip), %rsi
	pushq %rsi
	leaq record(%rip), %rsi
	movq %rsi, %rdi
	pushq %rsi
	leaq int_p(%rip), %rsi
	pushq %rsi
	leaq float_p(%rip), %rsi
	pushq %rsi
	leaq K0(%rip), %rsi
	movq %rsi, (ktmp)
	movq $ktmp, %rsi
	jmp record
K0:
	popq %rdi
	leaq 4*8(%rdi), %r15
	leaq K1(%rip), %rax
	movq %rax, (ktmp)
	pushq $1
	jmp *3*8(%rdi)
K1:
	popq %rsi
	int3
test_proc:
 	popq %rsi
 	jmp exit_ok
get_int:
make_record:
	movq %rdi, %r12
	movq %rsi, %r13
	popq %rdi
	shlq $3, %rdi
	call malloc
	testq %rax, %rax
	jne L0
	throw_except
L0:
	pushq %rax
	movq %r12, %rdi
	movq %r13, %rsi
	jmp *(%rsi)
record:
	movq %rdi, %r12
	movq %rsi, %r13
	movq %rbp, %rbx
	subq %rsp, %rbx
	call malloc
	testq %rax, %rax
	jne CPY0
	throw_except
CPY0:
	shrq $3, %rbx
	xorq %rsi, %rsi
L1:
	movq (%rsp, %rsi, 8), %rcx
	movq %rcx, (%rax, %rsi, 8)
	addq $1, %rsi
	cmpq %rsi, %rbx
	jne L1
	movq %rbp, %rsp
	pushq %rax
	movq %r12, %rdi
	movq %r13, %rsi
	jmp *(%rsi)
exit_ok:
	movq $sys_exit, %rax
	movq $0, %rdi
	syscall
exit_err:
	movq $sys_exit, %rax
	movq $69, %rdi
	syscall
int_p:
	popq %rax
	movq %rax, %rdx
	cmpl $0x2, %edx
	je T0
	jmp E0
E0:
	andq $TAG_MASK, %rdx
	testq %rdx, %rdx
	je E1
	jmp F0
E1:
	typeof %rax
	cmpq $tt_int, %rax
	je T0
F0:
	pushq $SLY_FALSE
	jmp *(%rsi)
T0:
	pushq $SLY_TRUE
	jmp *(%rsi)
float_p:
	popq %rax
	movq %rax, %rdx
	cmpl $0x2000002, %edx
	je T1
	jmp E2
E2:
	andq $TAG_MASK, %rdx
	testq %rdx, %rdx
	je E3
	jmp F1
E3:
	typeof %rax
	cmpq $tt_float, %rax
	je T0
F1:
	pushq $SLY_FALSE
	jmp *(%rsi)
T1:
	pushq $SLY_TRUE
	jmp *(%rsi)
