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

; AUGH, this should not be necessary
; value from PIC24FJ64GA004 Family Datasheet table 4-5
.ifndecl INTTREG
    .equ INTTREG, #0x00E0
.endif

.section .ndata, data, near

; the interrupt handler table
.global _interrupt_table
_interrupt_table: .fill 118, 2, 0xFFFF

.section .isr, code

; At present the trampoline takes eleven cycles. That could be reduced to six
; by using a jump table instead of calculating the address of the IVT entry
; at runtime. Each row of the jump table would be:
;     push.s      ; matching pop.s is in ISR prologue
;     mov f, w0   ; file register move with immediate address for IVT entry
;     btss w0, #0 ; check unhandled interrupt sentinel value
;     goto w0     ; an ISR is defined, call it
;     reset       ; unhandled interrupt, reset the CPU
;
; However, generating the jump table for default interrupt vectors would
; require modifying the linker script which generates the IVT. That's more
; complex than I want to get into for a savings of three cycles.

.global __DefaultInterrupt
__DefaultInterrupt:
    push.s ; matching pop.s is in ISR prologue

    ; compute address of interrupt table entry
    mov INTTREG, w0
    and #0x007F, w0             ; mask for VECNUM
    sub #8, w0                  ; compensate for the traps
    add w0, w0, w0              ; multipy index by two to get entry offset
    mov #_interrupt_table, w1   ; add base address of table
    add w1, w0, w0              ; "

    ; load and check the ISR address
    ; instructions are word-aligned, so pointers always have bit 0 clear
    ; we use the sentinel value 0xFFFF for unhandled interrupts
    mov [w0], w0
    btss w0, #0
    goto w0     ; an ISR is defined, call it
    reset       ; unhandled interrupt, reset the CPU
