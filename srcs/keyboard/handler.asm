; Defined in interrupts.c
[BITS 32]
[extern isr_handler]
[extern irq_handler]
[extern syscall_handler]

%macro no_error_code_isr_handler 1
	global isr_handler_%1
		isr_handler_%1:
		push 0 ; push 0 as error code
		push %1 ; push the interrupt number
		jmp common_isr_handler ; jump to the common handler
%endmacro

%macro error_code_isr_handler 1
	global isr_handler_%1
		isr_handler_%1:
		push %1 ; push the parameter as error code & interrupt number
		push %1 ; push the parameter as error code & interrupt number
		jmp common_isr_handler ; jump to the common handler
%endmacro

%macro no_error_code_irq_handler 2
	global irq_handler_%1
		irq_handler_%1:
		push %2 ; push the index (master and slave)
		push %1 ; push the interrupt number
		jmp common_irq_handler ; jump to the common handler
%endmacro


; handles isrs (software interrupts)
common_isr_handler:
	; save registers
	cli
	pusha

	; call c function
	call isr_handler

	; restores registers
	popa

	; restore esp, removing the interrupt number
	add	esp, 8

	; iret call
	sti
	iret

global syscall_handler_asm
syscall_handler_asm:
	pusha
	call syscall_handler
	mov [esp + 28], eax   ; This writes to the saved EAX (pusha order: EDI, ESI, EBP, original ESP, EBX, EDX, ECX, EAX)
	popa
	iret

; handles irqs (hardware interrupts)
common_irq_handler:
	; save registers
	cli
	pusha

	; call c function
	call irq_handler

	; restores registers
	popa

	; restore esp, removing the interrupt number
	add	esp, 8

	; iret call
	sti
	iret

no_error_code_isr_handler 0
no_error_code_isr_handler 1
no_error_code_isr_handler 2
no_error_code_isr_handler 3
no_error_code_isr_handler 4
no_error_code_isr_handler 5
no_error_code_isr_handler 6
error_code_isr_handler 7
no_error_code_isr_handler 8
error_code_isr_handler 9
error_code_isr_handler 10
error_code_isr_handler 11
error_code_isr_handler 12
error_code_isr_handler 13
no_error_code_isr_handler 14
no_error_code_isr_handler 15
error_code_isr_handler 16
no_error_code_isr_handler 17
no_error_code_isr_handler 18
no_error_code_isr_handler 19
no_error_code_isr_handler 20
no_error_code_isr_handler 21
no_error_code_isr_handler 22
no_error_code_isr_handler 23
no_error_code_isr_handler 24
no_error_code_isr_handler 25
no_error_code_isr_handler 26
no_error_code_isr_handler 27
no_error_code_isr_handler 28
no_error_code_isr_handler 29
no_error_code_isr_handler 30
no_error_code_isr_handler 31

;IRQs (hardware interrupts)
no_error_code_irq_handler 0, 32
no_error_code_irq_handler 1, 33
no_error_code_irq_handler 2, 34
no_error_code_irq_handler 3, 35
no_error_code_irq_handler 4, 36
no_error_code_irq_handler 5, 37
no_error_code_irq_handler 6, 38
no_error_code_irq_handler 7, 39
no_error_code_irq_handler 8, 40
no_error_code_irq_handler 9, 41
no_error_code_irq_handler 10, 42
no_error_code_irq_handler 11, 43
no_error_code_irq_handler 12, 44
no_error_code_irq_handler 13, 45
no_error_code_irq_handler 14, 46
no_error_code_irq_handler 15, 47

section .note.GNU-stack noalloc noexec nowrite progbits
