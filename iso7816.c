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
    // set up timer 2 for clock output
    T2CON           = 0;    // disable the timer
    TMR2            = 0;    // clear the timer register
    PR2             = 1;    // period of 1 at 16 MHz = 4 MHz
    IFS0bits.T2IF   = 0;    // disable the timer interrupt
    IEC0bits.T2IE   = 0;    // "
    IPC1bits.T2IP   = 0;    // "

    // set up output comparator 5 for clock output
    BP_CLK_RPOUT    = OC5_IO;   // output on CLK pin
    OC5CON          = 0x0001;   // initial low, use timer 2, prescaler 1:1
    OC5CONbits.OCM1 = 1;        // enable toggle mode
    OC5R            = PR2;      // OCxR = PRx toggles once per period
    IFS2bits.OC5IF  = 0;        // disable the comparator interrupt
    IEC2bits.OC5IE  = 0;        // "
    IPC10bits.OC5IP = 0;        // "
}

void ISO7816cleanup (void) {
    T2CON           = 0;
    OC5CON          = 0;
    BP_CLK_RPOUT    = 0;
}

void ISO7816macro (unsigned int c) {

}

void ISO7816start (void) {
    T2CONbits.TON = 1;
}

void ISO7816stop (void) {
    T2CONbits.TON = 0;
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