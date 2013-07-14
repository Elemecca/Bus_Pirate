/*
 * This file is part of the Bus Pirate project.
 * http://code.google.com/p/the-bus-pirate/
 *
 * Written and maintained by the Bus Pirate project.
 *
 * To the extent possible under law, the project has waived all
 * copyright and related or neighboring rights to Bus Pirate.
 * This work is published from the United States.
 *
 * For details see: http://creativecommons.org/publicdomain/zero/1.0/
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * 
 *
 * Device allocation:
 *   U2     sends and receives data on HIO
 *   T2     counts ticks on CLK
 *   T3     continuous timer used for clock rate measurements
 *   IC1    detects application of clock to CLK
 *   IC2    detects significant events on HRST
 *   IC3    detects significant events on HIO
 *   OC1    handles timing of clock rate measurements
 *   SPI1   generates clock signal for host mode
 *
 * Start-up sequence for sniffer mode:
 *   - SCS_OFFLINE: bus is inactive
 *     - T2 is set up to count CLK ticks
 *     - T3 is started
 *     - host initiates cold reset
 *       - host drives HRST low
 *       - host applies power to VBUS
 *       - host applies clock on CLK
 *         - first rising edge detected by IC1, captures value of T3
 *         - transition to SCS_RESET
 *   - SCS_RESET: device activation / reset
 *     - device sets IO high at or before 200t from clock start
 *       - detected by IC3, notify user of timing
 *     - host releases HRST high at or after 400t from clock start
 *       - detected by IC2, notify user of timing
 *       - set OC1 for 300t
 *     - calculate data rate 300t after HRST goes high
 *       - triggered by OC1
 *       - transition to SCS_ATR
 *   - SCS_ATR: device sends answer to reset
 *     - U2 is listening on HIO
 *     - device starts first byte between 400t and 40,000t after RST goes high
 *       - detected by IC3, notify user of timing
 */

#include "base.h"

#ifdef BP_USE_ISO7816

#include "interrupts.h"

//////////////////////////////////////////////////////////////////////
// Device Pin Mappings                                              //
//////////////////////////////////////////////////////////////////////

#if defined BUSPIRATEV3
    #define SC_VBUS         BP_VPU

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
    #error "Unsupported hardware version."
#endif

//////////////////////////////////////////////////////////////////////
// Global State Data                                                //
//////////////////////////////////////////////////////////////////////

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

    unsigned rate_ticks;
    unsigned rate_cycles;
} sc_state;


#define SCM_NOTE_OVERFLOW   1   // the notice buffer overflowed
#define SCM_CLK_START       2   // clock signal detected
#define SCM_CLK_RATE        3   // new clock rate calculated

#define SC_NOTIFY_BUFFER_SIZE 32
struct {
    unsigned short read;
    unsigned short write;
    unsigned short buffer[ SC_NOTIFY_BUFFER_SIZE ];
} sc_notes;

inline void sc_notify (unsigned short message) {
    if (sc_notes.write == (sc_notes.read - 1) % SC_NOTIFY_BUFFER_SIZE) {
        sc_notes.buffer[ sc_notes.write ] = SCM_NOTE_OVERFLOW;
    } else {
        sc_notes.buffer[ sc_notes.write ] = message;
        sc_notes.write = (sc_notes.write + 1) % SC_NOTIFY_BUFFER_SIZE;
    }
}

//////////////////////////////////////////////////////////////////////
// Clock Generator                                                  //
//////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////
// State Management and Interrupts                                  //
//////////////////////////////////////////////////////////////////////

