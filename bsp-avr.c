#include "bsp.h"
#include "dclock.h"
#include "timekeeper.h"
#include "timedisplay.h"
#include "buttons.h"
#include "serial.h"
#include "toggle-pin.h"
#include "morse.h"
#include "lcd.h"
#include <avr/wdt.h>


/* Copied from lcd.h, for Q_onAssert(). */
void lcd_assert(char const Q_ROM * const Q_ROM_VAR file, int line);
void lcd_assert_nostop(char const Q_ROM * const Q_ROM_VAR file, int line);

#define SB(reg,bit) ((reg) |= (1 << (bit)))
#define CB(reg,bit) ((reg) &= ~(1 << (bit)))


Q_DEFINE_THIS_FILE;


static void timer1_init(void);
static void buttons_init(void);
static void rtc_int_init(void);
static void leds_init(void);


void BSP_QF_onStartup(void)
{
	/* We setup the periodic interrupt after QF has started, so we don't
	   try to push events when the event handling stuff is not ready.
	   Timer 1 doesn't generate any events yet, but will eventually scan
	   the buttons and send button events. */
	timer1_init();
	buttons_init();

	Q_ASSERT( (SREG & (1<<7)) == 0 );

	sei();
	lcd_line2("Start");
}


void BSP_enable_rtc_interrupt(void)
{
	rtc_int_init();
}


static void AVR_sleep(void)
{
	/* Power reduction on SPI (unused) */
	PRR0 = (1 << PRSPI);
	/* Idle sleep mode.  We're mains powered, so it's not a big issue. */
	SMCR = (0b000 << SM0) | (1 << SE);

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


/* Definitions for the LCD brightness PWM pin. */
#define BRIGHT_PORT PORTB
#define BRIGHT_DDR  DDRB
#define BRIGHT_BIT  4


void Q_onAssert(char const Q_ROM * const Q_ROM_VAR file, int line)
{
	wdt_disable();

	/* Disconnect the timer from the pin so it goes full brightness.  We
	   want to be able to see the exception message. */
	BRIGHT_DDR &= ~(1 << BRIGHT_BIT);

	serial_assert_nostop(file, line);
	lcd_assert_nostop(file, line);
	morse_assert(file, line);
}


void BSP_watchdog(void)
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
	postISR(&timekeeper, WATCHDOG_SIGNAL, 0);
}


void BSP_startmain(void)
{
	wdt_reset();
	wdt_disable();
}


void BSP_init(void)
{
	Q_ASSERT( (SREG & (1<<7)) == 0 );
	wdt_reset();
	wdt_enable(WDTO_500MS);
	Q_ASSERT( (SREG & (1<<7)) == 0 );
	WDTCSR |= (1 << WDIE);
	/* Make the Arduino LED pin (D13) an output. */
	DDRB |= (1 << 5);

	/* Initialise the LEDs early, to make sure they're off after the power
	   comes on. */
	leds_init();
}


#define LED_CLOCK_PORT PORTF
#define LED_CLOCK_DDR  DDRF
#define LED_CLOCK_BIT  1
#define LED_DATA_PORT  PORTF
#define LED_DATA_DDR   DDRF
#define LED_DATA_BIT   2


static void
leds_init(void)
{
	static uint8_t leds0[] = {0,0,0,0,0,0};

	SB(LED_CLOCK_DDR, LED_CLOCK_BIT);
	SB(LED_DATA_DDR, LED_DATA_BIT);
	CB(LED_CLOCK_PORT, LED_CLOCK_BIT);
	CB(LED_DATA_PORT, LED_DATA_BIT);
	/* Make sure the LEDs are off, but wait more than 500us first so any
	   glitches from the DDR stuff above aren't seen as the first data
	   bit. */
	_delay_us(600);
	BSP_leds(leds0);
}


void BSP_leds(uint8_t *data)
{
	uint8_t sreg;

	sreg = SREG;
	cli();

	for (uint8_t i=0; i<6; i++) {
		uint8_t d = data[i];
		for (uint8_t j=0; j<8; j++) {
			/* This lowers the clock line to the LED drivers.
			   Putting this first in the loop lengthens the clock
			   cycle, as it lowers the clock line after the end of
			   loop arithmetic. */
			CB(LED_CLOCK_PORT, LED_CLOCK_BIT);
			if (d & 0x80) {
				SB(LED_DATA_PORT, LED_DATA_BIT);
			} else {
				CB(LED_DATA_PORT, LED_DATA_BIT);
			}
			/* Put the next data bit into the high bit of d.  Doing
			   this before we raise the clock increases the data
			   setup time. */
			d <<= 1;
			/* Raise the clock line. */
			SB(LED_CLOCK_PORT, LED_CLOCK_BIT);
		}
	}
	/* Leave the clock line low.  This needs to be here since we lower the
	   clock line at the _start_ of the loop above, so don't do the last
	   lowering of it inside the loop. */
	CB(LED_CLOCK_PORT, LED_CLOCK_BIT);
	/* The LED driver chip requires 500us idle time before it uses the
	   clocked in data to set the LED brightness.  We assume that we don't
	   come back here in that time, and don't worry about a 500us delay. */

	SREG = sreg;
}


