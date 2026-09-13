/*
	GCC is deleting our globals that override weak variables
	when using -flto. Its rather quite rude.
	God fucking damnit even this doesn't work!!~!!
	So the linker is getting rid of it?
	ok. it works? fuck it.
*/

;; 	.global __stacksize__
;; 	.section .data.__stacksize__, "aw"
;; 	.align 2
;; __stacksize__:
;; 	.word	1 << 20
