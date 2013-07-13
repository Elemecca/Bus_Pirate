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

unsigned ISO7816write(unsigned int c);
unsigned int ISO7816read(void);
void ISO7816setup(void);
void ISO7816cleanup(void);
void ISO7816macro(unsigned int c);
void ISO7816start(void);
void ISO7816stop(void);
unsigned int ISO7816periodic(void);
void ISO7816pins(void);
void ISO7816settings(void);
