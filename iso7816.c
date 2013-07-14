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

#include "base.h"

#ifdef BP_USE_ISO7816

#include "interrupts.h"

// define the pin mapping
#if defined BUSPIRATEV3
    #define SC_HIO          BP_MISO
    #define SC_HIO_DIR      BP_MISO_DIR
    #define SC_HIO_ODC      BP_MISO_ODC
    #define SC_HIO_CN       BP_MISO_CN
    #define SC_HIO_RPIN     BP_MISO_RPIN
    #define SC_HIO_RPOUT    BP_MISO_RPOUT

    #define SC_HRST         BP_CS
    #define SC_HRST_DIR     BP_CS_DIR
    #define SC_HRST_ODC     BP_CS_ODC
    #define SC_HRST_CN      BP_CS_CN
    #define SC_HRST_RPIN    BP_CS_RPIN
    #define SC_HRST_RPOUT   BP_CS_RPOUT

    #define SC_CLK          BP_CLK
    #define SC_CLK_DIR      BP_CLK_DIR
    #define SC_CLK_ODC      BP_CLK_ODC
    #define SC_CLK_CN       BP_CLK_CN
    #define SC_CLK_RPIN     BP_CLK_RPIN
    #define SC_CLK_RPOUT    BP_CLK_RPOUT
#elif defined BUSPIRATEV4
    #error "ISO 7816 mode is not yet supported on v4 hardware."
#else
    #error "Unknown hardware version."
#endif

extern struct _modeConfig modeConfig;

// session state values
// enums aren't allowed in bitfields, so use defines
#define SCS_MANUAL  0   // automatic operation is disabled
#define SCS_OFFLINE 1   // no session is active, the hardware is disconnected
#define SCS_RESET   2   // host has initiated reset
#define SCS_ATR     3   // device is sending ATR
#define SCS_IDLE    4   // session active, waiting for command
#define SCS_COMMAND 5   // command in progress

struct sc_state_t {
    unsigned session :4; // session state, use SCS_* defines

    
} sc_state;


/** Prescaler ratios for the SPI clock generator.
 * The numbers in the identifiers are frequency in KHz.
 */
enum sc_sck_prescale {
    SC_SCK_2000 = 0,
    SC_SCK_2286 = 1,
    SC_SCK_2666 = 2,
    SC_SCK_3200 = 3,
    SC_SCK_4000 = 4,
    SC_SCK_5333 = 5,
    SC_SCK_8000 = 6,
};

/** Sets up SPI module 1 as a clock generator on CLK.
 * 
 * When the SPI module is set to framed master mode its clock runs
 * continuously. The Peripheral Pin Select feature allows us to connect
 * just its clock output and ignore its other pins. That gives us a
 * clock output prescaled at 1:2,1:3..1:8 from the instruction clock
 * without bothering the CPU. See PIC24F FRM 23.3.4.1 for details.
 * 
 * @param prescale the prescaler ratio to use
 */
void scSCKSetup (enum sc_sck_prescale prescale) {
    SPI1STAT            = 0;            // reset SPI module
    SPI1CON1            = 0;            // "
    SPI1CON2            = 0;            // "
    SPI1CON1bits.MSTEN  = 1;            // enable master mode
    SPI1CON2bits.FRMEN  = 1;            // enable framed mode
    SPI1CON1bits.PPRE   = 3;            // primary   prescaler 1:1
    SPI1CON1bits.SPRE   = prescale;     // secondary prescaler
    IFS0bits.SPI1IF     = 0;            // disable SPI interrupt
    IEC0bits.SPI1IE     = 0;            // "
    IPC2bits.SPI1IP     = 0;            // "
    SC_CLK_RPOUT        = SCK1OUT_IO;   // connect SP1 SCK output to CLK
    SC_CLK_DIR          = 0;            // configure CLK as output
}

/** Cleans up after the SPI clock generator.
 * @see scSCKSetup
 */