static void
rtc_int_init(void)
{
	EICRB = 0b00110000;	/* INT6, rising edge */
	EIMSK |= (1 << 6);	/* INT6 interrupt enable */
	PORTE |= (1 << 6);	/* Pullup on the INT6 input */
}


SIGNAL(INT6_vect)
{
	postISR((&timekeeper), TICK_NORMAL_SIGNAL, 0);
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
 *
 * A secondary function of Timer 1 is producing the buzzer sound.  Normally
 * OC1B is disconnected, but we connect it to its output pin to make the sound.
 */
static void
timer1_init(void)
{
	uint8_t sreg;

	sreg = SREG;
	cli();

	DDRB &= ~(1 << 6);	/* OC1B input */
	TCCR1A =(0 << COM1A1) |
		(0 << COM1A0) |	/* OC1A disconnected */
		(1 << COM1B1) |
		(0 << COM1B0) |	/* OC1B set at 0, cleared on compare match */
		(1 << WGM11 ) |	/* Fast PWM, mode 15, count to OCR1A */
		(1 << WGM10);	/* Fast PWM */
	TCCR1B =(1 << WGM13 ) |	/* Fast PWM */
		(1 << WGM12 ) |	/* Fast PWM */
		(2 << CS10  );	/* CLKio/8 */
	OCR1AH = 0xd2;		/* 0xd2f0 = 54000 */
	OCR1AL = 0xf0;
	OCR1BH = 0;
	OCR1BL = 1;
	TIMSK1 =(1 << OCIE1A);

	SREG = sreg;
}


void BSP_buzzer_on(uint8_t volume)
{
	uint16_t ocr1b;
	uint8_t sreg;

	ocr1b = volume * 106;	/* When volume==255, this gives about 27000,
				   which is half of the timer 1 count range.*/
	sreg = SREG;
	cli();
	OCR1BH = (ocr1b >> 8) & 0xff;
	OCR1BL = ocr1b & 0xff;
	SREG = sreg;
	DDRB |= (1 << 6);
}


void BSP_buzzer_off(void)
{
	uint8_t sreg;

	sreg = SREG;
	cli();
	DDRB &= ~ (1 << 6);
	QF_INT_LOCK();
	OCR1BH = 0;
	OCR1BL = 1;
	SREG = sreg;
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
	Q_ASSERT( ((QActive*)(&timekeeper))->prio );
	postISR_r((&timekeeper), TICK_DECIMAL_32_SIGNAL, decimal_32_counter);
	/* The buttons don't care where we are in the second, so don't send the
	   counter with this signal. */
	postISR_r((&buttons), TICK_DECIMAL_32_SIGNAL, 0);

	watchdog_counter ++;
	if (watchdog_counter >= 7) {
		postISR_r((&timekeeper), WATCHDOG_SIGNAL, 0);
		watchdog_counter = 0;
		/* Turn on the Arduino LED.  It gets turned off when
		   WATCHDOG_SIGNAL is handled. */
		PORTB |= (1 << 5);
	}

	QF_tick();
}

SIGNAL(INT0_vect        ) { Q_ASSERT(0); }
SIGNAL(INT1_vect        ) { Q_ASSERT(0); }
SIGNAL(INT2_vect        ) { Q_ASSERT(0); }
SIGNAL(INT3_vect        ) { Q_ASSERT(0); }
SIGNAL(INT4_vect        ) { Q_ASSERT(0); }
SIGNAL(INT5_vect        ) { Q_ASSERT(0); }
//SIGNAL(INT6_vect        ) { Q_ASSERT(0); }
SIGNAL(INT7_vect        ) { Q_ASSERT(0); }
SIGNAL(PCINT0_vect      ) { Q_ASSERT(0); }
SIGNAL(USB_GEN_vect     ) { Q_ASSERT(0); }
SIGNAL(USB_COM_vect     ) { Q_ASSERT(0); }
//SIGNAL(WDT_vect         ) { Q_ASSERT(0); }
SIGNAL(TIMER2_COMPA_vect) { Q_ASSERT(0); }
SIGNAL(TIMER2_COMPB_vect) { Q_ASSERT(0); }
SIGNAL(TIMER2_OVF_vect  ) { Q_ASSERT(0); }
SIGNAL(TIMER1_CAPT_vect ) { Q_ASSERT(0); }
//SIGNAL(TIMER1_COMPA_vect) { Q_ASSERT(0); }
SIGNAL(TIMER1_COMPB_vect) { Q_ASSERT(0); }
SIGNAL(TIMER1_COMPC_vect) { Q_ASSERT(0); }
SIGNAL(TIMER1_OVF_vect  ) { Q_ASSERT(0); }
SIGNAL(TIMER0_COMPA_vect) { Q_ASSERT(0); }
SIGNAL(TIMER0_COMPB_vect) { Q_ASSERT(0); }
SIGNAL(TIMER0_OVF_vect  ) { Q_ASSERT(0); }
SIGNAL(SPI_STC_vect     ) { Q_ASSERT(0); }
SIGNAL(USART1_RX_vect   ) { Q_ASSERT(0); }
//SIGNAL(USART1_UDRE_vect ) { Q_ASSERT(0); }
SIGNAL(USART1_TX_vect   ) { Q_ASSERT(0); }
SIGNAL(ANALOG_COMP_vect ) { Q_ASSERT(0); }
SIGNAL(ADC_vect         ) { Q_ASSERT(0); }
SIGNAL(EE_READY_vect    ) { Q_ASSERT(0); }
SIGNAL(TIMER3_CAPT_vect ) { Q_ASSERT(0); }
SIGNAL(TIMER3_COMPA_vect) { Q_ASSERT(0); }
SIGNAL(TIMER3_COMPB_vect) { Q_ASSERT(0); }
SIGNAL(TIMER3_COMPC_vect) { Q_ASSERT(0); }
SIGNAL(TIMER3_OVF_vect  ) { Q_ASSERT(0); }
//SIGNAL(TWI_vect         ) { Q_ASSERT(0); }
SIGNAL(SPM_READY_vect   ) { Q_ASSERT(0); }


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
/* Values for my prototype with 2.2k, 1k, and 4.7k resistors. */
#define HYSTERESIS 12
#define SELECT_MIN (80  - HYSTERESIS)
#define SELECT_MAX (80  + HYSTERESIS)
#define UP_MIN     (184 - HYSTERESIS)
#define UP_MAX     (184 + HYSTERESIS)
#define DOWN_MIN   (0               )
#define DOWN_MAX   (12  + HYSTERESIS)


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


/**
 * Generate a PWM signal for LCD brightness.
 */
void BSP_lcd_init(uint8_t pwm)
{
	uint8_t sreg;

	sreg = SREG;
	cli();
	TCCR2A = (0b00 << COM2B0) | /* OC2B disconnected */
		(0b10 << COM2A0) |  /* Clear OC2A on compare match */
		(0b11 << WGM20);    /* WGM = 0b011, Fast PWM */
	TCCR2B = (0 << FOC2A) |
		(0 << FOC2B) |
		(0 << WGM22) |	/* WMG = 0b011 */
		(0b001 << CS20); /* No clock scaling, PWM at 62.5kHZ (16MHz/256)*/
	OCR2A = pwm;
	TIMSK2 = 0; 		/* No interrupts */
	BRIGHT_PORT &= ~(1 << BRIGHT_BIT); /* Switch off the output bit for
					      when we have PWM off. */
	BRIGHT_DDR |= (1 << BRIGHT_BIT); /* Connect the timer to the pin. */
	SREG = sreg;
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


#define MORSE_PORT PORTD
#define MORSE_DDR DDRD
#define MORSE_BIT 6


void BSP_enable_morse_line(void)
{
        CB(MORSE_PORT, MORSE_BIT);
        SB(MORSE_DDR, MORSE_BIT);
}


void BSP_morse_signal(uint8_t onoff)
{
        if (onoff)
                SB(MORSE_PORT, MORSE_BIT);
        else
                CB(MORSE_PORT, MORSE_BIT);
}


void BSP_stop_everything(void)
{
	cli();
	wdt_reset();
	wdt_disable();

	TCCR0A = 0;		/* Stop timer 0 */
	TCCR0B = 0;
	TCCR1A = 0;		/* Stop timer 1 */
	TCCR1B = 0;
	TCCR2A = 0;		/* Stop timer 2 */
	TCCR2B = 0;
	TCCR3A = 0;		/* Stop timer 3 */
	TCCR3B = 0;
	PRR0 = 0xff;
	PRR1 = 0xff;
	DDRA = 0;
	DDRB = 0;
	DDRC = 0;
	DDRD = 0;
	DDRE = 0;
	DDRF = 0;
}


/**
 * Force a chip reset.  We do this by enabling the watchdog, then disabling
 * interrupts and waiting.
 */
void BSP_reset(void)
{
	cli();
	wdt_enable(WDTO_15MS);
	while (1)
                ;
}
