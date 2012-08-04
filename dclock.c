/**
 * @file
 */

#include "dclock.h"
#include "buttons.h"
#include "alarm.h"
#include "lcd.h"
#include "serial.h"
#include "version.h"
#include "bsp.h"
#include "toggle-pin.h"
#include "twi.h"
#include "twi-status.h"
#include "rtc.h"
#include <string.h>
#include <stdio.h>


/** The only active DClock. */
struct DClock dclock;


Q_DEFINE_THIS_FILE;

static QState dclockInitial          (struct DClock *me);
static QState dclockState            (struct DClock *me);
static QState dclockTempBrightnessState(struct DClock *me);
static QState dclockSetState         (struct DClock *me);
static QState dclockSetHoursState    (struct DClock *me);
static QState dclockSetMinutesState  (struct DClock *me);
static QState dclockSetSecondsState  (struct DClock *me);


static QEvent twiQueue[4];
static QEvent dclockQueue[4];
static QEvent buttonsQueue[4];
static QEvent alarmQueue[4];

/* The order of these objects is important, firstly because it determines
   priority, but also because it determines the order in which they are
   initialised.  For instance, dclock sends a signal to twi when dclock starts,
   so twi much be initialised first by placing it earlier in this list. */
QActiveCB const Q_ROM Q_ROM_VAR QF_active[] = {
	{ (QActive *)0              , (QEvent *)0      , 0                        },
	{ (QActive *)(&twi)         , twiQueue         , Q_DIM(twiQueue)          },
	{ (QActive *)(&buttons)     , buttonsQueue     , Q_DIM(buttonsQueue)      },
	{ (QActive *)(&dclock)      , dclockQueue      , Q_DIM(dclockQueue)       },
	{ (QActive *)(&alarm)       , alarmQueue       , Q_DIM(alarmQueue)        },
};
/* If QF_MAX_ACTIVE is incorrectly defined, the compiler says something like:
   lapclock.c:68: error: size of array ‘Q_assert_compile’ is negative
 */
Q_ASSERT_COMPILE(QF_MAX_ACTIVE == Q_DIM(QF_active) - 1);


int main(int argc, char **argv)
{
	uint8_t mcusr;

 startmain:
	mcusr = MCUSR;
	MCUSR = 0;
	TOGGLE_BEGIN();
	BSP_startmain();	/* Disables the watchdog timer. */
	serial_init();
	serial_send_rom(startup_message);
	serial_drain();
	SERIALSTR("*** Reset reason:");
	if (mcusr & (1 << WDRF)) SERIALSTR(" WD");
	if (mcusr & (1 << BORF)) SERIALSTR(" BO");
	if (mcusr & (1 << EXTRF)) SERIALSTR(" EXT");
	if (mcusr & (1 << PORF)) SERIALSTR(" PO");
	SERIALSTR("\r\n");
	twi_ctor();
	lcd_init();
	dclock_ctor();
	buttons_ctor();
	alarm_ctor();

	/* Drain the serial output just before the watchdog timer is
	   reenabled. */
	serial_drain();
	/* Initialize the BSP.  Enables the watchdog timer. */
	BSP_init();

	QF_run();

	goto startmain;
}


static uint32_t
rtc_time_to_decimal_time(const uint8_t *bytes)
{
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint32_t dtime;

	seconds = (bytes[0] & 0x0f) + (10 * ((bytes[0] & 0xf0) >> 4));
	minutes = (bytes[1] & 0x0f) + (10 * ((bytes[1] & 0xf0) >> 4));
	hours   = (bytes[2] & 0x0f) + (10 * ((bytes[2] & 0xf0) >> 4));

	dtime = ((uint32_t)seconds)
		+ (((uint32_t)minutes) * 60L)
		+ (((uint32_t)hours) * 3600L);
	dtime = (dtime * 125L) / 108L;
	return dtime;
}


void QF_onStartup(void)
{
	Q_ASSERT(twi.ready);
	Q_ASSERT(buttons.ready);
	Q_ASSERT(dclock.ready);
	Q_ASSERT(alarm.ready);

	serial_drain();

	BSP_QF_onStartup();
}


