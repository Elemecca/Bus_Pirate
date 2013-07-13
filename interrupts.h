/*
 * This file is part of the Bus Pirate project.
 * http://code.google.com/p/the-bus-pirate/
 *
 * Written and maintained by the Bus Pirate project.
 *
 * To the extent possible under law, the project has
 * waived all copyright and related or neighboring rights to Bus Pirate. This
 * work is published from United States.
 *
 * For details see: http://creativecommons.org/publicdomain/zero/1.0/
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef INTERRUPTS_H_
#define INTERRUPTS_H_

/** Runtime interrupt vector table (IVT).
 * Any interrupt whose vector is not defined at compile time will use the
 * vector defined in this table. Entries for interrupts with no vector must
 * be set to the sentinel value {@code NULL_ISR} ({@code 0xFFFF}). Interrupt
 * service routines (ISRs) must be declared as normal, except that the function
 * identifier must not start with an underbar. See the XC16 C Compiler User's
 * Guide section 10.3 for details on writing ISRs. Additionally, the first
 * instruction of the ISR must be {@code POP.S}. This can be achieved with the
 * {@code __preprologue__} attribute.
 *
 * Two macros have been provided for declaring ISRs. The first, {@code ISR_RT},
 * provides the complete function attribute set and should be placed between
 * the return type and identifier. If you need to define attributes yourself
 * you can use {@code ISR_RT_PROLOGUE} which contains just the preprologue
 * parameter to {@code __interrupt__}. For example:
 * <pre>
 *     // simple ISR
 *     void ISR_RT my_interrupt_handler (void);
 *
 *     // ISR with custom attributes
 *     void __attribute__((
 *             __interrupt__( ISR_RT_PROLOGUE ),
 *             __no_auto_psv__, shadow,
 *         )) my_interrupt_handler (void);
 * </pre>
 *
 * Rather than manipulating this table directly you should use the provided
 * {@code ISR_x} macros. For example:
 * <pre>
 *     // enable runtime ISR for UART 2 errors
 *     ISR_U2Err = &my_interrupt_handler;
 *
 *     // disable runtime ISR
 *     ISR_U2ERR = NULL_ISR;
 * </pre>
 */
extern void (*interrupt_table[ 118 ])(void);

/** Sentinel value for unhandled interrupts in the runtime IVT. */
#define NULL_ISR ((void (*)(void)) 0xFFFF)

/** {@code __preprologue__} attribute which must be applied to runtime ISRs.
 * Must be included as an argument to the {@code __interrupt__} attribute.
 */
#define ISR_RT_PROLOGUE __preprologue__( "pop.s" )

/** Attributes for runtime interrupt service routines (ISRs).
 * @see interrupt_table
 */
#define ISR_RT __attribute__(( \
        __interrupt__( ISR_RT_PROLOGUE ), \
        __no_auto_psv__, \
    ))


// these macros are based on the interrupt table in 
// the XC16 C Compiler User's Guide section 10.4.3

// INT0 External interrupt 0
#define IRQ_INT0    0
#define ISR_INT0    (interrupt_table[ 0 ])

// IC1 Input Capture 1
#define IRQ_IC1     1
#define ISR_IC1     (interrupt_table[ 1 ])

// OC1 Output Compare 1
#define IRQ_OC1     2
#define ISR_OC1     (interrupt_table[ 2 ])

// TMR1 Timer 1 expired
#define IRQ_T1      3
#define ISR_T1      (interrupt_table[ 3 ])

// IC2 Input Capture 2
#define IRQ_IC2     5
#define ISR_IC2     (interrupt_table[ 5 ])

// OC2 Output Compare 2
#define IRQ_OC2     6
#define ISR_OC2     (interrupt_table[ 6 ])

// TMR2 Timer 2 expired
#define IRQ_T2      7
#define ISR_T2      (interrupt_table[ 7 ])

// TMR3 Timer 3 expired
#define IRQ_T3      8
#define ISR_T3      (interrupt_table[ 8 ])

// SPI1 error interrupt
#define IRQ_SPI1Err 9
#define ISR_SPI1Err (interrupt_table[ 9 ])

