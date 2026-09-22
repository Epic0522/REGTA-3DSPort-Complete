#include <3ds/asminc.h>

BEGIN_ASM_FUNC initSystem, weak
	ldr	r2, =saved_stack
	str	sp, [r2]
	str	lr, [r2,#4]

	bl	__libctru_init

	ldr	r2, =fake_heap_start
	ldr	sp, [r2]

	ldr	r3, =__stacksize__
	ldr	r3, [r3]
	add sp, sp, r3
	add	sp, sp, #7
	bics	sp, sp, #7
	str	sp, [r2]

	bl	__appInit
	bl	__libc_init_array

	ldr	r2, =saved_stack
	ldr	lr, [r2,#4]
 	bx	lr
END_ASM_FUNC

BEGIN_ASM_FUNC __ctru_exit, weak
	@ Running C++ global destructors at exit deletes frontend RenderWare
	@ resources a second time, and a power-off request can reach that path
	@ while those globals still retain stale texture pointers. The process is
	@ exiting and the kernel will reclaim its address space, so skip the
	@ duplicate global destructor sweep and proceed directly to service
	@ shutdown.
	@
	@ REQUIREMENT: because the destructor sweep is skipped, each game must
	@ explicitly stop every thread it started before returning from main().
	@ __libctru_exit unmaps the heap immediately after this; any thread still
	@ running will fault on freed memory. See callTheMaid() in each
	@ <game>/src/skel/3ds/3ds.cpp.
	bl	__appExit

	ldr	r2, =saved_stack
	ldr	sp, [r2]
	b	__libctru_exit
END_ASM_FUNC

	.section .data.__stacksize__, "aw"
	.align 2
__stacksize__:
	.word	32 * 1024
	.weak	__stacksize__

	.section .bss.saved_stack.42, "aw", %nobits
	.align 2
saved_stack:
	.space 8
