/*
WIN9X_CHKSTK.S

The stack probe clang calls on the GNU target for a frame of more than a
page (_alloca: eax bytes), as mingw-w64's runtime has it: Windows commits a
thread's stack a page at a time, as each guard page is touched, so the new
frame's pages are touched in order, then esp moved below them.
*/

	.text
	.globl __alloca
	.globl ___chkstk
__alloca:
___chkstk:
	pushl %ecx
	/* (the stack as the caller had it: past the return address and ecx) */
	leal 8(%esp), %ecx
1:	cmpl $0x1000, %eax
	jb 2f
	subl $0x1000, %ecx
	orl $0, (%ecx)
	subl $0x1000, %eax
	jmp 1b
2:	subl %eax, %ecx
	orl $0, (%ecx)
	/* (esp to the new bottom; return as the caller expects, ecx kept) */
	movl %esp, %eax
	movl %ecx, %esp
	movl (%eax), %ecx
	pushl 4(%eax)
	ret
