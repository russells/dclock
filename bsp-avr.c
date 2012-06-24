#include "bsp.h"
#include "dclock.h"
#include "buttons.h"
#include "toggle-pin.h"


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

}


void BSP_startmain(void)
{

}


void BSP_init(void)
{

}


/**
 * @brief Set up timer 1 to generate a periodic interrupt.
 *
 * We can use this interrupt to scan keys and for QP-nano periodic processing.
 *
 * Timer calculations: CLKio==16MHz.  Then CLKio/8 == 2MHz.  Divide that by
 * 20000 == 100Hz.  The 20000 divisor gives us some leeway to adjust the time
 * of day clock (although adjustment by 1 means 4.32 seconds per day, so some
 * additional smarts may be needed).  100Hz may be a slightly high sample rate
 * for the buttons, but we can always sample on a subset of interrupts.
 *
 * @todo Implement the real time clock.
 * @todo Implement adjustments for the real time clock.
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
	OCR1AH = 0x4e;		/* 0x4e20 = 20000 */
	OCR1AL = 0x20;
	TIMSK1 =(1 << OCIE1A);

	TOGGLE_DDR  |= (1<<TOGGLE_PIN);	/* output */
	TOGGLE_PORT |= (1<<TOGGLE_PIN);	/* high, toggled by interrupts and
					   AVR_sleep() */

	sei();
}


/**
 * @brief Handle the periodic interrupt from timer 1.
 *
 * @todo Scan the buttons.
 */
SIGNAL(TIMER1_COMPA_vect)
{
	TOGGLE_ON();
	fff(&dclock);
	QActive_postISR((QActive*)(&dclock), TICK32_SIGNAL, 0);
	QActive_postISR((QActive*)(&buttons), TICK32_SIGNAL, 0);
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
