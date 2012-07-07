/**
 * @file
 *
 * Manage the HD44780 LCD on the Freetronics LCD Keypad Shield.
 */

#include "lcd.h"
#include "bsp.h"
#include "cpu-speed.h"
#include "qpn_port.h"
#include <util/delay.h>
#include <avr/wdt.h>


Q_DEFINE_THIS_FILE;


#define RS_PORT PORTB
#define RS_DDR  DDRB
#define RS_BIT  0
#define EN_PORT PORTB
#define EN_DDR  DDRB
#define EN_BIT  1
#define D4_PORT PORTD
#define D4_DDR  DDRD
#define D4_BIT  4
#define D5_PORT PORTD
#define D5_DDR  DDRD
#define D5_BIT  5
#define D6_PORT PORTD
#define D6_DDR  DDRD
#define D6_BIT  6
#define D7_PORT PORTD
#define D7_DDR  DDRD
#define D7_BIT  7


#define BRIGHTNESS_0 0
/** A PWM value of 0 in the timer results in a one cycle pulse from the output
    pin, which is the minimum brightness we can get, apart from "off". */
#define BRIGHTNESS_1 0
/** The default brightness at startup. */
#define BRIGHTNESS_2 18
#define BRIGHTNESS_3 70
/** Full brightness. */
#define BRIGHTNESS_4 255


/** The current brightness level.  This is distinct from the PWM value for the
    timer because level 0 is handled differently.  We essentially need two zero
    levels for PWM, since a PWM level of zero results in a one cycle pulse.  To
    really turn off the back light we need to disconnect the timer PWM. */
static uint8_t brightness;


/** Set an IO bit. */
#define SB(port,bit)				\
	do {					\
		port |= (1 << bit);		\
	} while (0)

/** Clear an IO bit. */
#define CB(port,bit)				\
	do {					\
		port &= (~ (1 << bit));		\
	} while (0)

/*
 * These macros look complicated, but...
 *
 * - They are designed so that they can be used anywhere that a function call
 *   can be used.  That is the point of the do{}while(0) thing.
 *
 * - At the point in the source where these are invoked, they are very simple -
 *   eg RS(0).
 *
 * - With a constant argument, all of the conditional stuff is done at compile
 *   time, and avr-gcc collapses each of them into a single machine
 *   instruction.  With a non-constant argument, it emits a simple test and
 *   jump sequence of five instructions.
 */

/** Set or clear the RS bit in the LCD interface. */
#define RS(x)					\
	do { if (x) SB(RS_PORT, RS_BIT); else CB(RS_PORT, RS_BIT); } while (0)

/** Set or clear the EN bit in the LCD interface. */
#define EN(x)					\
	do { if (x) SB(EN_PORT, EN_BIT); else CB(EN_PORT, EN_BIT); } while (0)

/** Set or clear the D4 bit in the LCD interface. */
#define D4(x)					\
	do { if (x) SB(D4_PORT, D4_BIT); else CB(D4_PORT, D4_BIT); } while (0)

/** Set or clear the D5 bit in the LCD interface. */
#define D5(x)					\
	do { if (x) SB(D5_PORT, D5_BIT); else CB(D5_PORT, D5_BIT); } while (0)

/** Set or clear the D6 bit in the LCD interface. */
#define D6(x)					\
	do { if (x) SB(D6_PORT, D6_BIT); else CB(D6_PORT, D6_BIT); } while (0)

/** Set or clear the D7 bit in the LCD interface. */
#define D7(x)					\
	do { if (x) SB(D7_PORT, D7_BIT); else CB(D7_PORT, D7_BIT); } while (0)


static void one_char(uint8_t rs, char c);


void lcd_init(void)
{
	brightness = 2;
	BSP_lcd_init(BRIGHTNESS_2);
	BSP_lcd_pwm_on();

	/* The HD44780 takes 10ms to start. */
	_delay_ms(10);

	/* All the display pins are outputs. */
	SB(RS_DDR, RS_BIT);
	SB(EN_DDR, EN_BIT);
	SB(D4_DDR, D4_BIT);
	SB(D5_DDR, D5_BIT);
	SB(D6_DDR, D6_BIT);
	SB(D7_DDR, D7_BIT);
	/* Set all the outputs high.  Idle is high for the EN pin. */
	RS(1);
	EN(1);
	D4(1);
	D5(1);
	D6(1);
	D7(1);

	one_char(0, 0x01);	/* Clear display */
	_delay_ms(1.6);
	one_char(0, 0x02);	/* Return home */
	_delay_ms(1.6);
	one_char(0, 0b00000110);	/* I/D=increment, Shift=0 */
	one_char(0, 0b00001100);	/* Display on, Cursor off, Blink off */
	one_char(0, 0b00010000);	/* S/C=0, R/L=0 */
	one_char(0, 0b00101000);	/* DL=0 (4 bits), N=1 (2 lines), F=0
					   (5x8) */
}


/**
 * Turn off interrupts, display a file name and line number, stop.
 */
