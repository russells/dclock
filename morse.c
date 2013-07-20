#include "morse.h"
#include "qpn_port.h"
#include "bsp.h"

#include <stdint.h>


/** Milliseconds per Morse code element. */
#define DIT_LENGTH 150


/**
 * @brief Holds a set of characters and their Morse Code representations.
 *
 * @note If flash memory space becomes scarce, this could possibly be trimmed
 * by putting the number of symbols (.nsymbols) in the lower three bits of
 * .symbols, leaving the higher five bits for the symbols themselves.  The
 * problem would then be how to represent characters with six symbols.  A way
 * around that would be to assume that if nsymbols is 6 (0b110) then there is
 * an extra symbol at the end of the symbols which is a dit, and if nsymbols is
 * 7 (0b111) there is an extra symbol which is a dah.  But... there is one
 * seven symbol character ($), which would have to be handled as a special
 * case.
 */
struct MorseChar {
	/** The character. */
	char ch;
	/** Number of symbols (dots or dashes) in this character. */
	uint8_t nsymbols;
	/** The symbols of this character, one per bit, starting at the MSB.
	    1==dah, 0==dit. */
	uint8_t symbols;
};


/**
 * @brief http://en.wikipedia.org/wiki/Morse_code#Letters.2C_numbers.2C_punctuation
 */
static const Q_ROM struct MorseChar morseChars[] = {
	{ 'A' , 2, 0b01000000 },
	{ 'B' , 4, 0b10000000 },
	{ 'C' , 4, 0b10100000 },
	{ 'D' , 3, 0b10000000 },
	{ 'E' , 1, 0b00000000 },
	{ 'F' , 4, 0b00100000 },
	{ 'G' , 3, 0b11000000 },
	{ 'H' , 4, 0b00000000 },
	{ 'I' , 2, 0b00000000 },
	{ 'J' , 4, 0b01110000 },
	{ 'K' , 3, 0b10100000 },
	{ 'L' , 4, 0b01000000 },
	{ 'M' , 2, 0b11000000 },
	{ 'N' , 2, 0b10000000 },
	{ 'O' , 3, 0b11100000 },
	{ 'P' , 4, 0b01100000 },
	{ 'Q' , 4, 0b11010000 },
	{ 'R' , 3, 0b01000000 },
	{ 'S' , 3, 0b00000000 },
	{ 'T' , 1, 0b10000000 },
	{ 'U' , 3, 0b00100000 },
	{ 'V' , 4, 0b00010000 },
	{ 'W' , 3, 0b01100000 },
	{ 'X' , 4, 0b10010000 },
	{ 'Y' , 4, 0b10110000 },
	{ 'Z' , 4, 0b11000000 },
	{ '1' , 5, 0b01111000 },
	{ '2' , 5, 0b00111000 },
	{ '3' , 5, 0b00011000 },
	{ '4' , 5, 0b00001000 },
	{ '5' , 5, 0b00000000 },
	{ '6' , 5, 0b10000000 },
	{ '7' , 5, 0b11000000 },
	{ '8' , 5, 0b11100000 },
	{ '9' , 5, 0b11110000 },
	{ '0' , 5, 0b11111000 },
	{ '.' , 6, 0b01010100 },
	{ ',' , 6, 0b11001100 },
	{ '?' , 6, 0b00110000 },
	{ '\'', 6, 0b01111000 },
	{ '!' , 6, 0b10101100 },
	{ '/' , 5, 0b10010000 },
	{ '(' , 5, 0b10110000 },
	{ ')' , 6, 0b10110100 },
	{ '&' , 5, 0b01000000 },
	{ ':' , 6, 0b01110000 },
	{ ';' , 6, 0b10101000 },
	{ '=' , 5, 0b10001000 },
	{ '+' , 5, 0b01010000 },
	{ '-' , 6, 0b10000100 },
	{ '_' , 6, 0b11001000 },
	{ '"' , 6, 0b01001000 },
	{ '$' , 7, 0b00010010 },
	{ '@' , 6, 0b01101000 },
	{ '\0', 0, 0 }
};


static const Q_ROM struct MorseChar *find_morse_char(const char c)
{
	const struct MorseChar *mc;
	char cup;

	if (c >= 'a' && c <= 'z') {
		cup = c - 0x20;
	} else {
		cup = c;
	}

	mc = morseChars;
	while (1) {
		char mcc = Q_ROM_BYTE(mc->ch);
		if (! mcc)
			break;
		if (mcc == cup)
			return mc;
		mc++;
	}
	return 0;
}


/**
 * @brief Output one dit.
 */
static void dit(void)
{
	BSP_morse_signal(1);
	BSP_delay_ms( DIT_LENGTH );
	BSP_morse_signal(0);
}


/**
 * @brief Output one dah.
 */
static void dah(void)
{
	BSP_morse_signal(1);
	BSP_delay_ms( DIT_LENGTH * 3 );
	BSP_morse_signal(0);
}


static void send_morse_char(const Q_ROM struct MorseChar *mc)
{
	uint8_t nsymbols = Q_ROM_BYTE(mc->nsymbols);
	uint8_t symbols = Q_ROM_BYTE(mc->symbols);

	while (nsymbols) {
		if (symbols & 0b10000000)
			dah();
		else
			dit();
		BSP_delay_ms( DIT_LENGTH );
		symbols <<= 1;
		nsymbols--;
	}
}


static void char_pause(void)
{
	BSP_delay_ms( DIT_LENGTH * 6 );
}


static void word_pause(void)
{
	BSP_delay_ms( DIT_LENGTH * 9 );
}


static void morse_char(char c)
{
	const Q_ROM struct MorseChar *mc;

	mc = find_morse_char(c);
	if (mc) {
		send_morse_char(mc);
	} else {
		/* Send a question mark as the unknown character. */
		mc = find_morse_char('?');
		if (mc)
			send_morse_char(mc);
	}
	char_pause();
}


/**
 * @brief Stop everything and blink in Morse code.
 */
void morse_assert(char const Q_ROM * const Q_ROM_VAR str, int num)
{
	int i;
	char c;
	char buf[10];
	char *number_cp;
	char neg_flag;

	BSP_stop_everything();
	BSP_enable_morse_line();

	/* Format the number */
	number_cp = buf + 9;
	*number_cp = '\0';
	if (num == 0) {
		*--number_cp = '0';
	} else {
		if (num < 0) {
			neg_flag = '-';
			num *= -1;
		} else {
			neg_flag = 0;
		}
		while (num) {
			int n = num % 10;
			*--number_cp = (char)(n+'0');
			num /= 10;
		}
		if (neg_flag) {
			*--number_cp = neg_flag;
		}
	}

	/* Display the assertion message a limited number of times to avoid
	   completely draining the battery. */
	for (uint8_t loops=0; loops < 10; loops++) {
		word_pause();
		for (i=0; /*EMPTY*/ ; i++) {
			c = Q_ROM_BYTE(str[i]);
			if (!c)
				break;
			morse_char(c);
		}
		word_pause();
		for (i=0; number_cp[i]; i++) {
			morse_char(number_cp[i]);
		}
		word_pause();
		word_pause();
	}
	BSP_reset();
}
