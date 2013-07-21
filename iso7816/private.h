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
 */

#ifndef ISO7816_PRIVATE_H
#define	ISO7816_PRIVATE_H

#include "../base.h"

// session state values
// enums aren't allowed in bitfields, so use defines
#define SCS_MANUAL  0   // automatic operation is disabled
#define SCS_OFFLINE 1   // no session is active, the hardware is disconnected
#define SCS_RESET   2   // host has initiated reset
#define SCS_ATR     3   // device is sending ATR
#define SCS_IDLE    4   // session active, waiting for command
#define SCS_COMMAND 5   // command in progress

// notification messages
#define SCM_CONFUSED        0   // state doesn't pass sanity checks
#define SCM_CLK_START       1   // clock signal detected
#define SCM_CLK_RATE        2   // new clock rate calculated
#define SCM_RESET_ACK       3   // device acknowledged reset by setting IO high
#define SCM_RESET_END       4   // host released HRST
#define SCM_ATR_START       5   // device started ATR
#define SCM_INVERSE_CODING  6   // device uses inverse coding
#define SCM_ATR_INVALID     7   // invalid value in ATR
#define SCM_ATR_OVERFLOW    8   // ATR more than 32 bytes
#define SCM_ATR_DONE        9   // complete ATR received


#define SC_READ_OK      -1
#define SC_READ_DONE    -2
#define SC_READ_ABORT   -3

#define SC_RX_BUFFER_SIZE 128
#define SC_NOTIFY_BUFFER_SIZE 32

struct sc_state_t {
    unsigned session :4; // session state, use SCS_* defines
    unsigned note_overflow  :1;

    unsigned int mult_t2;   // rollover multiplier for Timer 2
    unsigned int mult_t3;   // rollover multiplier for Timer 3

    unsigned long rate_ticks;
    unsigned long rate_cycles;

    unsigned int  reset_ack;    // tick count when device set IO high, <=200
    unsigned long reset_end;    // tick count when host released RST
    unsigned long atr_start;    // tick count when device started ATR

    unsigned char atr[ 32 ];
    unsigned char atr_len;

    struct {
        // ring buffer used by some modes
        unsigned char buffer[ SC_RX_BUFFER_SIZE ];
        unsigned char read;
        unsigned char write;

        // private state for modes' use
        unsigned char mode;
        unsigned char offset;

        // function to be called when a byte is read
        int (*callback) (unsigned char byte);

        unsigned char next_state;
    } rx;

    struct {
        unsigned char buffer[ SC_NOTIFY_BUFFER_SIZE ];
        unsigned char read;
        unsigned char write;
    } notes;
};

extern struct sc_state_t sc_state;

void sc_notify (unsigned short message);

#ifdef SC_PROF_ENABLE
    #define SC_PROF_LENGTH 128

    struct sc_prof_t {
        unsigned long time;
        const char *event;
    };

    extern struct sc_prof_t sc_prof[ SC_PROF_LENGTH ];
    extern unsigned sc_prof_idx;

    inline void sc_profile (const char *event);
#else
    #define sc_profile(message)
#endif

#endif	/* ISO7816_PRIVATE_H */
