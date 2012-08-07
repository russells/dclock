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
#include "timekeeper.h"
#include "time-utils.h"
#include "rtc.h"
#include <string.h>
#include <stdio.h>


/** The only active DClock. */
struct DClock dclock;


Q_DEFINE_THIS_FILE;

static QState dclockInitial            (struct DClock *me);
static QState dclockState              (struct DClock *me);
static QState dclockTempBrightnessState(struct DClock *me);
static QState dclockSetTimeState       (struct DClock *me);
static QState dclockSetHoursState      (struct DClock *me);
static QState dclockSetMinutesState    (struct DClock *me);
static QState dclockSetSecondsState    (struct DClock *me);
static QState dclockSetAlarmState      (struct DClock *me);


static QEvent twiQueue[4];
static QEvent dclockQueue[4];
static QEvent buttonsQueue[4];
static QEvent alarmQueue[4];
static QEvent timekeeperQueue[4];

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
	{ (QActive *)(&timekeeper)  , timekeeperQueue  , Q_DIM(timekeeperQueue)   },
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
	timekeeper_ctor();
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


void QF_onStartup(void)
{
	Q_ASSERT(twi.ready);
	Q_ASSERT(timekeeper.ready);
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
	dclock.ready = 0;
	dclock.settingWhich = 0;
}


static QState dclockInitial(struct DClock *me)
{
	lcd_clear();
	LCD_LINE1_ROM("    A clock!");
	LCD_LINE2_ROM(V);
	return Q_TRAN(&dclockState);
}


static void displayTime(struct DClock *me, uint32_t dseconds)
{
	char line[17];
	uint8_t h, m, s;
	uint8_t spaces;

	s = dseconds % 100;
	dseconds /= 100;
	m = dseconds % 100;
	dseconds /= 100;
	h = dseconds % 100;
	spaces = m % 9;
	for (uint8_t i=0; i<spaces; i++) {
		line[i] = ' ';
	}
	snprintf(line+spaces, 17-spaces, "%02u.%02u.%02u%s",
		 h, m, s, "        ");
	lcd_line1(line);
}


static QState dclockState(struct DClock *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->ready = 73;	/* (V)(;,,,;)(V) */
		lcd_clear();
		return Q_HANDLED();

	case TICK_DECIMAL_SIGNAL:
		displayTime(me, Q_PAR(me));
		return Q_HANDLED();

	case BUTTON_SELECT_PRESS_SIGNAL:
		if (lcd_get_brightness()) {
			return Q_HANDLED();
		} else {
			return Q_TRAN(dclockTempBrightnessState);
		}
	case BUTTON_SELECT_RELEASE_SIGNAL:
		/* We react to a release signal because we want to distinguish
		   between a button press and a long press.  If we see a
		   release signal here, that means that we must have had a
		   short press, otherwise we would have transitioned out of
		   this state via a long press. */
		me->settingWhich = SETTING_ALARM;
		return Q_TRAN(dclockSetHoursState);
	case BUTTON_SELECT_LONG_PRESS_SIGNAL:
		me->settingWhich = SETTING_TIME;
		return Q_TRAN(dclockSetHoursState);
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
	case BUTTON_SELECT_LONG_PRESS_SIGNAL:
		/* Ignore this signal because it's used by the parent state to
		   transition somewhere else. */
		return Q_HANDLED();
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

	snprintf(line, 17, "%02u.%02u.%02u        ", me->setTime[0],
		 me->setTime[1], me->setTime[2]);
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
static void displayMenuName(const char Q_ROM *name)
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
	case Q_ENTRY_SIG:
		me->timeSetChanged = 0;
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		/* Ignore the seconds ticking over while setting the time. */
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
	case BUTTON_UP_LONG_PRESS_SIGNAL:
	case BUTTON_UP_RELEASE_SIGNAL:
	case BUTTON_DOWN_LONG_PRESS_SIGNAL:
	case BUTTON_DOWN_RELEASE_SIGNAL:
		/* Make sure we ignore these button signals, as parent states
		   may do things with them that will interfere with time
		   setting. */
		return Q_HANDLED();
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_TRAN(dclockState);
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
		Q_ASSERT( me->setTime[0] <= 9 );
		Q_ASSERT( me->setTime[1] <= 99 );
		Q_ASSERT( me->setTime[2] <= 99 );
		LCD_LINE2_ROM("                ");
		lcd_cursor_off();
		lcd_clear();
		displayTime(me, get_dseconds(&timekeeper));
		return Q_HANDLED();
	}
	return Q_SUPER(dclockState);
}


static QState dclockSetTimeState(struct DClock *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("> dclockSetTimeState\r\n");
		get_dtimes(&timekeeper, me->setTime);
		displaySettingTime(me);
		return Q_HANDLED();
	case Q_EXIT_SIG:
		SERIALSTR("< dclockSetTimeState\r\n");
		if (me->timeSetChanged) {
			set_dtimes(&timekeeper, me->setTime);
		}
		return Q_HANDLED();
	}
	return Q_SUPER(dclockSetState);
}