void lcd_assert(char const Q_ROM * const Q_ROM_VAR file, int line)
{
	cli();
	wdt_disable();
	lcd_clear();
	_delay_ms(10);
	LCD_LINE1_ROM("ASSERT: ");
	lcd_setpos(0, 8);
	if (line) {
		char number[8];
		char *cp = number+7;
		*cp = '\0';
		while (line) {
			int n = line % 10;
			*--cp = (char)(n+'0');
			line /= 10;
		}
		while (*cp) {
			one_char(1, *cp++);
			_delay_ms(10);
		}
	} else {
		one_char(1, '0');
	}
	lcd_setpos(1, 0);
	for (uint8_t i=0; i<16; i++) {
		char c = Q_ROM_BYTE(file[i]);
		if (! c)
			break;
		one_char(1, c);
		_delay_ms(10);
	}

	/* Wait forever. */
	while (1) {
		_delay_ms(10);
	}
}


void lcd_clear(void)
{
	one_char(0, 0x01);
	_delay_ms(2);
}


void lcd_setpos(uint8_t line, uint8_t pos)
{
	Q_ASSERT( line < 2 );
	Q_ASSERT( pos < 16 );

	if (0 == line) {
		one_char(0, 0x80 | pos);
	} else {
		one_char(0, 0x80 | (pos+0x40));
	}
}


void lcd_set_cursor(uint8_t line, uint8_t pos)
{
	lcd_setpos(line, pos);
	one_char(0, 0b00001111);	/* Display on, Cursor on, Blink on */
}


void lcd_cursor_off(void)
{
	one_char(0, 0b00001100);	/* Display on, Cursor on, Blink off */
}


void lcd_line1(const char *line)
{
	lcd_setpos(0, 0);
	for (uint8_t i=0; i<16 && line[i]; i++) {
		one_char(1, line[i]);
	}
}


void lcd_line2(const char *line)
{
	lcd_setpos(1, 0);
	for (uint8_t i=0; i<16 && line[i]; i++) {
		one_char(1, line[i]);
	}
}


void lcd_line1_rom(char const Q_ROM * const Q_ROM_VAR s)
{
	static char line[16];
	uint8_t i;
	char c;

	for (i=0; i<16; i++) {
		line[i] = ' ';
	}

	for (i=0; i<16; i++) {
		c = Q_ROM_BYTE(s[i]);
		if (!c) {
			break;
		}
		line[i] = c;
	}
	lcd_line1(line);
}


void lcd_line2_rom(char const Q_ROM * const Q_ROM_VAR s)
{
	static char line[16];
	uint8_t i;
	char c;

	for (i=0; i<16; i++) {
		line[i] = ' ';
	}

	for (i=0; i<16; i++) {
		c = Q_ROM_BYTE(s[i]);
		if (!c) {
			break;
		}
		line[i] = c;
	}
	lcd_line2(line);
}


/**
 * Send one char to the HD44780.
 *
 * This is the only part of displaying characters that is done with interrupts
 * off.  But we manage the interrupt state so you can call this with interrupts
 * on or off, and we will return in the same state.
 */
static void one_char(uint8_t rs, char c)
{
	uint8_t sreg;

	sreg = SREG;
	cli();

	RS(rs);
	D7(c & 0b10000000);
	D6(c & 0b01000000);
	D5(c & 0b00100000);
	D4(c & 0b00010000);
	EN(0);
	_delay_us(5);
	EN(1);
	D7(c & 0b00001000);
	D6(c & 0b00000100);
	D5(c & 0b00000010);
	D4(c & 0b00000001);
	EN(0);
	_delay_us(5);
	EN(1);

	/* Ensure we wait for this char to be absorbed by the LCD. */
	_delay_us(37);

	SREG = sreg;
}


void lcd_inc_brightness(void)
{
	switch (brightness) {
	case 0:
		lcd_set_brightness(1);
		break;
	case 1:
		lcd_set_brightness(2);
		break;
	case 2:
		lcd_set_brightness(3);
		break;
	case 3:
		lcd_set_brightness(4);
		break;
	case 4:
		break;
	default:
		Q_ASSERT( 0 );
		break;
	}
}


void lcd_dec_brightness(void)
{
	switch (brightness) {
	case 0:
		break;
	case 1:
		lcd_set_brightness(0);
		break;
	case 2:
		lcd_set_brightness(1);
		break;
	case 3:
		lcd_set_brightness(2);
		break;
	case 4:
		lcd_set_brightness(3);
		break;
	default:
		Q_ASSERT( 0 );
		break;
	}
}


void lcd_set_brightness(uint8_t b)
{
	Q_ASSERT( b < 5 );

	switch (b) {
	case 0:
		BSP_lcd_pwm(BRIGHTNESS_0);
		BSP_lcd_pwm_off();
		break;
	case 1:
		BSP_lcd_pwm(BRIGHTNESS_1);
		BSP_lcd_pwm_on();
		break;
	case 2:
		BSP_lcd_pwm(BRIGHTNESS_2);
		BSP_lcd_pwm_on();
		break;
	case 3:
		BSP_lcd_pwm(BRIGHTNESS_3);
		BSP_lcd_pwm_on();
		break;
	case 4:
		BSP_lcd_pwm(BRIGHTNESS_4);
		BSP_lcd_pwm_on();
		break;
	}
	brightness = b;
}


uint8_t lcd_get_brightness(void)
{
	return brightness;
}
