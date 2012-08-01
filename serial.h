#ifndef serial_h_INCLUDED
#define serial_h_INCLUDED

#include "qpn_port.h"


void serial_init(void);
int  serial_send(const char *s);
int  serial_send_rom(char const Q_ROM * const Q_ROM_VAR s);
int  serial_send_int(unsigned int n);
int  serial_send_hex_int(unsigned int x);
int  serial_send_char(char c);
void serial_assert(char const Q_ROM * const Q_ROM_VAR file, int line);
void serial_assert_nostop(char const Q_ROM * const Q_ROM_VAR file, int line);
void serial_drain(void);

/**
 * Send a constant string (stored in ROM).
 *
 * This macro takes care of the housekeeping required to send a ROM string.  It
 * creates a scope, stores the string in ROM, accessible only inside that
 * scope, and calls serialstr() to output the string.
 */
#define SERIALSTR(s)						\
	do {							\
		static const char PROGMEM ss[] = s;		\
		serial_send_rom(ss);				\
	} while (0)

#endif