void scSCKCleanup (void) {
    // SPI module 1 was clock output
    SPI1STAT        = 0; // reset SPI module
    SPI1CON1        = 0; // "
    SPI1CON2        = 0; // "
    SC_CLK_DIR      = 1; // configure CLK as input
    SC_CLK_RPOUT    = 0; // disconnect CLK
}

/** Starts or stops the SPI clock generator.
 * @param enable whether the clock generator should be running
 * @see scSCKSetup
 */
void scSCKEnable (int enable) {
    SPI1STATbits.SPIEN = enable & 1;
}

inline void sc_transition (unsigned new_state) {
    /* For most settings one mode is torn down and then another is set up.
     * There are a few settings, however, which would cause issues if they were
     * briefly disabled. Instead the setup for each mode sets these to the
     * correct value, which may be the current one. The complete list:
     *
     *     SC_HRST_CN           enables monitoring of bus resets
     *                          disabling this could make us miss a reset
     *
     *     U2MODEbits.UARTEN    enables the UART
     *                          disabling this could make us miss a character
     */

    // tear down the current mode
    switch (sc_state.session) {
        case SCS_MANUAL:
            // no teardown, everything's already stopped
            break;
            
        case SCS_OFFLINE:
            SC_CLK_CN = 0;
            break;

        case SCS_RESET:
            // stop the clock measurement timers
            T3CONbits.TON = 0;
            T2CONbits.TON = 0;
            break;

        case SCS_ATR:
            // no teardown yet
            break;
    }

    // set up the new mode
    switch (new_state) {
        case SCS_MANUAL:
            U2MODEbits.UARTEN   = 0; // stop the UART
            SC_HRST_CN          = 0; // don't listen for reset
            break;

        case SCS_OFFLINE:
            U2MODEbits.UARTEN   = 0; // no IO when the clock is stopped
            SC_HRST_CN          = 0; // no soft reset when clock is stopped
            SC_CLK_CN           = 1; // wait for the clock to start
            break;

        case SCS_RESET:
            // start measuring device clock rate
            T2CONbits.TON   = 0;
            T3CONbits.TON   = 0;
            TMR2            = 0;
            TMR3            = 0;
            PR2             = 700; // 700t leaves us 100t before IO is allowed
            PR3             = 0xFFFF;
            T2CONbits.TON   = 1;
            T3CONbits.TON   = 1;

            U2MODEbits.UARTEN   = 0; // the IO line is undefined
            SC_HRST_CN          = 1; // allow soft reset
            break;

        case SCS_ATR:
            U2MODEbits.UARTEN   = 1;
            SC_HRST_CN          = 1; // allow soft reset
            break;
    }

    sc_state.session = new_state;
}

void ISR_RT isr_input (void) {
    IFS1bits.CNIF = 0;

    // clock started, beginning of cold reset sequence
    if (SC_CLK_CN && SCS_OFFLINE == sc_state.session && SC_CLK && !SC_HRST) {
        sc_transition( SCS_RESET );
    }

    // reset asserted, beginning of warm reset sequence
    if (SC_HRST_CN && SCS_OFFLINE != sc_state.session && !SC_HRST) {
        sc_transition( SCS_RESET );
    }
}

void ISR_RT isr_timer (void) {
    IFS0bits.T2IF = 0;

    // make sure we get the values as close together as possible
    int cycles  = TMR3;
    int ticks   = TMR2;

    // I don't like doing floating-point math in an ISR but we need this value
    // 320 - 1600 cycles after the interrupt, depending on the bus clock rate.
    U2BRG = (int)( 93.0 * (double)cycles / (double)(ticks + PR2) + 1.0 );

    sc_transition( SCS_ATR );
}

