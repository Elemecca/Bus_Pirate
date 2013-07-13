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

extern struct _modeConfig modeConfig;

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

void ISO7816Process (void) {

}

unsigned ISO7816write (unsigned int c) {
    return 0;
}

unsigned int ISO7816read (void) {
    return 0;
}


void ISR_RT isr_cn (void) {
    IFS1bits.CNIF = 0;

    if (!SC_HRST && SC_CLK) {
        SC_CLK_CN = 0;

        T2CONbits.TON   = 0;
        T3CONbits.TON   = 0;
        TMR2            = 0;
        TMR3            = 0;
        PR2             = 700;
        PR3             = 0xFFFF;
        T2CONbits.TON   = 1;
        T3CONbits.TON   = 1;
    }
}

int baud_internal, baud_external;

void ISR_RT isr_t2 (void) {
    IFS0bits.T2IF = 0;

    baud_internal = TMR3;
    baud_external = TMR2 + PR2;

    T3CONbits.TON = 0;
    T2CONbits.TON = 0;
}

void ISO7816setup (void) {
    // IO pins are open collector
    modeConfig.HiZ = 1;

    // set up pin modes
    SC_CLK_DIR  = 1;
    SC_HRST_DIR = 1;
    SC_HIO_DIR  = 1;

    // set up timer 1 as synchronous counter on CLK
    T2CON               = 0;            // reset the timer
    T2CONbits.TCS       = 1;            // enable external sync
    RPINR3bits.T2CKR    = SC_CLK_RPIN;  // connect clock input to CLK
    ISR_T2              = isr_t2;       // set up ISR
    IFS0bits.T2IF       = 0;            // clear interrupt flag
    IPC1bits.T2IP       = 7;            // maximum interrupt priority
    IEC0bits.T2IE       = 1;            // enable interrupts

    // set up timer 3 as timer
    T3CON               = 0;            // reset the timer
    IFS0bits.T3IF       = 0;            // disable interrupts
    IPC2bits.T3IP       = 0;            // "
    IEC0bits.T3IE       = 0;            // "


    // set up port change notification interrupts
    ISR_CN              = isr_cn;   // set up CN ISR
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

    // reset IO pins
    SC_HRST_CN      = 0;
    SC_HIO_CN       = 0;
    SC_CLK_CN       = 0;
    SC_HRST_DIR     = 1;
    SC_HIO_DIR      = 1;
    SC_CLK_DIR      = 1;
}

void ISO7816macro (unsigned int c) {

}

void ISO7816start (void) {
    SC_CLK_CN = 1;

    modeConfig.periodicService = 1;
}

void ISO7816stop (void) {
    T2CONbits.TON   = 0;
    T3CONbits.TON   = 0;
    SC_CLK_CN       = 0;

    modeConfig.periodicService = 0;
}

unsigned int ISO7816periodic (void) {
    if (baud_internal) {
        bpWstring( "counters: " );
        bpWintdec( baud_internal );
        bpWstring( " " );
        bpWintdec( baud_external );
        bpWline("");

        int brg = (int)( 93.0 * (double)baud_internal / (double)baud_external + 1.0 );
        bpWstring( "BRGH: " );
        bpWintdec( brg );
        bpWline("");

        baud_internal = 0;
    }

    return 0;
}

void ISO7816pins (void) {
    bpWline( "CLK\t-\tRST\tI/O" );
}

void ISO7816settings (void) {

}

#endif