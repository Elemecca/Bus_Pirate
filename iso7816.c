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
 * Pins:
 *   CLK    bus shared clock; v3 doesn't have enough pins for split clock
 *   HRST   RST line from host device
 *   HIO    I/O line from host device
 *   CRST   RST line to responding device
 *   CIO    I/O line to responding device
 *
 * Device allocation:
 *   U2     sends and receives data on HIO
 *   T2     counts ticks on CLK
 *   T3     continuous timer used for clock rate measurements
 *   IC1    detects application of clock to CLK
 *   IC2    detects significant events on HRST
 *   IC3    detects significant events on HIO
 *   SPI1   generates clock signal for host mode
 *
 * It would be much better if we could use 32-bit timers, but we need to
 * capture from both timers and the Input Capture modules only support T2
 * and T3. We therefore use the timer interrupts to maintain a count of how
 * many times each has rolled over and then multiply to get the 32-bit value.
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
 *       - calculate data rate
 *       - transition to SCS_ATR
 *   - SCS_ATR: device sends answer to reset
 *     - U2 is listening on HIO
 *     - device starts first byte between 400t and 40,000t after RST goes high
 *       - detected by IC3, notify user of timing
 */

#include "base.h"

#ifdef BP_USE_ISO7816

#include <string.h>
#include <stdio.h> // for sprintf
#include "interrupts.h"

#include "iso7816/private.h"
#include "iso7816/atr.h"

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

struct sc_state_t sc_state;

void sc_notify (unsigned short message) {
    if (sc_state.note_overflow) return;

    register short next = (sc_state.notes.write + 1) % SC_NOTIFY_BUFFER_SIZE;
    if (next == sc_state.notes.read) {
        sc_state.note_overflow = 1;
    } else {
        sc_state.notes.buffer[ sc_state.notes.write ] = message;
        sc_state.notes.write = next;
    }
}

#ifdef SC_PROF_ENABLE
struct sc_prof_t sc_prof[ SC_PROF_LENGTH ];
unsigned sc_prof_idx;

inline void sc_profile (const char *event) {
    if (sc_prof_idx >= SC_PROF_LENGTH) return;
    sc_prof[ sc_prof_idx ].time = TMR4 | ((long)TMR5HLD << 16);
    sc_prof[ sc_prof_idx ].event = event;
    sc_prof_idx++;
}
#endif

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

void ISR_RT isr_end_rst (void);
void ISR_RT isr_rx  (void);

inline void sc_transition (unsigned new_state) {
    sc_profile( "> sc_transition" );

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
            IC1CONbits.ICM      = 0; // disable clock start detection
            IC3CONbits.ICM      = 0; // disable reset ack detection
            IEC2bits.IC3IE      = 1; // turn IC3 interrupts back on
            break;

        case SCS_RESET:
            // no teardown yet
            break;

        case SCS_ATR:
            sc_state.rx.next_state = SCS_MANUAL;
            U2MODEbits.UARTEN = 0;
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
            IEC2bits.IC3IE      = 0; // will be re-enabled in isr_ack_rst
            IC3CONbits.ICM      = 3; // enable reset ack detection
            break;

        case SCS_RESET:
            U2MODEbits.UARTEN   = 0; // the IO line is undefined
            ISR_IC2 = isr_end_rst;   // enable reset end detection
            IC2CONbits.ICM      = 3; // detect rising edge
            break;

        case SCS_ATR:
            sc_state.rx.read    = 0;
            sc_state.rx.write   = 0;
            sc_state.rx.callback = sc_atr_read;
            sc_state.rx.next_state = SCS_IDLE;
            ISR_U2RX            = isr_rx;
            IEC1bits.U2RXIE     = 1;
            U2MODEbits.UARTEN   = 1;
            break;
    }

    sc_state.session = new_state;

    sc_profile( "< sc_transition" );
}

void ISR_RT isr_clk_start (void) {
    sc_profile( "> isr_clk_start" );
    IFS0bits.IC1IF = 0;

    // store the current value of the cycle timer for use in rate calc
    sc_state.rate_cycles = IC1BUF;
    sc_state.mult_t3     = 0;
    sc_state.rate_ticks  = 0;
    sc_state.mult_t2     = 0;

    // this is a one-shot, disable the trigger
    IC1CONbits.ICM = 0; // disable module
    IFS0bits.IC1IF = 0; // clear the interrupt flag again
    
    // clock started, beginning of cold reset sequence
    sc_notify( SCM_CLK_START );
    sc_transition( SCS_RESET );

    // allow interrupts from reset ack
    IEC2bits.IC3IE = 1;

    sc_profile( "< isr_clk_start" );
}

