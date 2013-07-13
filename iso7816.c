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
    BP_CLK_RPOUT        = SCK1OUT_IO;   // connect SP1 SCK output to CLK
    BP_CLK_DIR          = 0;            // configure CLK as output
}

/** Cleans up after the SPI clock generator.
 * @see scSCKSetup
 */
void scSCKCleanup (void) {
    // SPI module 1 was clock output
    SPI1STAT        = 0; // reset SPI module
    SPI1CON1        = 0; // "
    SPI1CON2        = 0; // "
    BP_CLK_DIR      = 1; // configure CLK as input
    BP_CLK_RPOUT    = 0; // disconnect CLK
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

int sc_reset_flag   = 0;
int sc_reset        = 0;

void ISR_RT isr_cn (void) {
    IFS1bits.CNIF = 0;

    // detect RST being asserted
    if (sc_reset != BP_CS) {
        sc_reset = BP_CS;
        sc_reset_flag = 1;
    }
}

void ISO7816setup (void) {
    // IO pins are open collector
    modeConfig.HiZ = 1;

    // set up CS as RST input
    BP_CS_DIR           = 1;        // configure CS as input

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

    // disconnect RST on CS
    BP_CS_DIR           = 1;    // configure as input
    BP_CS_CN            = 0;    // disable change notification
}

void ISO7816macro (unsigned int c) {

}

void ISO7816start (void) {
    modeConfig.periodicService = 1;

    // enable RST change notification
    BP_CS_CN = 1;
}

void ISO7816stop (void) {
    // disable RST change notification
    BP_CS_CN = 0;

    modeConfig.periodicService = 0;
}

unsigned int ISO7816periodic (void) {
    if (sc_reset_flag) {
        sc_reset_flag = 0;
        if (sc_reset) bpWline( "RST asserted" );
        else bpWline( "RST cleared" );
    }

    return 0;
}

void ISO7816pins (void) {
    bpWline( "CLK\t-\tRST\tI/O" );
}

void ISO7816settings (void) {

}

#endif