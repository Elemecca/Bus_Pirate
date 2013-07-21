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

#include "private.h"

#define SC_ATR_TS   0
#define SC_ATR_TD   1
#define SC_ATR_TA   2
#define SC_ATR_TB   3
#define SC_ATR_TC   4
#define SC_ATR_TK   5

int sc_atr_read (unsigned char byte) {
    sc_profile( "* sc_atr_read" );
    
    if (sc_state.atr_len >= 32) {
        sc_notify( SCM_ATR_OVERFLOW );
        return SC_READ_ABORT;
    }

    sc_state.atr[ sc_state.atr_len++ ] = byte;

    switch (sc_state.rx.mode) {
    case SC_ATR_TS:
        switch (byte) {
        case 0x3B:
            // direct coding, move on to rest of ATR
            sc_state.rx.mode = SC_ATR_TD;
            return SC_READ_OK;
        case 0xC0:
            // inverse coding, not supported
            sc_notify( SCM_INVERSE_CODING );
        default:
            // invalid/unsupported value, abort
            sc_notify( SCM_ATR_INVALID );
            return SC_READ_ABORT;
        }

    case SC_ATR_TD:
        sc_state.rx.offset = sc_state.atr_len - 1;
        if (sc_state.atr[ sc_state.rx.offset ] & 0x10) {
            sc_state.rx.mode = SC_ATR_TA;
            return SC_READ_OK;
        }
    case SC_ATR_TA:
        if (sc_state.atr[ sc_state.rx.offset ] & 0x20) {
            sc_state.rx.mode = SC_ATR_TB;
            return SC_READ_OK;
        }
    case SC_ATR_TB:
        if (sc_state.atr[ sc_state.rx.offset ] & 0x40) {
            sc_state.rx.mode = SC_ATR_TC;
            return SC_READ_OK;
        }
    case SC_ATR_TC:
        if (sc_state.atr[ sc_state.rx.offset ] & 0x80) {
            sc_state.rx.mode = SC_ATR_TD;
            return SC_READ_OK;
        } else {
            sc_state.rx.offset =
                    // this is 1 if the last T is not 0, meaning TCK is present
                    (0 != (sc_state.atr[ sc_state.rx.offset ] & 0x0F) ? 1 : 0)
                    // this is K, the number of TK bytes
                    + (sc_state.atr[ 1 ] & 0x0F);
            if (sc_state.rx.offset > 0) {
                sc_state.rx.mode = SC_ATR_TK;
                return SC_READ_OK;
            } else {
                sc_notify( SCM_ATR_DONE );
                return SC_READ_DONE;
            }
        }

    case SC_ATR_TK:
        if (--sc_state.rx.offset > 0) {
            return SC_READ_OK;
        } else {
            sc_notify( SCM_ATR_DONE );
            return SC_READ_DONE;
        }

    default:
        // shouldn't happen, but...
        sc_notify( SCM_CONFUSED );
        return SC_READ_ABORT;
    }
}

void sc_atr_print (void) {
    bpWstring( "raw ATR:" );
    bpWhexdump( sc_state.atr, sc_state.atr_len );
}