void ISR_RT isr_ack_rst (void) {
    sc_profile( "> isr_ack_rst" );
    IFS2bits.IC3IF = 0;

    // save the tick count when the ack came in
    sc_state.reset_ack = IC3BUF + (long)sc_state.mult_t2 * PR2;

    // this is a one-shot, disable the trigger
    IC3CONbits.ICM = 0;

    sc_notify( SCM_RESET_ACK );

    sc_profile( "< isr_ack_rst" );
}

void ISR_RT isr_end_rst (void) {
    sc_profile( "> isr_end_rst" );
    IFS0bits.IC2IF = 0;

    // this is a one-shot, disable the trigger
    IC2CONbits.ICM = 0;

    // make sure we get the values as close together as possible
    unsigned long cycles = TMR3;
    unsigned long ticks  = TMR2;

    // compensate for timer rollver
    cycles += (long)sc_state.mult_t3 * PR3;
    ticks  += (long)sc_state.mult_t2 * PR2;

    // compensate for free-running cycle timer
    cycles -= sc_state.rate_cycles;

    // I don't like doing floating-point math in an ISR but we need this value
    // 400 ticks (1280 - 6400 cycles depending on the bus clock rate) after
    // reset is released and the computation takes about 1100 cycles.
    U2BRG = (int)( 93.0 * (double)cycles / (double)ticks + 1.0 );

    sc_profile( "* BRG set" );

    sc_state.rate_cycles = cycles;
    sc_state.rate_ticks  = ticks;


    // save the tick count when reset ended
    sc_state.reset_end = IC2BUF + (long)sc_state.mult_t2 * PR2;

    
    sc_notify( SCM_RESET_END );
    sc_notify( SCM_CLK_RATE );
    sc_transition( SCS_ATR );

    sc_profile( "< isr_end_rst" );
}

void ISR_RT isr_t2_roll (void) {
    IFS0bits.T2IF = 0;
    sc_state.mult_t2++;
}

void ISR_RT isr_t3_roll (void) {
    IFS0bits.T3IF = 0;
    sc_state.mult_t3++;
}

void ISR_RT isr_rx (void) {
    sc_profile( "> isr_rx" );
    IFS1bits.U2RXIF = 0;

    // TODO: check errors, do... something with them
    switch ( sc_state.rx.callback( U2RXREG ) ) {
        case SC_READ_ABORT:
            sc_profile( "* SC_READ_ABORT" );
            sc_transition( SCS_MANUAL );
            break;

        case SC_READ_DONE:
            sc_profile( "* SC_READ_DONE" );
            sc_transition( sc_state.rx.next_state );
            break;
    }

    sc_profile( "< isr_rx" );
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
    SC_HIO_ODC  = 1;

    // set up UART 2 on host IO
    U2MODE              = 0;            // reset the UART
    U2STA               = 0;            // "
    U2MODEbits.BRGH     = 1;            // use BRG factor for high baud rates
    U2MODEbits.PDSEL    = 1;            // 8 bits, even parity
    U2MODEbits.STSEL    = 1;            // 2 stop bits
    RPINR19bits.U2RXR   = SC_HIO_RPIN;  // connect pin to Rx input
    SC_HIO_RPOUT        = U2TX_IO;      // connect pin to Tx output
    IFS1bits.U2RXIF     = 0;            // clear the interrupt flag
    IPC7bits.U2RXIP     = 4;            // normal interrupt priority
    IEC1bits.U2RXIE     = 0;            // disable interrupt for now

    // set up Timer 2 as synchronous counter on CLK
    T2CON               = 0;            // reset the timer
    PR2                 = 0xFFFF;       // maximum period; don't restart early
    T2CONbits.TCS       = 1;            // enable external sync
    RPINR3bits.T2CKR    = SC_CLK_RPIN;  // connect clock input to CLK
    ISR_T2              = isr_t2_roll;  // set up rollover interrupt handler
    IFS0bits.T2IF       = 0;            // clear interupt flag
    IPC1bits.T2IP       = 7;            // rollover count must be accurate
    IEC0bits.T2IE       = 1;            // enable interrupt

    // set up Timer 3 as timer
    T3CON               = 0;            // reset the timer
    PR3                 = 0xFFFF;       // maximum period; don't restart early
    ISR_T3              = isr_t3_roll;  // set up rollover interrupt handler
    IFS0bits.T3IF       = 0;            // clear interrupt flag
    IPC2bits.T3IP       = 7;            // rollover count must be accurate
    IEC0bits.T3IE       = 1;            // enable interrupt

    // set up Input Capture 1 to detect clock start
    IC1CON              = 0;                // reset the capture module
    IC1CONbits.ICTMR    = 0;                // use Timer 3
    RPINR7bits.IC1R     = SC_CLK_RPIN;      // connect input to CLK
    ISR_IC1             = isr_clk_start;    // set the ISR
    IFS0bits.IC1IF      = 0;                // clear the interrupt flag
    IPC0bits.IC1IP      = 4;                // medium priority
    IEC0bits.IC1IE      = 1;                // enable interrupts

    // set up Input Capture 2 to monitor HRST
    IC2CON              = 0;                // reset the module
    IC2CONbits.ICTMR    = 1;                // use Timer 2
    RPINR7bits.IC2R     = SC_HRST_RPIN;     // connect input to HRST
    IFS0bits.IC2IF      = 0;                // clear the interrupt flag
    IPC1bits.IC2IP      = 4;                // medium priority
    IEC0bits.IC2IE      = 1;                // enable interrupts

    // set up Input Capture 3 to monitor HIO
    IC3CON              = 0;                // reset the module
    IC3CONbits.ICTMR    = 1;                // use Timer 2
    RPINR8bits.IC3R     = SC_HIO_RPIN;      // connect input to HIO
    ISR_IC3             = isr_ack_rst;      // set the ISR
    IFS2bits.IC3IF      = 0;                // clear the interrupt flag
    IPC9bits.IC3IP      = 4;                // medium priority
    IEC2bits.IC3IE      = 1;                // enable interrupts

#ifdef SC_PROF_ENABLE
    // set up Timer 4/5 as 32-bit cycle counter for profiling
    T4CON               = 0;                // reset T4
    T5CON               = 0;                // reset T5
    TMR5                = 0;                // initialize timer to 0
    TMR4                = 0;                // "
    PR5                 = 0xFFFF;           // set maximum period
    PR4                 = 0xFFFF;           // "
    T4CONbits.T32       = 1;                // enable 32-bit mode
    IFS1bits.T5IF       = 0;                // disable interrupts
    IPC7bits.T5IP       = 0;                // "
    IEC1bits.T5IE       = 0;                // "
#endif
}