static QState dclockSetAlarmState(struct DClock *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("> dclockSetAlarmState\r\n");
		get_alarm_dtimes(&alarm, &(me->setTime[0]),
				 &(me->setTime[1]), &(me->setTime[2]));
		displaySettingTime(me);
		return Q_HANDLED();
	case Q_EXIT_SIG:
		SERIALSTR("< dclockSetAlarmState\r\n");
		if (me->timeSetChanged) {
			set_alarm_dtimes(&alarm, me->setTime[0],
					 me->setTime[1], + me->setTime[2]);
		}
		return Q_HANDLED();
	}
	return Q_SUPER(dclockSetState);
}


/**
 * This state has two parent states - dclockSetTimeState() and
 * dclockSetAlarmState().
 *
 * The parent state is selected dynamically based on which of the current time
 * or the alarm time we are setting right now.  That decision is based on
 * me->settingWhich, which should not change for the duration of any one time
 * setting user operation.  settingWhich is set by the code that begins the
 * transition to here (in dclockState()), and unset when we exit
 * dclockSetState().
 */
static QState dclockSetHoursState(struct DClock *me)
{
	static const char Q_ROM setTimeHoursName[] = "Set hours:";
	static const char Q_ROM setAlarmHoursName[] = "Alarm hours:";

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		QActive_arm_sig((QActive*)me, TSET_TOUT,
				UPDATE_HOURS_TIMEOUT_SIGNAL);
		me->setTimeouts = N_TSET_TOUTS;
		displaySettingTime(me);
		switch (me->settingWhich) {
		case SETTING_TIME:
			displayMenuName(setTimeHoursName);
			break;
		case SETTING_ALARM:
			displayMenuName(setAlarmHoursName);
			break;
		default:
			Q_ASSERT(0);
		}
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
			post(me, UPDATE_TIME_SET_CURSOR_SIGNAL, 0);
		}
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		me->setTime[0] = inc_hours(me->setTime[0]);
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		me->setTime[0] = dec_hours(me->setTime[0]);
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case UPDATE_TIME_SET_CURSOR_SIGNAL:
		lcd_set_cursor(1, 1);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(dclockSetMinutesState);
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_HANDLED();
	}

	switch (me->settingWhich) {
	case SETTING_TIME:
		return Q_SUPER(dclockSetTimeState);
	case SETTING_ALARM:
		return Q_SUPER(dclockSetAlarmState);
	default:
		Q_ASSERT(0);
		return Q_SUPER(dclockSetTimeState);
	}
}


/**
 * @see dclockSetHoursState()
 */
static QState dclockSetMinutesState(struct DClock *me)
{
	static const char Q_ROM setTimeMinutesName[] = "Set minutes:";
	static const char Q_ROM setAlarmMinutesName[] = "Alarm minutes:";

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		QActive_arm_sig((QActive*)me, TSET_TOUT,
				UPDATE_MINUTES_TIMEOUT_SIGNAL);
		me->setTimeouts = N_TSET_TOUTS;
		displaySettingTime(me);
		switch (me->settingWhich) {
		case SETTING_TIME:
			displayMenuName(setTimeMinutesName);
			break;
		case SETTING_ALARM:
			displayMenuName(setAlarmMinutesName);
			break;
		default:
			Q_ASSERT(0);
		}
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
			post(me, UPDATE_TIME_SET_CURSOR_SIGNAL, 0);
		}
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		me->setTime[1] = inc_minutes(me->setTime[1]);
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		me->setTime[1] = dec_minutes(me->setTime[1]);
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case UPDATE_TIME_SET_CURSOR_SIGNAL:
		lcd_set_cursor(1, 4);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(dclockSetSecondsState);
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_HANDLED();
	}

	switch (me->settingWhich) {
	case SETTING_TIME:
		return Q_SUPER(dclockSetTimeState);
	case SETTING_ALARM:
		return Q_SUPER(dclockSetAlarmState);
	default:
		Q_ASSERT(0);
		return Q_SUPER(dclockSetTimeState);
	}
}


/**
 * @see dclockSetHoursState()
 */
static QState dclockSetSecondsState(struct DClock *me)
{
	static const char Q_ROM setTimeSecondsName[] = "Set seconds:";
	static const char Q_ROM setAlarmSecondsName[] = "Alarm seconds:";

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		QActive_arm_sig((QActive*)me, TSET_TOUT,
				UPDATE_SECONDS_TIMEOUT_SIGNAL);
		me->setTimeouts = N_TSET_TOUTS;
		displaySettingTime(me);
		switch (me->settingWhich) {
		case SETTING_TIME:
			displayMenuName(setTimeSecondsName);
			break;
		case SETTING_ALARM:
			displayMenuName(setAlarmSecondsName);
			break;
		default:
			Q_ASSERT(0);
		}
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
			post(me, UPDATE_TIME_SET_CURSOR_SIGNAL, 0);
		}
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		me->setTime[2] = inc_seconds(me->setTime[2]);
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		me->setTime[2] = dec_seconds(me->setTime[2]);
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case UPDATE_TIME_SET_CURSOR_SIGNAL:
		lcd_set_cursor(1, 7);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(dclockSetState);
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_HANDLED();
	}

	switch (me->settingWhich) {
	case SETTING_TIME:
		return Q_SUPER(dclockSetTimeState);
	case SETTING_ALARM:
		return Q_SUPER(dclockSetAlarmState);
	default:
		Q_ASSERT(0);
		return Q_SUPER(dclockSetTimeState);
	}
}