inline void sc_transition (unsigned new_state) {
    /* For most settings one mode is torn down and then another is set up.
     * There are a few settings, however, which would cause issues if they were
     * briefly disabled. Instead the setup for each mode sets these to the
     * correct value, which may be the current one. The complete list:
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
            // no teardown, everything is one-shot
            break;

        case SCS_RESET:
            // no teardown yet
            break;

        case SCS_ATR:
            // no teardown yet
            break;
    }

    // set up the new mode
    switch (new_state) {
        case SCS_MANUAL:
            U2MODEbits.UARTEN   = 0; // stop the UART
            break;

        case SCS_OFFLINE:
            U2MODEbits.UARTEN   = 0; // no IO when the clock is stopped
            TMR2                = 0; // start tick counter
            T2CONbits.TON       = 1; // "
            TMR3                = 0; // start timer
            T3CONbits.TON       = 1; // "
            IC1CONbits.ICM      = 3; // enable clock start detection
            OC1CONbits.OCM      = 1; // enable rate calculation trigger
            break;

        case SCS_RESET:
            U2MODEbits.UARTEN   = 0; // the IO line is undefined
            break;

        case SCS_ATR:
            U2MODEbits.UARTEN   = 1;
            break;
    }

    sc_state.session = new_state;
}

void ISR_RT isr_clk_start (void) {
    IFS0bits.IC1IF = 0;

    // store the current value of the cycle timer for use in rate calc
    sc_state.rate_cycles = IC1BUF;
    sc_state.rate_ticks  = 0;

    // this is a one-shot, disable the trigger
    IC1CONbits.ICM = 0;
    
    // clock started, beginning of cold reset sequence
    sc_notify( SCM_CLK_START );
    sc_transition( SCS_RESET );
}

void ISR_RT isr_rate (void) {
    IFS0bits.OC1IF = 0;

    // make sure we get the values as close together as possible
    register int cycles  = TMR3;
    register int ticks   = TMR2;

    // this is currently a one-shot, disable the trigger
    OC1CONbits.OCM = 0;

    // compensate for timer rollver
    if (cycles < sc_state.rate_cycles) {
        cycles += PR3 - sc_state.rate_cycles;
    } else {
        cycles -= sc_state.rate_cycles;
    }
    if (ticks < sc_state.rate_ticks) {
        ticks += PR2 - sc_state.rate_ticks;
    } else {
        ticks -= sc_state.rate_ticks;
    }

    // I don't like doing floating-point math in an ISR but we need this value
    // 320 - 1600 cycles after the interrupt, depending on the bus clock rate.
    U2BRG = (int)( 93.0 * (double)cycles / (double)(ticks + PR2) + 1.0 );

    sc_state.rate_cycles = cycles;
    sc_state.rate_ticks  = ticks;

    sc_notify( SCM_CLK_RATE );
    sc_transition( SCS_ATR );
}

//////////////////////////////////////////////////////////////////////
// Mode Setup and Teardown                                          //
//////////////////////////////////////////////////////////////////////

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
    SC_HIO_RPOUT        = U2TX_IO;      // connect pin to Tx output

    // set up Timer 2 as synchronous counter on CLK
    T2CON               = 0;            // reset the timer
    PR2                 = 0xFFFF;       // maximum period; don't restart early
    T2CONbits.TCS       = 1;            // enable external sync
    RPINR3bits.T2CKR    = SC_CLK_RPIN;  // connect clock input to CLK
    IFS0bits.T2IF       = 0;            // disable interrupts
    IPC1bits.T2IP       = 0;            // "
    IEC0bits.T2IE       = 0;            // "

    // set up Timer 3 as timer
    T3CON               = 0;            // reset the timer
    PR3                 = 0xFFFF;       // maximum period; don't restart early
    IFS0bits.T3IF       = 0;            // disable interrupts
    IPC2bits.T3IP       = 0;            // "
    IEC0bits.T3IE       = 0;            // "

    // set up Input Capture 1 to detect clock start
    IC1CON              = 0;                // reset the capture module
    IC1CONbits.ICTMR    = 0;                // use Timer 3
    RPINR7bits.IC1R     = SC_CLK_RPIN;      // connect input to CLK
    ISR_IC1             = isr_clk_start;    // set the ISR
    IFS0bits.IC1IF      = 0;                // clear the interrupt flag
    IPC0bits.IC1IP      = 4;                // medium priority
    IEC0bits.IC1IE      = 1;                // enable interrupts

    // set up Output Compare 1 to interupt at end of rate measurement period
    OC1CON              = 0;                // reset the module
    OC1CONbits.OCTSEL   = 0;                // use Timer 2
    OC1R                = 700;              // trigger after 700 ticks
    ISR_OC1             = isr_rate;         // set the ISR
    IFS0bits.OC1IF      = 0;                // clear the interrupt flag
    IPC0bits.OC1IP      = 7;                // every cycle counts, max priority
    IEC0bits.OC1IE      = 1;                // enable interrupts

}

void ISO7816cleanup (void) {
    
    // shut down Input Capture 1
    IC1CON              = 0;        // reset the module
    RPINR7bits.IC1R     = 0x1F;     // disconnect the input from CLK
    IEC0bits.IC1IE      = 0;        // disable interrupt
    IFS0bits.IC1IF      = 0;        // "
    IPC0bits.IC1IP      = 0;        // "
    ISR_IC1             = NULL_ISR; // "

    // shut down Output Compare 1
    OC1CON              = 0;        // reset the module
    IEC0bits.OC1IE      = 0;        // disable interrupt
    IFS0bits.OC1IF      = 0;        // "
    IPC0bits.OC1IP      = 0;        // "
    ISR_OC1             = NULL_ISR; // "

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
    RPINR19bits.U2RXR   = 0x1F;     // disconnect Rx input
    SC_HIO_RPOUT        = 0;        // disconnect Tx output

    // reset IO pins
    SC_HRST_DIR     = 1;
    SC_HIO_DIR      = 1;
    SC_CLK_DIR      = 1;
}

//////////////////////////////////////////////////////////////////////
// User Interface                                                   //
//////////////////////////////////////////////////////////////////////

void ISO7816start (void) {
    if (SC_VBUS || SC_HRST || SC_HIO) {
        bpWline( "!!! the bus appears to be active, not starting" );
        bpWline( "We can't start monitoring an active session because we" );
        bpWline( "need to observe the reset sequence in order to know the" );
        bpWline( "protocol parameters that are in use." );
    } else {
        sc_transition( SCS_OFFLINE );
        modeConfig.periodicService = 1;
    }
}

void ISO7816stop (void) {
    sc_transition( SCS_MANUAL );
    modeConfig.periodicService = 0;
}


unsigned int ISO7816periodic (void) {
    // check for async notifications
    if (sc_notes.read != sc_notes.write) {
        switch (sc_notes.buffer[ sc_notes.read ]) {
            case SCM_NOTE_OVERFLOW:
                bpWline( "!!! notification buffer overflowed" );
                break;
                
            case SCM_CLK_START:
                bpWline( "** bus clock started, begin cold reset" );
                break;

            case SCM_CLK_RATE:
                bpWstring( "** clock rate " );
                bpWintdec( (int)( 1600.0
                        * (double)sc_state.rate_cycles
                        / (double)sc_state.rate_ticks ) );

                bpWstring( " KHz, BRGH = ");
                bpWintdec( U2BRG );
                
                bpWstring( ", " );
                bpWintdec( sc_state.rate_ticks );
                bpWstring( "t = " );
                bpWintdec( sc_state.rate_cycles );
                bpWline("c");
                break;

            default:
                bpWstring( "!!! received unknown notification " );
                bpWbyte( sc_notes.buffer[ sc_notes.read ] );
                bpWline("");
                break;
        }
        sc_notes.read = (sc_notes.read + 1) % SC_NOTIFY_BUFFER_SIZE;
    }

    if (U2STAbits.URXDA) {
        register int status = U2STA;

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

#endif /* defined BP_USE_ISO7816 */