void ISO7816cleanup (void) {
    
    // shut down Input Capture 1
    IC1CON              = 0;        // reset the module
    RPINR7bits.IC1R     = 0x1F;     // disconnect the input from CLK
    IEC0bits.IC1IE      = 0;        // disable interrupt
    IFS0bits.IC1IF      = 0;        // "
    IPC0bits.IC1IP      = 0;        // "
    ISR_IC1             = NULL_ISR; // "

    // shut down Input Capture 2
    IC2CON              = 0;        // reset the module
    RPINR7bits.IC2R     = 0x1F;     // disconnect this input from HRST
    IEC0bits.IC2IE      = 0;        // disable interrupt
    IFS0bits.IC2IF      = 0;        // "
    IPC1bits.IC2IP      = 0;        // "
    ISR_IC2             = NULL_ISR; // ""

    // shut down Input Capture 3
    IC3CON              = 0;        // reset the module
    RPINR8bits.IC3R     = 0x1F;     // disconnect the input from HIO
    IEC2bits.IC3IE      = 0;        // diable interrupt
    IFS2bits.IC3IF      = 0;        // "
    IPC9bits.IC3IP      = 0;        // "
    ISR_IC3             = NULL_ISR; // "

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
    IEC0bits.T3IE       = 0;        // disable interrupt
    IPC2bits.T3IP       = 0;        // "
    IFS0bits.T3IF       = 0;        // "
    ISR_T3              = NULL_ISR; // "
    T3CON               = 0;        // reset the timer
    TMR3                = 0;        // "
    PR3                 = 0;        // "

    // disconnect UART 2 from host IO
    U2MODE              = 0;        // reset the UART
    RPINR19bits.U2RXR   = 0x1F;     // disconnect Rx input
    SC_HIO_RPOUT        = 0;        // disconnect Tx output
    IEC1bits.U2RXIE     = 0;        // disable interrupt
    IFS1bits.U2RXIF     = 0;        // "
    IPC7bits.U2RXIP     = 0;        // "

#ifdef SC_PROF_ENABLE
    // clean up Timer 4/5
    T4CON               = 0;
    T5CON               = 0;
    TMR4                = 0;
    TMR5                = 0;
#endif

    // reset IO pins
    SC_HRST_DIR     = 1;
    SC_HRST_ODC     = 0;
    SC_HIO_DIR      = 1;
    SC_HIO_ODC      = 0;
    SC_CLK_DIR      = 1;
    SC_CLK_ODC      = 0;
}

//////////////////////////////////////////////////////////////////////
// User Interface                                                   //
//////////////////////////////////////////////////////////////////////