void dclock_ctor(void)
{
	QActive_ctor((QActive *)(&dclock), (QStateHandler)&dclockInitial);
	/* Using this value as the initial time allows us to test the code that
	   moves the time across the display each minute. */
	dclock.dseconds = 11745;
	dclock.ready = 0;
}


static QState dclockInitial(struct DClock *me)
{
	lcd_clear();
	LCD_LINE1_ROM("    A clock!");
	LCD_LINE2_ROM(V);
	return Q_TRAN(&dclockState);
}


static void inc_dseconds(struct DClock *me)
{
	/* There is no need to disable interrupts while we access or update
	   dseconds, even though it's a four byte variable, as we never touch
	   it during an interrupt handler. */

	if (me->dseconds > 99999) {
		SERIALSTR("me->dseconds == ");
		char ds[12];
		snprintf(ds, 12, "%lu", me->dseconds);
		serial_send(ds);
		SERIALSTR("\r\n");
		_delay_ms(30);
	}
	Q_ASSERT( me->dseconds <= 99999 );
	if (99999 == me->dseconds) {
		me->dseconds = 0;
	} else {
		me->dseconds++;
	}
}


static void displayTime(struct DClock *me)
{
	char line[17];
	uint32_t sec;
	uint8_t h, m, s;
	uint8_t spaces;

	sec = me->dseconds;
	s = sec % 100;
	sec /= 100;
	m = sec % 100;
	sec /= 100;
	h = sec % 100;
	spaces = m % 9;
	for (uint8_t i=0; i<spaces; i++) {
		line[i] = ' ';
	}
	snprintf(line+spaces, 17-spaces, "%02u.%02u.%02u%s",
		 h, m, s, "        ");
	lcd_line1(line);
}


// FIXME make this take enough parameters to do a write to the RTC (register address) followed by a read or a write.  twi does two requests for a reason.

static void start_rtc_twi_request(struct DClock *me,
				  uint8_t reg, uint8_t nbytes, uint8_t rw)
{
	SERIALSTR("Sending a TWI request\r\n");

	me->twiRequest0.qactive = (QActive*)me;
	me->twiRequest0.signal = TWI_REPLY_0_SIGNAL;
	me->twiRequest0.bytes = me->twiBuffer0;
	me->twiBuffer0[0] = reg;
	me->twiRequest0.nbytes = 1;
	me->twiRequest0.address = RTC_ADDR << 1; /* RW = 0, write */
	me->twiRequest0.count = 0;
	me->twiRequest0.status = 0;
	me->twiRequestAddresses[0] = &(me->twiRequest0);

	me->twiRequest1.qactive = (QActive*)me;
	me->twiRequest1.signal = TWI_REPLY_1_SIGNAL;
	me->twiRequest1.bytes = me->twiBuffer1;
	me->twiRequest1.nbytes = nbytes;
	me->twiRequest1.address = (RTC_ADDR << 1) | (rw ? 1 : 0);
	me->twiRequest1.count = 0;
	me->twiRequest1.status = 0;
	me->twiRequestAddresses[1] = &(me->twiRequest1);

	post(&twi, TWI_REQUEST_SIGNAL, (QParam)((uint16_t)(&(me->twiRequestAddresses))));
}


