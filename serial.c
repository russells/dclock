#include "dclock.h"
#include "serial.h"
#include "toggle-pin.h"
#include <util/delay.h>
#include <avr/wdt.h>
#include <stdint.h>


Q_DEFINE_THIS_FILE;


/**
 * @brief The number of bytes that can be queued for sending.
 *
 * This buffer needs to be a reasonable size, since during development we send
 * output once per second.  If the buffer is too small, we will lose data.
 * (Lost data is indicated by the '!' character - see serial_send_char().)
 *
 * Ideally, make this at least the maximum number of bytes we will ever send
 * inside one second.
 *
 * @note The number of bytes that can actually be queued is one less than this
 * value, due to the way that the ring buffer works.
 */
#define SEND_BUFFER_SIZE 120

static char sendbuffer[SEND_BUFFER_SIZE];
static volatile uint8_t sendhead = 0;
static volatile uint8_t sendtail = 0;


void
serial_init(void)
{
	cli();

	/* Set the baud rate.  Arduino clock = 16MHz, baud = 9600.
	   doc8161.pdf, p179 and p203. */
	UBRR0H = 0;
	UBRR0L = 103;

	UCSR0B =(1<<RXCIE0) |
		(0<<TXCIE0) |
		(0<<UDRIE0) |
		(0<<RXEN0 ) |
		(1<<TXEN0 ) |
		(0<<UCSZ02) |
		(0<<RXB80 ) |
		(0<<TXB80 );
	/* N81 */
	UCSR0C =(0<<UMSEL01) |
		(0<<UMSEL00) |
		(0<<UPM01  ) |
		(0<<UPM00  ) |
		(0<<USBS0  ) |
		(1<<UCSZ01 ) |
		(1<<UCSZ00 ) |
		(0<<UCPOL0 );

	/* Does the RX pin have to be made input? */
	DDRD &= ~ ( 1 << 0 );

	sendhead = 0;
	sendtail = 0;

	sei();
}


/**
 * @brief Send a string from data memory out the serial port.
 *
 * If the serial send buffer is close to being overrun, we send a '!' character
 * and stop.  The '!' is not included in the character count.
 *
 * Note that the '!' character is reserved for "buffer nearly overrun" and
 * should not be sent otherwise.
 *
 * @return the number of characters that are sent.
 */
int serial_send(const char *s)
{
	int sent = 0;

	while (*s) {
		if (serial_send_char(*s++)) {
			sent++;
		} else {
			break;
		}
	}
	return sent;
}


/**
 * @brief Send a string from program memory out the serial port.
 *
 * @see serial_send()
 *
 * @return the number of characters that are sent.
 */
int serial_send_rom(char const Q_ROM * const Q_ROM_VAR s)
{
	char c;
	int i = 0;
	int sent = 0;

	while (1) {
		c = Q_ROM_BYTE(s[i++]);
		if (!c) {
			break;
		}
		if (serial_send_char(c)) {
			sent++;
		} else {
			break;
		}
	}
	return sent;
}


static uint8_t
sendbuffer_space(void)
{
	if (sendhead == sendtail) {
		return SEND_BUFFER_SIZE - 1;
	} else if (sendhead > sendtail) {
		return SEND_BUFFER_SIZE - 1 - (sendhead - sendtail);
	} else {
		/* sendhead < sendtail */
		return sendtail - sendhead - 1;
	}
}


/**
 * @brief Put one character into the serial send buffer.
 *
 * We assume that the buffer has already been checked for space.
 *
 * Interrupts should be off when calling this function.
 */
static void
put_into_buffer(char c)
{
	sendbuffer[sendhead] = c;
	sendhead++;
	if (sendhead >= SEND_BUFFER_SIZE)
		sendhead = 0;
	UCSR0B |= (1 << UDRIE0);
}


