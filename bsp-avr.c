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


static void
buttons_init(void)
{
	/* Set the pins as inputs. */
	DDRC  &= ~ (1 << 0);
	DDRC  &= ~ (1 << 1);
	DDRC  &= ~ (1 << 2);
	/* Enable pull up resistors for the pins. */
	PORTC |=   (1 << 0);
	PORTC |=   (1 << 1);
	PORTC |=   (1 << 2);
}


uint8_t
BSP_getButton(void)
{
	uint8_t pinc = PINC & 0b111;

	if (! (pinc & 0b001))
		return 1;
	if (! (pinc & 0b010))
		return 2;
	if (! (pinc & 0b100))
		return 3;
	return 0;
}