static QState dclockState(struct DClock *me)
{
	uint8_t d32counter;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->ready = 73;	/* (V)(;,,,;)(V) */
		lcd_clear();
		start_rtc_twi_request(me, 0, 3, 1); /* Start from register 0, 3
						       bytes, read */
		return Q_HANDLED();

	case TWI_REPLY_0_SIGNAL:
		SERIALSTR("TWI_REPLY_0_SIGNAL: ");
		serial_send_rom(twi_status_string(me->twiRequest0.status));
		SERIALSTR("\r\n");
		SERIALSTR("   status was 0x");
		serial_send_hex_int(me->twiRequest0.status);
		SERIALSTR(" &request=");
		serial_send_hex_int((uint16_t)(&me->twiRequest0));
		SERIALSTR("\r\n");
		serial_drain();
		return Q_HANDLED();

	case TWI_REPLY_1_SIGNAL:
		SERIALSTR("TWI_REPLY_1_SIGNAL: ");
		serial_send_rom(twi_status_string(me->twiRequest1.status));
		SERIALSTR("\r\n");
		SERIALSTR("   status was 0x");
		serial_send_hex_int(me->twiRequest1.status);
		SERIALSTR(" &request=");
		serial_send_hex_int((uint16_t)(&me->twiRequest1));
		SERIALSTR("\r\n");
		SERIALSTR("    bytes=");
		serial_send_hex_int(me->twiBuffer1[0]);
		SERIALSTR(",");
		serial_send_hex_int(me->twiBuffer1[1]);
		SERIALSTR(",");
		serial_send_hex_int(me->twiBuffer1[2]);
		SERIALSTR("\r\n");
		serial_drain();
		me->dseconds = rtc_time_to_decimal_time(me->twiBuffer1);
		return Q_HANDLED();

	case WATCHDOG_SIGNAL:
		BSP_watchdog(me);
		return Q_HANDLED();
	case TICK_DECIMAL_32_SIGNAL:
		d32counter = (uint8_t) Q_PAR(me);
		Q_ASSERT( d32counter != 0 );
		Q_ASSERT( d32counter <= 32 );
		if (d32counter == 32) {
			/* We set the decimal 1/32 second counter early, to
			   avoid the possibility that the second calculations
			   and display take much longer than the time before
			   the next timer interrupt, and therefore before the
			   next signal arrives. */
			BSP_set_decimal_32_counter(0);
			/* We've counted 32 parts of a decimal second, so tick
			   over to the next second. */
			post(me, TICK_DECIMAL_SIGNAL, 0);
		}
		return Q_HANDLED();

	case TICK_DECIMAL_SIGNAL:
		inc_dseconds(me);
		displayTime(me);
		post_r((&alarm), TICK_DECIMAL_SIGNAL, me->dseconds);
		return Q_HANDLED();

	case BUTTON_SELECT_PRESS_SIGNAL:
		if (lcd_get_brightness()) {
			return Q_TRAN(dclockSetHoursState);
		} else {
			return Q_TRAN(dclockTempBrightnessState);
		}
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		lcd_inc_brightness();
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		lcd_dec_brightness();
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}


static QState dclockTempBrightnessState(struct DClock *me)
{
	static uint8_t counter;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		lcd_inc_brightness();
		counter = 0;
		return Q_HANDLED();
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_TRAN(dclockState);
	case BUTTON_SELECT_REPEAT_SIGNAL:
		switch (counter) {
		case 0:
		case 32:
		case 64:
			lcd_inc_brightness();
		default:
			counter ++;
			break;
		case 65:
			break;
		}
		return Q_HANDLED();
	case Q_EXIT_SIG:
		while (lcd_get_brightness()) {
			lcd_dec_brightness();
		}
		return Q_HANDLED();
	}
	return Q_SUPER(dclockState);
}


static void displaySettingTime(struct DClock *me)
{
	char line[17];
	uint8_t i;
	uint8_t dots;

	snprintf(line, 17, "%02u.%02u.%02u        ", me->setHours,
		 me->setMinutes, me->setSeconds);
	Q_ASSERT( strlen(line) == 16 );
	i = 9;
	dots = me->setTimeouts;
	Q_ASSERT( dots <= 7 );
	while (dots--) {
		line[i++] = '.';
	}
	Q_ASSERT( i <= 16 );
	Q_ASSERT( line[16] == '\0' );
	lcd_line2(line);
}


/**
 * Display a string on the top line of the LCD.
 *
 * The string is stored in ROM.
 */
static void displayMenuName(const char PROGMEM *name)
{
	char line[17];
	uint8_t i;
	char c;

	i = 0;
	while ( (c=Q_ROM_BYTE(name[i])) ) {
		line[i] = c;
		i++;
	}
	Q_ASSERT( i < 16 );
	while (i < 16) {
		line[i++] = ' ';
	}
	line[i] = '\0';
	Q_ASSERT( line[16] == '\0' );
	lcd_line1(line);
}