void ISO7816start (void) {
    if (SC_VBUS || SC_HRST) {
        bpWline( "!!! the bus appears to be active, not starting" );
        bpWline( "We can't start monitoring an active session because we" );
        bpWline( "need to observe the reset sequence in order to know the" );
        bpWline( "protocol parameters that are in use." );
    } else {
        // reset the state structures
        memset( &sc_state, 0, sizeof( struct sc_state_t ) );

#ifdef SC_PROF_ENABLE
        sc_prof_idx = 0;

        // start profiling counter
        TMR5            = 0;    // reset the timer value
        TMR4            = 0;    // "
        T4CONbits.TON   = 1;    // start the timer
#endif
        
        sc_transition( SCS_OFFLINE );
        modeConfig.periodicService = 1;
    }
}

void ISO7816stop (void) {
    if (SCS_MANUAL == sc_state.session) return;

    sc_transition( SCS_MANUAL );
    modeConfig.periodicService = 0;

    bpWstring( "t2: " );
    bpWintdec( sc_state.mult_t2 );
    bpWstring( ", t3: " );
    bpWintdec( sc_state.mult_t3 );
    bpWline("");

    bpWstring( "ATR bytes:" );
    bpWhexdump( sc_state.atr, sc_state.atr_len );

#ifdef SC_PROF_ENABLE
    // stop profiling counter
    T4CONbits.TON = 0;

    // write profiling events
    unsigned int idx;
    char buffer[ 80 ];
    for (idx = 0; idx < sc_prof_idx; idx++) {
        snprintf( (char*)&buffer, 80, "%10lu %s",
                sc_prof[ idx ].time, sc_prof[ idx ].event );
        bpWline( (char*)&buffer );
    }

    if (sc_prof_idx >= SC_PROF_LENGTH)
        bpWline( "!!! profiling buffer overflowed" );
#endif
}


unsigned int ISO7816periodic (void) {
    // check for async notifications
    while (sc_state.notes.read != sc_state.notes.write) {
        switch (sc_state.notes.buffer[ sc_state.notes.read ]) {
            case SCM_CONFUSED:
                bpWline( "  There's someone in my head, but it's not me." );
                bpWline( "    And if the cloud bursts thunder in your ear" );
                bpWline( "    You shout and no one seems to hear" );
                bpWline( "  And if the band you're in starts playing different tunes" );
                bpWline( "    I'll see you on the dark side of the moon." );
                break;

            case SCM_CLK_START:
                bpWstring( "** bus clock started, begin cold reset, t3: " );
                bpWlongdec( sc_state.rate_cycles );
                bpWline( "c" );
                break;

            case SCM_CLK_RATE:
                bpWstring( "** clock rate " );
                bpWintdec( (int)( 1600.0
                        * (double)sc_state.rate_cycles
                        / (double)sc_state.rate_ticks ) );

                bpWstring( " KHz, BRGH = ");
                bpWintdec( U2BRG );
                
                bpWstring( ", " );
                bpWlongdec( sc_state.rate_ticks );
                bpWstring( "t = " );
                bpWlongdec( sc_state.rate_cycles );
                bpWline("c");
                break;

            case SCM_RESET_ACK:
                bpWstring( "** device acknowledged reset at " );
                bpWintdec( sc_state.reset_ack );
                bpWline( "t" );
                break;

            case SCM_RESET_END:
                bpWstring( "** host released RST at " );
                bpWlongdec( sc_state.reset_end );
                bpWline( "t" );
                break;

            case SCM_INVERSE_CODING:
                bpWline( "!!! device uses inverse coding" );
                break;

            case SCM_ATR_OVERFLOW:
                bpWline( "!!! received more than 32 bytes for ATR" );
                break;

            case SCM_ATR_INVALID:
                bpWline( "!!! invalid or unsupported value in ATR, aborting" );
                bpWstring( "ATR received so far:" );
                bpWhexdump( sc_state.atr, sc_state.atr_len );
                break;

            default:
                bpWstring( "!!! received unknown notification " );
                bpWhex( sc_state.notes.buffer[ sc_state.notes.read ] );
                bpWline("");
                break;
        }
        sc_state.notes.read = (sc_state.notes.read + 1) % SC_NOTIFY_BUFFER_SIZE;
    }

    if (sc_state.note_overflow) {
        bpWline( "!!! notification buffer overflowed" );
        sc_state.note_overflow = 0;
    }

    while (sc_state.rx.read != sc_state.rx.write) {
        bpWstring( "read " );
        bpWbyte( sc_state.rx.buffer[ sc_state.rx.read++ ] );
        sc_state.rx.read %= SC_RX_BUFFER_SIZE;
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
