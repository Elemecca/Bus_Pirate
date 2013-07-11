/*
 * This file is part of the Bus Pirate project (http://code.google.com/p/the-bus-pirate/).
 *
 * Written and maintained by the Bus Pirate project.
 *
 * To the extent possible under law, the project has
 * waived all copyright and related or neighboring rights to Bus Pirate. This
 * work is published from United States.
 *
 * For details see: http://creativecommons.org/publicdomain/zero/1.0/.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */
#include "base.h"

#ifdef BP_USE_ISO7816

void ISO7816Process (void) {

}

unsigned ISO7816write (unsigned int c) {
    return 0;
}

unsigned int ISO7816read (void) {
    return 0;
}

void ISO7816setup (void) {
    // set up SPI module 1 for clock output
    SPI1STAT            = 0;            // reset SPI module
    SPI1CON1            = 0;            // "
    SPI1CON2            = 0;            // "
    SPI1CON1bits.MSTEN  = 1;            // enable master mode
    SPI1CON2bits.FRMEN  = 1;            // enable framed mode
    SPI1CON1bits.PPRE   = 3;            // primary   prescale 1:1
    SPI1CON1bits.SPRE   = 4;            // secondary prescale 1:4 = 4 MHz
    IFS0bits.SPI1IF     = 0;            // disable SPI interrupt
    IEC0bits.SPI1IE     = 0;            // "
    IPC2bits.SPI1IP     = 0;            // "
    BP_CLK_RPOUT        = SCK1OUT_IO;   // connect SP1 SCK output to CLK
    BP_CLK_DIR          = 0;            // configure CLK as output

    /* When the SPI module is set to framed master mode its clock runs
     * continuously. The Peripheral Pin Select feature allows us to connect
     * just its clock output and ignore its other pins. That gives us a
     * clock output prescaled at 1:2,1:3..1:8 from the instruction clock
     * without bothering the CPU. See PIC24F FRM 23.3.4.1 for details.
     *
     * secondary prescaler values:
     *   0  1:8  2.00 MHz
     *   1  1:7  2.28 MHz
     *   2  1:6  2.66 MHz
     *   3  1:5  3.20 MHz
     *   4  1:4  4.00 MHz
     *   5  1:3  5.33 MHz
     *   6  1:2  8.00 MHz
     *   7  1:1  invalid, malformed wave
     * (assumes 16 MHz instruction clock and 1:1 primary prescaler)
     */
}

void ISO7816cleanup (void) {
    // SPI module 1 was clock output
    SPI1STAT        = 0; // reset SPI module
    SPI1CON1        = 0; // "
    SPI1CON2        = 0; // "
    BP_CLK_DIR      = 1; // configure CLK as input
    BP_CLK_RPOUT    = 0; // disconnect CLK
}

void ISO7816macro (unsigned int c) {

}

void ISO7816start (void) {
    SPI1STATbits.SPIEN = 1;
}

void ISO7816stop (void) {
    SPI1STATbits.SPIEN = 0;
}

unsigned int ISO7816periodic (void) {
    return 0;
}

void ISO7816pins (void) {
    bpWline( "CLK\t-\t-\t-" );
}

void ISO7816settings (void) {

}

#endif