/**
 * @brief Send a single character out the serial port.
 *
 * If there is no space in the send buffer, do nothing.  If there is only one
 * character's space, send a '!' character.  Otherwise send the given
 * character.  We never busy wait for buffer space, since that can lead to
 * QP-nano event queues filling up as we wouldn't be handling events while busy
 * waiting.  (This has happened more than once during development.)
 *
 * The '!' character is reserved for indicating that the buffer was nearly
 * overrun, and shouldn't be sent otherwise.  If you think you need the
 * exclamation mark for emphasis, you're wrong.
 *
 * @return 1 if the given character was put into the buffer, 0 otherwise.  Note
 * that we also return 0 if '!' was put into the buffer, to indicate that we've
 * nearly overrun the buffer.
 */
int serial_send_char(char c)
{
	int available;
	int sent;

	cli();
	available = sendbuffer_space();
	if (available >= 1) {
		if (available == 1) {
			put_into_buffer('!');
			sent = 0;
		} else {
			put_into_buffer(c);
			sent = 1;
		}
	} else {
		sent = 0;
	}
	sei();
	return sent;
}


SIGNAL(USART_UDRE_vect)
{
	char c;

	TOGGLE_ON();

	if (sendhead == sendtail) {
		UCSR0B &= ~ (1 << UDRIE0);
	} else {
		c = sendbuffer[sendtail];
		sendtail++;
		if (sendtail >= SEND_BUFFER_SIZE)
			sendtail = 0;
		UDR0 = c;
	}
}


static void
serial_send_noint(uint8_t byte)
{
	while ( !( UCSR0A & (1<<UDRE0)) );
	UDR0 = byte;
	while ( !( UCSR0A & (1<<UDRE0)) );
}


/**
 * Do all of the serial logging on assertion, but don't stop the CPU.
 *
 * This allows us to send the assertion message out the serial port, and then
 * also show the message on the LCD.
 */
void serial_assert_nostop(char const Q_ROM * const Q_ROM_VAR file, int line)
{
	int i;
	char number[10];

	/* Turn off everything. */
	cli();
	wdt_reset();
	wdt_disable();

	/* Rashly assume that the UART is configured. */
	serial_send_noint('\r');
	serial_send_noint('\n');
	serial_send_noint('A');
	serial_send_noint('S');
	serial_send_noint('S');
	serial_send_noint('E');
	serial_send_noint('R');
	serial_send_noint('T');
	serial_send_noint(' ');

	i = 0;
	while (1) {
		char c = Q_ROM_BYTE(file[i++]);
		if (!c)
			break;
		serial_send_noint(c);
	}
	serial_send_noint(' ');

	if (line) {
		char *cp = number+9;
		*cp = '\0';
		while (line) {
			int n = line % 10;
			*--cp = (char)(n+'0');
			line /= 10;
		}
		while (*cp) {
			serial_send_noint(*cp++);
		}
	} else {
		serial_send_noint('0');
	}
	serial_send_noint('\r');
	serial_send_noint('\n');
}


void serial_assert(char const Q_ROM * const Q_ROM_VAR file, int line)
{
	serial_assert_nostop(file, line);

	while (1) {
		_delay_ms(10);
	}
}


int serial_send_int(unsigned int n)
{
	char buf[10];
	char *bufp;

	bufp = buf + 9;
	*bufp = '\0';
	if (0 == n) {
		bufp--;
		*bufp = '0';
	} else {
		while (n) {
			int nn = n % 10;
			bufp--;
			*bufp = (char)(nn + '0');
			n /= 10;
		}
	}
	return serial_send(bufp);
}


int serial_send_hex_int(unsigned int x)
{
	char buf[10];
	char *bufp;

	static PROGMEM char hexchars[] = "0123456789ABCDEF";

	bufp = buf + 9;
	*bufp = '\0';
	if (0 == x) {
		bufp--;
		*bufp = '0';
	} else {
		while (x) {
			int xx = x & 0x0f;
			char c = pgm_read_byte_near(&(hexchars[xx]));
			bufp--;
			*bufp = c;
			x >>= 4;
		}
	}
	return serial_send(bufp);
}

