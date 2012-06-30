#include "bsp.h"
#include "dclock.h"
#include "buttons.h"
#include "toggle-pin.h"
#include <avr/wdt.h>


Q_DEFINE_THIS_FILE;


static void timer1_init(void);
static void buttons_init(void);


void QF_onStartup(void)
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
	//serial_assert(file, line);
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
	QActive_postISR((QActive*)(&dclock), WATCHDOG_SIGNAL, 0);
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
 * We use this interrupt to scan keys and for QP-nano periodic processing.
 *
 * This is done independently of the RTC so we can monitor the RTC's
 * availablity separately.  If the RTC is unavailable (disconnected or broken)
 * the user interface can continue to function, and we could even continue to
 * function as a less accurate clock.
 *
 * During development, this will be the real time clock source.
 *
 * Timer calculations: CLKio==16MHz.  Then CLKio/8 == 2MHz.  Divide that by
 * 62500 == 32Hz.
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
	OCR1AH = 0xf4;		/* 0xf424 = 62500 */
	OCR1AL = 0x24;
	TIMSK1 =(1 << OCIE1A);

	sei();
}


/**
 * @brief Handle the periodic interrupt from timer 1.
 *
 * The buttons are scanned in response to the TICK32_SIGNAL.
 *
 * @todo If the RTC is not functioning, send TICK_RTC32_SIGNALs.
 */
SIGNAL(TIMER1_COMPA_vect)
{
	static uint8_t watchdog_counter = 0;

	TOGGLE_ON();
	fff(&dclock);
	QActive_postISR((QActive*)(&dclock), TICK32_SIGNAL, 0);
	QActive_postISR((QActive*)(&buttons), TICK32_SIGNAL, 0);
	watchdog_counter ++;
	if (watchdog_counter >= 7) {
		fff(&dclock);
		QActive_postISR((QActive*)(&dclock), WATCHDOG_SIGNAL, 0);
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


#define HYSTERESIS 12
#define SELECT_MIN 0
#define SELECT_MAX (2 * HYSTERESIS)
#define UP_MIN     (123 - HYSTERESIS)
#define UP_MAX     (123 + HYSTERESIS)
#define DOWN_MIN   (215 - HYSTERESIS)
#define DOWN_MAX   (215 + HYSTERESIS)


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
