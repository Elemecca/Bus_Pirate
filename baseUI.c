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

extern void bpmessages(void);

void bpMsg(int offset, int length) {
	int i;
	for( i = offset; i < (offset+length); i++) {
            /* These two variables ought to be rendered unnecessary by a decent
             * optimizing compiler. Unfortunately a bug in XC16 v1.11 causes
             * invalid output when optimizing away repeated division and mod
             * operations with the same operands. The resulting symptom is that
             * when (i % 3 == 0) the message table offset is calculated as 0
             * and so the output is the first character of the first message in
             * the table, currently 'W'. For details on the issue see:
             * http://www.microchip.com/forums/fb.ashx?m=734999
             */
            register int mod = i % 3;
            register int addr = (int)( &bpmessages ) + ((i / 3) << 1);

            if (0 == mod) UART1TX( __builtin_tblrdl( addr ) & 0x00FF );
            if (1 == mod) UART1TX((__builtin_tblrdl( addr ) >> 8) & 0xFF );
            if (2 == mod) UART1TX( __builtin_tblrdh( addr ) & 0x00FF );
	}

}
void bpMsgBR(int offset, int length)
{	bpMsg(offset, length);
	bpBR;
}

// new printHelp function
// are separate functioncalls memorywise beter then 1 huge string? time would tell ;)

void printHelp(void)
{	HLP1000;
	HLP1001;
	HLP1002;
	HLP1003;
	HLP1004;
	HLP1005;
	HLP1006;
	HLP1007;
	HLP1008;
	HLP1009;
	HLP1010;
	HLP1011;
	HLP1012;
	HLP1013;
	HLP1014;
	HLP1015;
	HLP1016;
	HLP1017;
	HLP1018;
	HLP1019;
	HLP1020;
	HLP1021;
	HLP1022;
}

int agree(void)
{	char c;

	//bpWstring("Are you sure? ");
	BPMSG1135;

	while(!UART1RXRdy());
	c=UART1RX();
	UART1TX(c);
	bpBR;

	if((c=='y')||(c=='Y'))
	{	return 1;
	}

	return 0;
}