/** Wait two seconds in the time setting states between changes of dots. */
#define TSET_TOUT (37*2)

/** Have five dots of timeout in the time setting states. */
#define N_TSET_TOUTS 5


static QState dclockSetState(struct DClock *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG: {
		uint32_t sec = me->dseconds;

		me->timeSetChanged = 0;
		me->setSeconds = sec % 100;
		sec /= 100;
		me->setMinutes = sec % 100;
		sec /= 100;
		me->setHours = sec % 10;
		displaySettingTime(me);
		return Q_HANDLED();
	}
	case TICK_DECIMAL_SIGNAL:
		/* We update the decimal seconds counter while in here, but
		   don't take any other action, as we're displaying the new set
		   time.  The seconds continue to tick over in response to
		   TICK_DECIMAL_SIGNAL (here) and TICK_DECIMAL_32_SIGNAL
		   (above), so if the time does not get changed in here we
		   don't lose track. */
		inc_dseconds(me);
		return Q_HANDLED();
	case UPDATE_TIME_SET_SIGNAL:
		/* We get this whenever the user changes one of the hours,
		   mintues, or seconds.  We have to change the displayed
		   setting time, reset the timeouts, and re-display the name
		   and dots. */
		me->timeSetChanged = 73; /* Need a true value?  Why not 73? */
		me->setTimeouts = N_TSET_TOUTS;
		/* We don't change the signal that QP sends us on timeout,
		   since the signal is specific to the child set state. */
		QActive_arm_sig((QActive*)me, TSET_TOUT, 0);
		displaySettingTime(me);
		post(me, UPDATE_TIME_SET_CURSOR_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_SELECT_LONG_PRESS_SIGNAL:
	case BUTTON_SELECT_REPEAT_SIGNAL:
	case BUTTON_SELECT_RELEASE_SIGNAL:
	case BUTTON_UP_LONG_PRESS_SIGNAL:
	case BUTTON_UP_RELEASE_SIGNAL:
	case BUTTON_DOWN_LONG_PRESS_SIGNAL:
	case BUTTON_DOWN_RELEASE_SIGNAL:
		/* Make sure we ignore these button signals, as parent states
		   may do things with them that will interfere with time
		   setting. */
		return Q_HANDLED();
	case UPDATE_TIME_TIMEOUT_SIGNAL:
		/* Any of our child states can time out.  When they do, we get
		   this signal to tell us that, and we abort the time setting.
		   The child states use their own signals to do the timeout in
		   order to avoid a race condition between them with
		   Q_TIMEOUT_SIG, which is why we don't handle Q_TIMEOUT_SIG
		   here (except to assert that it shouldn't happen.) */
		me->timeSetChanged = 0;
		return Q_TRAN(dclockState);
	case Q_TIMEOUT_SIG:
		Q_ASSERT( 0 );
		return Q_HANDLED();
	case Q_EXIT_SIG:
		QActive_disarm((QActive*)me);
		/* These three values are all unsigned and can all be zero, so
		   we don't need to check the lower bound. */
		Q_ASSERT( me->setHours <= 9 );
		Q_ASSERT( me->setMinutes <= 99 );
		Q_ASSERT( me->setSeconds <= 99 );
		LCD_LINE2_ROM("                ");
		lcd_cursor_off();
		if (me->timeSetChanged) {
			me->dseconds = (me->setHours * 10000L)
				+ (me->setMinutes * 100L) + me->setSeconds;
		}
		lcd_clear();
		displayTime(me);
		return Q_HANDLED();
	}
	return Q_SUPER(dclockState);
}


