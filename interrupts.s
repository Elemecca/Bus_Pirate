; This file is part of the Bus Pirate project.
; http://code.google.com/p/the-bus-pirate/
;
; Written and maintained by the Bus Pirate project.
;
; To the extent possible under law, the project has
; waived all copyright and related or neighboring rights to Bus Pirate. This
; work is published from United States.
;
; For details see: http://creativecommons.org/publicdomain/zero/1.0/
;
;    This program is distributed in the hope that it will be useful,
;    but WITHOUT ANY WARRANTY; without even the implied warranty of
;    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.


; the default ISR provided by the linker which just resets the CPU
.extern __DefaultInterrupt


.section .rivt, data ; Runtime Interrupt Vector Table

; generates an array of function pointers initialized to the default ISR
.global _interrupt_table
_interrupt_table:
.rept 118
    .word __DefaultInterrupt
.endr


.section .rivjt, code ; Runtime Interrupt Vector Jump Table

; builds a jump table
; each row jumps to the address in the corresponding row of the RIVT
; each row in the static IVT points to the corresponding row in this table
.equ index, 0
.rept 118
    push.s ; matching pop.s is in the runtime ISR prologue
    mov _interrupt_table + #index, w0
    goto w0
    .equ index, #index + 2
.endr