void ISO7816setup (void) {
    // IO pins are open collector
    modeConfig.HiZ = 1;

    // set up pin modes
    SC_CLK_DIR  = 1;
    SC_HRST_DIR = 1;
    SC_HIO_DIR  = 1;

    // set up UART 2 on host IO
    U2MODE              = 0;            // reset the UART
    U2STA               = 0;            // "
    U2MODEbits.BRGH     = 1;            // use BRG factor for high baud rates
    U2MODEbits.PDSEL    = 1;            // 8 bits, even parity
    U2MODEbits.STSEL    = 1;            // 2 stop bits
    RPINR19bits.U2RXR   = SC_HIO_RPIN;  // connect pin to Rx input
    //SC_HIO_RPOUT        = U2TX_IO;      // connect pin to Tx output

    // set up timer 2 as synchronous counter on CLK
    T2CON               = 0;            // reset the timer
    T2CONbits.TCS       = 1;            // enable external sync
    RPINR3bits.T2CKR    = SC_CLK_RPIN;  // connect clock input to CLK
    ISR_T2              = isr_timer;    // set up ISR
    IFS0bits.T2IF       = 0;            // clear interrupt flag
    IPC1bits.T2IP       = 7;            // maximum interrupt priority
    IEC0bits.T2IE       = 1;            // enable interrupts

    // set up timer 3 as timer
    T3CON               = 0;            // reset the timer
    IFS0bits.T3IF       = 0;            // disable interrupts
    IPC2bits.T3IP       = 0;            // "
    IEC0bits.T3IE       = 0;            // "


    // set up port change notification interrupts
    ISR_CN              = isr_input;// set up CN ISR
    IFS1bits.CNIF       = 0;        // clear the CN interrupt flag
    IPC4bits.CNIP       = 6;        // RST changes are timing-critical so use
                                    //   priority 6, timers are at 7
    IEC1bits.CNIE       = 1;        // enable CN interrupts

}

void ISO7816cleanup (void) {
    
    // disable CN interrupts
    IFS1bits.CNIF       = 0;
    IEC1bits.CNIE       = 0;
    IPC4bits.CNIP       = 0;
    ISR_CN              = NULL_ISR;

    // disconnect timer 2 from CLK
    IEC0bits.T2IE       = 0;        // disable interrupt
    IPC1bits.T2IP       = 0;        // "
    IFS0bits.T2IF       = 0;        // "
    ISR_T2              = NULL_ISR; // "
    T2CON               = 0;        // reset the timer
    TMR2                = 0;        // "
    PR2                 = 0;        // "
    RPINR3bits.T2CKR    = 0;        // disconnect clock input

    // shut down timer 3
    T3CON               = 0;

    // disconnect UART 2 from host IO
    U2MODE              = 0;        // reset the UART
    RPINR19bits.U2RXR   = 0xFF;     // disconnect Rx input
    SC_HIO_RPOUT        = 0;        // disconnect Tx output

    // reset IO pins
    SC_HRST_CN      = 0;
    SC_HIO_CN       = 0;
    SC_CLK_CN       = 0;
    SC_HRST_DIR     = 1;
    SC_HIO_DIR      = 1;
    SC_CLK_DIR      = 1;
}


void ISO7816start (void) {
    sc_transition( SCS_OFFLINE );
    modeConfig.periodicService = 1;
}

void ISO7816stop (void) {
    sc_transition( SCS_MANUAL );
    modeConfig.periodicService = 0;
}


int once_flag = 0;

unsigned int ISO7816periodic (void) {
    if (!once_flag && U2BRG) {
        bpWstring( "BRGH " );
        bpWintdec( U2BRG );
        bpWline("");
        once_flag = 1;
    }
    
    if (U2STAbits.URXDA) {
        int status = U2STA;

        bpWstring( "Received " );
        bpWhex( U2RXREG );

        if (status & 0xC) {
            bpWstring( " p f" );
        } else if (status & 0x8) {
            bpWstring( " p" );
        } else if (status & 0x4) {
            bpWstring( "   f" );
        }

        bpWline("");
    }

    return 0;
}

unsigned ISO7816write (unsigned int c) {
    return 0;
}

unsigned int ISO7816read (void) {
    return 0;
}

void ISO7816macro (unsigned int c) {

}

void ISO7816pins (void) {
    bpWline( "CLK\t-\tRST\tI/O" );
}

void ISO7816settings (void) {

}

#endif