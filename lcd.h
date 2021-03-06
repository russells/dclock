#ifndef lcd_h_INCLUDED
#define lcd_h_INCLUDED

#include "qpn_port.h"


void lcd_init(void);
void lcd_clear(void);
void lcd_setpos(uint8_t line, uint8_t pos);
void lcd_set_cursor(uint8_t line, uint8_t pos);
void lcd_cursor_off(void);
void lcd_line1(const char *line);
void lcd_line1_rom(char const Q_ROM * const Q_ROM_VAR s);
void lcd_line2(const char *line);
void lcd_line2_rom(char const Q_ROM * const Q_ROM_VAR s);
void lcd_inc_brightness(void);
void lcd_dec_brightness(void);
void lcd_set_brightness(uint8_t b);
uint8_t lcd_get_brightness(void);
void lcd_assert(char const Q_ROM * const Q_ROM_VAR file, int line);
void lcd_assert_nostop(char const Q_ROM * const Q_ROM_VAR file, int line);


#define LCD_LINE1_ROM(s)					\
	do {							\
		static const char PROGMEM ss[] = s;		\
		lcd_line1_rom(ss);				\
	} while (0)

#define LCD_LINE2_ROM(s)					\
	do {							\
		static const char PROGMEM ss[] = s;		\
		lcd_line2_rom(ss);				\
	} while (0)



#endif