static QState dclockSetHoursState(struct DClock *me)
{
	static const char PROGMEM setTimeHoursName[] = "Set hours:";

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		QActive_arm_sig((QActive*)me, TSET_TOUT,
				UPDATE_HOURS_TIMEOUT_SIGNAL);
		me->setTimeouts = N_TSET_TOUTS;
		displaySettingTime(me);
		displayMenuName(setTimeHoursName);
		lcd_set_cursor(1, 1);
		return Q_HANDLED();
	case UPDATE_HOURS_TIMEOUT_SIGNAL:
		Q_ASSERT( me->setTimeouts );
		me->setTimeouts --;
		if (0 == me->setTimeouts) {
			post(me, UPDATE_TIME_TIMEOUT_SIGNAL, 0);
		} else {
			displaySettingTime(me);
			QActive_arm_sig((QActive*)me, TSET_TOUT,
					UPDATE_HOURS_TIMEOUT_SIGNAL);
		}
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		Q_ASSERT( me->setHours <= 9 );
		if (9 == me->setHours) {
			me->setHours = 0;
		} else {
			me->setHours++;
		}
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		Q_ASSERT( me->setHours <= 9 );
		if (0 == me->setHours) {
			me->setHours = 9;
		} else {
			me->setHours--;
		}
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case UPDATE_TIME_SET_CURSOR_SIGNAL:
		lcd_set_cursor(1, 1);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(dclockSetMinutesState);
	}
	return Q_SUPER(dclockSetState);
}


static QState dclockSetMinutesState(struct DClock *me)
{
	static const char PROGMEM setTimeMinutesName[] = "Set minutes:";

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		QActive_arm_sig((QActive*)me, TSET_TOUT,
				UPDATE_MINUTES_TIMEOUT_SIGNAL);
		me->setTimeouts = N_TSET_TOUTS;
		displaySettingTime(me);
		displayMenuName(setTimeMinutesName);
		lcd_set_cursor(1, 4);
		return Q_HANDLED();
	case UPDATE_MINUTES_TIMEOUT_SIGNAL:
		Q_ASSERT( me->setTimeouts );
		me->setTimeouts --;
		if (0 == me->setTimeouts) {
			post(me, UPDATE_TIME_TIMEOUT_SIGNAL, 0);
		} else {
			displaySettingTime(me);
			QActive_arm_sig((QActive*)me, TSET_TOUT,
					UPDATE_MINUTES_TIMEOUT_SIGNAL);
		}
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		Q_ASSERT( me->setMinutes <= 99 );
		if (99 == me->setMinutes) {
			me->setMinutes = 0;
		} else {
			me->setMinutes++;
		}
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		Q_ASSERT( me->setMinutes <= 99 );
		if (0 == me->setMinutes) {
			me->setMinutes = 99;
		} else {
			me->setMinutes--;
		}
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case UPDATE_TIME_SET_CURSOR_SIGNAL:
		lcd_set_cursor(1, 4);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(dclockSetSecondsState);
	}
	return Q_SUPER(dclockSetState);
}


static QState dclockSetSecondsState(struct DClock *me)
{
	static const char PROGMEM setTimeSecondsName[] = "Set seconds:";

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		QActive_arm_sig((QActive*)me, TSET_TOUT,
				UPDATE_SECONDS_TIMEOUT_SIGNAL);
		me->setTimeouts = N_TSET_TOUTS;
		displaySettingTime(me);
		displayMenuName(setTimeSecondsName);
		lcd_set_cursor(1, 7);
		return Q_HANDLED();
	case UPDATE_SECONDS_TIMEOUT_SIGNAL:
		Q_ASSERT( me->setTimeouts );
		me->setTimeouts --;
		if (0 == me->setTimeouts) {
			post(me, UPDATE_TIME_TIMEOUT_SIGNAL, 0);
		} else {
			displaySettingTime(me);
			QActive_arm_sig((QActive*)me, TSET_TOUT,
					UPDATE_SECONDS_TIMEOUT_SIGNAL);
		}
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		Q_ASSERT( me->setSeconds <= 99 );
		if (99 == me->setSeconds) {
			me->setSeconds = 0;
		} else {
			me->setSeconds++;
		}
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		Q_ASSERT( me->setSeconds <= 99 );
		if (0 == me->setSeconds) {
			me->setSeconds = 99;
		} else {
			me->setSeconds--;
		}
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case UPDATE_TIME_SET_CURSOR_SIGNAL:
		lcd_set_cursor(1, 7);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(dclockState);
	}
	return Q_SUPER(dclockSetState);
}
