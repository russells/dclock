#include "bsp.h"
#include "dclock.h"
#include "buttons.h"
#include "serial.h"
#include "toggle-pin.h"
#include <avr/wdt.h>


/* Copied from lcd.h, for Q_onAssert(). */
void lcd_assert(char const Q_ROM * const Q_ROM_VAR file, int line);


Q_DEFINE_THIS_FILE;


static void timer1_init(void);
static void buttons_init(void);


void BSP_QF_onStartup(void)
{
	/* We setup the periodic interrupt after QF has started, so we don't
	   try to push events when the event handling stuff is not ready.
	   Timer 1 doesn't generate any events yet, but will eventually scan
	   the buttons and send button events. */
	timer1_init();
	buttons_init();
}


static void AVR_sleep(void)
{
	/* Power reduction on SPI (unused) */
	PRR = (1 << PRSPI);
	/* Idle sleep mode.  We're mains powered, so it's not a big issue. */
	SMCR = (0 << SM0) | (1 << SE);

	TOGGLE_OFF();

	/* Don't separate the following two assembly instructions.  See
	   Atmel's NOTE03. */
	__asm__ __volatile__ ("sei" "\n\t" :: );
	__asm__ __volatile__ ("sleep" "\n\t" :: );

	SMCR = 0;                                           /* clear the SE bit */
}


void QF_onIdle(void)
{
	AVR_sleep();
}

void Q_onAssert(char const Q_ROM * const Q_ROM_VAR file, int line)
{
	serial_assert_nostop(file, line);
	lcd_assert(file, line);
}


void BSP_watchdog(struct DClock *me)
{
	wdt_reset();
	WDTCSR |= (1 << WDIE);
	/* Turn off the Arduino LED.  It was turned on by the interrupt handler
	   that sent the WATCHDOG_SIGNAL. */
	PORTB &= ~ (1 << 5);
}


SIGNAL(WDT_vect)
{
	/* There's no point calling postISR_r() instead of postISR() here.  If
	   we don't send this signal we're hosed by the next WDT timeout
	   anyway. */
	postISR(&dclock, WATCHDOG_SIGNAL, 0);
}


void BSP_startmain(void)
{

}


void BSP_init(void)
{
	wdt_reset();
	wdt_enable(WDTO_500MS);
	WDTCSR |= (1 << WDIE);
	/* Make the Arduino LED pin (D13) an output. */
	DDRB |= (1 << 5);
}


/**
 * @brief Set up timer 1 to generate a periodic interrupt.
 *
 * We use this interrupt to count decimal seconds, to scan keys, and for
 * QP-nano periodic processing.
 *
 * This is done independently of the RTC so we can monitor the RTC's
 * availablity separately.  If the RTC is unavailable (disconnected or broken)
 * the user interface can continue to function, and we could even continue to
 * function as a less accurate clock.
 *
 * We set up the timer to give 32 interrupts in a decimal second - ie 32
 * interrupts in 0.864 seconds.
 *
 * During development, this will be the real time clock source.
 *
 * Timer calculations: CLKio==16MHz.  Then CLKio/8 == 2MHz.  Divide that by
 * 62500 == 32Hz.  Multiply that by 0.864 to give 32 periods in a decimal
 * second == 54000.
 */
static void
timer1_init(void)
{
	cli();

	TCCR1A =(0 << COM1A1) |
		(0 << COM1A0) |	/* OC1A disconnected */
		(0 << COM1B1) |
		(0 << COM1B0) |	/* OC1B disconnected */
		(0 << WGM11 ) |	/* CTC, 4, count to OCR1A */
		(0 << WGM10);	/* CTC */
	TCCR1B =(0 << WGM13 ) |	/* CTC */
		(1 << WGM12 ) |	/* CTC */
		(2 << CS10  );	/* CLKio/8 */
	OCR1AH = 0xd2;		/* 0xd2f0 = 54000 */
	OCR1AL = 0xf0;
	TIMSK1 =(1 << OCIE1A);

	sei();
}


/**
 * Increments each TICK_DECIMAL_32_SIGNAL, so we can see where in the decimal
 * second an event was generated.  Set to zero by the code that ticks over
 * decimal seconds.
 */
static uint8_t decimal_32_counter;


void BSP_set_decimal_32_counter(uint8_t dc)
{
	uint8_t sreg;
	sreg = SREG;
	cli();
	decimal_32_counter = dc;
	SREG = sreg;
}


