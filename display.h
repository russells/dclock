#ifndef display_h_INCLUDED
#define display_h_INCLUDED

#include "qpn_port.h"


void display_init(void);
void display_clear(void);
void display_setpos(uint8_t line, uint8_t pos);
void display_line1(const char *line);
void display_line1_rom(char const Q_ROM * const Q_ROM_VAR s);
void display_line2(const char *line);
void display_line2_rom(char const Q_ROM * const Q_ROM_VAR s);
void display_assert(char const Q_ROM * const Q_ROM_VAR file, int line);


#define DISPLAY_LINE1_ROM(s)					\
	do {							\
		static const char PROGMEM ss[] = s;		\
		display_line1_rom(ss);				\
	} while (0)

#define DISPLAY_LINE2_ROM(s)					\
	do {							\
		static const char PROGMEM ss[] = s;		\
		display_line2_rom(ss);				\
	} while (0)



#endif
