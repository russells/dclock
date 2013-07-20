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


// FIXME get the byte wide interface back.  See the init function, and one_char().

Q_DEFINE_THIS_FILE;


#define RS_PORT PORTD
#define RS_DDR  DDRD
#define RS_BIT  7
#define RW_PORT PORTE
#define RW_DDR  DDRE
#define RW_BIT  0
/* Changed because there seems to be a problem with PE1 on my Teensy. */
/* #define EN_PORT PORTE */
/* #define EN_DDR  DDRE */
/* #define EN_BIT  1 */
#define EN_PORT PORTD
#define EN_DDR  DDRD
#define EN_BIT  5

#define D4_PORT PORTC
#define D4_DDR  DDRC
#define D4_BIT  4
#define D5_PORT PORTC
#define D5_DDR  DDRC
#define D5_BIT  5
#define D6_PORT PORTC
#define D6_DDR  DDRC
#define D6_BIT  6
#define D7_PORT PORTC
#define D7_DDR  DDRC
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

/** Set or clear the RW bit in the LCD interface. */
#define RW(x)					\
	do { if (x) SB(RW_PORT, RW_BIT); else CB(RW_PORT, RW_BIT); } while (0)

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


static void lcd_init2(void);
static void one_char(uint8_t rs, char c);
static void half_char(uint8_t rs, char c);
static void lcd_on(void);
static void lcd_off(void);


void lcd_init(void)
{
	uint8_t sreg;

	sreg = SREG;
	cli();

	brightness = 2;
	BSP_lcd_init(BRIGHTNESS_2);
	BSP_lcd_pwm_on();

	/* Set all the display pins as outputs. */
	SB(RS_DDR, RS_BIT);
	SB(RW_DDR, RW_BIT);
	SB(EN_DDR, EN_BIT);
	SB(D4_DDR, D4_BIT);
	SB(D5_DDR, D5_BIT);
	SB(D6_DDR, D6_BIT);
	SB(D7_DDR, D7_BIT);

	/* Set all the control outputs before we send data to them. */
	RS(0);
	RW(0);
	EN(0);
	D4(1);
	D5(1);
	D6(1);
	D7(1);

	/* This is the sequence that results in the QP5520 LCD module working
	   in 4 bit mode (or any mode at all).  Looks weird, but I spend ages
	   figuring this out. */
	_delay_ms(50);
	lcd_init2();
	lcd_set_cursor(0, 0);
	_delay_ms(50);
	lcd_init2();

	lcd_line1("Hello!");
	_delay_ms(100);

	SREG = sreg;
}


static void lcd_init2(void)
{
	_delay_ms(5);
	for (uint8_t i=0; i<2; i++) {
		half_char(0, 0b00100000); /* DL=0 */
		half_char(0, 0b11000000); /* N=1 (2 line), F=X */
		_delay_ms(5);
	}
	half_char(0, 0b00000000);
	half_char(0, 0b11000000); /* Display on, cursor off, blink off */
	_delay_ms(5);
	half_char(0, 0b00000000);
	half_char(0, 0b00010000); /* Display clear */
	_delay_ms(5);
	_delay_ms(5);
	half_char(0, 0b00000000);
	half_char(0, 0b01100000); /* I/D=increment, shift=0 */
	_delay_ms(5);
}


/**
 * Turn off interrupts, display a file name and line number.
 */
void lcd_assert_nostop(char const Q_ROM * const Q_ROM_VAR file, int line)
{
	cli();
	lcd_on();
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
}


void lcd_assert(char const Q_ROM * const Q_ROM_VAR file, int line)
{
	lcd_assert_nostop(file, line);

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
	_delay_us(2);
	EN(1);
	_delay_us(2);
	EN(0);
	D7(c & 0b00001000);
	D6(c & 0b00000100);
	D5(c & 0b00000010);
	D4(c & 0b00000001);
	EN(1);
	_delay_us(2);
	EN(0);

	/* Ensure we wait for this char to be absorbed by the LCD. */
	_delay_us(37);

	SREG = sreg;
}


static void half_char(uint8_t rs, char c)
{
	uint8_t sreg;

	sreg = SREG;
	cli();

	RS(rs);
	D7(c & 0b10000000);
	D6(c & 0b01000000);
	D5(c & 0b00100000);
	D4(c & 0b00010000);
	_delay_us(2);
	EN(1);
	_delay_us(2);
	EN(0);

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


static void lcd_on(void)
{
	one_char(0, 0b00001100); /* Display on, Cursor off, Blink off */
}


static void lcd_off(void)
{
	one_char(0, 0b00001000); /* Display off, Cursor off, Blink off */
}


void lcd_set_brightness(uint8_t b)
{
	Q_ASSERT( b < 5 );

	switch (b) {
	case 0:
		BSP_lcd_pwm(BRIGHTNESS_0);
		BSP_lcd_pwm_off();
		lcd_off();
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
	/* If the LCD was off previously (brightness==0) and we're turning it
	   on now, make sure to turn on the LCD itself, rather than just the
	   backlight. */
	if (0 == brightness && 0 != b) {
		lcd_on();
	}
	brightness = b;
}


uint8_t lcd_get_brightness(void)
{
	return brightness;
}