/**
 * @brief Handle the periodic interrupt from timer 1.
 *
 * The buttons are scanned in response to the TICK_DECIMAL_32_SIGNAL.
 *
 * @todo If the RTC is not functioning, send TICK_RTC32_SIGNALs.
 */
SIGNAL(TIMER1_COMPA_vect)
{
	static uint8_t watchdog_counter = 0;

	TOGGLE_ON();
	/* Increment the counter before sending the event.  We should never
	   send a zero.  No real reason, just the way it is. */
	decimal_32_counter ++;
	postISR_r((&dclock), TICK_DECIMAL_32_SIGNAL, decimal_32_counter);
	/* The buttons don't care where we are in the second, so don't send the
	   counter with this signal. */
	postISR_r((&buttons), TICK_DECIMAL_32_SIGNAL, 0);
	watchdog_counter ++;
	if (watchdog_counter >= 7) {
		postISR_r((&dclock), WATCHDOG_SIGNAL, 0);
		watchdog_counter = 0;
		/* Turn on the Arduino LED.  It gets turned off when
		   WATCHDOG_SIGNAL is handled. */
		PORTB |= (1 << 5);
	}
	QF_tick();
}


/**
 * Set the ADC to channel 0, Vcc reference.
 */
static void
buttons_init(void)
{

	ADMUX = (0b01 << REFS0) |
		(1 << ADLAR) |
		(0b0000 << MUX0); /* ADC0 */
	ADCSRA = (1 << ADEN) |
		(0 << ADSC) |
		(0 << ADATE) |
		(1 << ADIF) |
		(0 << ADIE) |
		(0b110 << ADPS0); /* 16MHZ/64 = 250kHz ADC clock, for speed */
	ADCSRB = (0 << ACME) |
		(0b000 << ADTS0);
}


/*
#define HYSTERESIS 12
#define SELECT_MIN 0
#define SELECT_MAX (2 * HYSTERESIS)
#define UP_MIN     (123 - HYSTERESIS)
#define UP_MAX     (123 + HYSTERESIS)
#define DOWN_MIN   (215 - HYSTERESIS)
#define DOWN_MAX   (215 + HYSTERESIS)
*/
/* Values for the Freetonics LCD Keypad Shield. */
#define HYSTERESIS 8
#define SELECT_MIN (185 - HYSTERESIS)
#define SELECT_MAX (185 + HYSTERESIS)
#define UP_MIN     (36  - HYSTERESIS)
#define UP_MAX     (36  + HYSTERESIS)
#define DOWN_MIN   (82  - HYSTERESIS)
#define DOWN_MAX   (82  + HYSTERESIS)


uint8_t
BSP_getButton(void)
{
	uint16_t adc_value;

	ADCSRA |= (1 << ADSC);
	while (ADCSRA & (1 << ADSC))
		;
	adc_value = ADCH;

	if (adc_value >= SELECT_MIN && adc_value <= SELECT_MAX)
		return 1;
	if (adc_value >= UP_MIN && adc_value <= UP_MAX)
		return 2;
	if (adc_value >= DOWN_MIN && adc_value <= DOWN_MAX)
		return 3;
	return 0;
}


#define BRIGHT_PORT PORTD
#define BRIGHT_DDR  DDRD
#define BRIGHT_BIT  3


/**
 * Generate a PWM signal for LCD brightness.
 */
void BSP_lcd_init(uint8_t pwm)
{
	cli();
	TCCR2A = (0b00 << COM2A0) | /* OC2A disconnected */
		(0b10 << COM2B0) |  /* Clear OC2B on compare match */
		(0b11 << WGM20);    /* WGM = 0b011, Fast PWM */
	TCCR2B = (0 << FOC2A) |
		(0 << FOC2B) |
		(0 << WGM22) |	/* WMG = 0b011 */
		(0b001 << CS20); /* No clock scaling, PWM at 62.5kHZ (16MHz/256)*/
	OCR2B = pwm;
	TIMSK2 = 0; 		/* No interrupts */
	BRIGHT_PORT &= ~(1 << BRIGHT_BIT); /* Switch off the output bit for
					      when we have PWM off. */
	BRIGHT_DDR |= (1 << BRIGHT_BIT); /* Connect the timer to the pin. */
	sei();
}


void BSP_lcd_pwm(uint8_t pwm)
{
	OCR2B = pwm;
}


void BSP_lcd_pwm_on(void)
{
	/* This is all we need to reconnect the timer to the output pin. */
	TCCR2A |= (1 << COM2B1);
}


void BSP_lcd_pwm_off(void)
{
	/* Disconnect the timer. */
	TCCR2A &= ~ (1 << COM2B1);
}