// SPI1 transfer completed interrupt
#define IRQ_SPI1    10
#define ISR_SPI1    (interrupt_table[ 10 ])

// UART1RX Uart 1 Receiver
#define IRQ_U1RX    11
#define ISR_U1RX    (interrupt_table[ 11 ])

// UART1TX Uart 1 Transmitter
#define IRQ_U1TX    12
#define ISR_U1TX    (interrupt_table[ 12 ])

// ADC 1 convert completed
#define IRQ_ADC1    13
#define ISR_ADC1    (interrupt_table[ 13 ])

// Slave I2C interrupt 1
#define IRQ_SI2C1   16
#define ISR_SI2C1   (interrupt_table[ 16 ])

// Master I2C interrupt 1
#define IRQ_MI2C1   17
#define ISR_MI2C1   (interrupt_table[ 17 ])

// Comparator interrupt
#define IRQ_Comp    18
#define ISR_Comp    (interrupt_table[ 18 ])

// CN Input change interrupt
#define IRQ_CN      19
#define ISR_CN      (interrupt_table[ 19 ])

// INT1 External interrupt 1
#define IRQ_INT1    20
#define ISR_INT1    (interrupt_table[ 20 ])

// OC3 Output Compare 3
#define IRQ_OC3     25
#define ISR_OC3     (interrupt_table[ 25 ])

// OC4 Output Compare 4
#define IRQ_OC4     26
#define ISR_OC4     (interrupt_table[ 26 ])

// TMR4 Timer 4 expired
#define IRQ_T4      27
#define ISR_T4      (interrupt_table[ 27 ])

// TMR5 Timer 5 expired
#define IRQ_T5      28
#define ISR_T5      (interrupt_table[ 28 ])

// INT2 External interrupt 2
#define IRQ_INT2    29
#define ISR_INT2    (interrupt_table[ 29 ])

// UART2RX Uart 2 Receiver
#define IRQ_U2RX    30
#define ISR_U2RX    (interrupt_table[ 30 ])

// UART2TX Uart 2 Transmitter
#define IRQ_U2TX    31
#define ISR_U2TX    (interrupt_table[ 31 ])

// SPI2 error interrupt
#define IRQ_SPI2Err 32
#define ISR_SPI2Err (interrupt_table[ 32 ])

// SPI2 transfer completed interrupt
#define IRQ_SPI2    33
#define ISR_SPI2    (interrupt_table[ 33 ])

// IC3 Input Capture 3
#define IRQ_IC3     37
#define ISR_IC3     (interrupt_table[ 37 ])

// IC4 Input Capture 4
#define IRQ_IC4     38
#define ISR_IC4     (interrupt_table[ 38 ])

// IC5 Input Capture 5
#define IRQ_IC5     39
#define ISR_IC5     (interrupt_table[ 39 ])

// OC5 Output Compare 5
#define IRQ_OC5     41
#define ISR_OC5     (interrupt_table[ 41 ])

// Parallel master port interrupt
#define IRQ_PMP     45
#define ISR_PMP     (interrupt_table[ 45 ])

// Slave I2C interrupt 2
#define IRQ_SI2C2   49
#define ISR_SI2C2   (interrupt_table[ 49 ])

// Master I2C interrupt 2
#define IRQ_MI2C2   50
#define ISR_MI2C2   (interrupt_table[ 50 ])

// INT3 External interrupt 3
#define IRQ_INT3    53
#define ISR_INT3    (interrupt_table[ 53 ])

// INT4 External interrupt 4
#define IRQ_INT4    54
#define ISR_INT4    (interrupt_table[ 54 ])

// Real-Time Clock And Calender
#define IRQ_RTCC    62
#define ISR_RTCC    (interrupt_table[ 62 ])

// UART1 error interrupt
#define IRQ_U1Err   65
#define ISR_U1Err   (interrupt_table[ 65 ])

// UART2 error interrupt
#define IRQ_U2Err   66
#define ISR_U2Err   (interrupt_table[ 66 ])

// Cyclic Redundancy Check
#define IRQ_CRC     67
#define ISR_CRC     (interrupt_table[ 67 ])


#endif