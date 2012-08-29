#include "timesetter.h"
#include "timedisplay.h"
#include "timekeeper.h"
#include "time.h"
#include "lcd.h"
#include "dclock.h"
#include "alarm.h"
#include "serial.h"
#include <stdio.h>


static QState initial              (struct TimeSetter *me);
static QState top                  (struct TimeSetter *me);
static QState tempBrightnessState  (struct TimeSetter *me);
static QState setTimePauseState    (struct TimeSetter *me);
static QState setTimeState         (struct TimeSetter *me);
static QState setHoursState        (struct TimeSetter *me);
static QState setMinutesState      (struct TimeSetter *me);
static QState setSecondsState      (struct TimeSetter *me);
static QState setAlarmState        (struct TimeSetter *me);
static QState setAlarmPauseState   (struct TimeSetter *me);
static QState setAlarmOnOffState   (struct TimeSetter *me);


struct TimeSetter timesetter;


Q_DEFINE_THIS_FILE;


void timesetter_ctor(void)
{
	QActive_ctor((QActive*)(&timesetter), (QStateHandler)initial);
	timesetter.ready = 0;
	timesetter.settingWhich = 0;
}


static QState initial(struct TimeSetter *me)
{
	me->ready = 73;
	return Q_TRAN(top);
}


static QState top(struct TimeSetter *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->ready = 73;	/* (V)(;,,,;)(V) */
		lcd_clear();
		return Q_HANDLED();

	case BUTTON_SELECT_PRESS_SIGNAL:
		if (lcd_get_brightness()) {
			return Q_HANDLED();
		} else {
			return Q_TRAN(tempBrightnessState);
		}
	case BUTTON_SELECT_RELEASE_SIGNAL:
		/* We react to a release signal because we want to distinguish
		   between a button press and a long press.  If we see a
		   release signal here, that means that we must have had a
		   short press, otherwise we would have transitioned out of
		   this state via a long press. */
		me->settingWhich = SETTING_ALARM;
		return Q_TRAN(setAlarmPauseState);
	case BUTTON_SELECT_LONG_PRESS_SIGNAL:
		me->settingWhich = SETTING_TIME;
		return Q_TRAN(setTimePauseState);
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


static QState tempBrightnessState(struct TimeSetter *me)
{
	static uint8_t counter;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		lcd_inc_brightness();
		counter = 0;
		return Q_HANDLED();
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_TRAN(top);
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
	return Q_SUPER(top);
}


static void displayWithTimeout(struct TimeSetter *me, char *line)
{
	uint8_t dots;

	dots = me->setTimeouts;
	Q_ASSERT( dots < 7 );
	switch (dots) {
	case 6:
		line[15] = '.';
		line[14] = '.';
		line[13] = '.';
		break;
	case 5:
		line[15] = '*';
		line[14] = '.';
		line[13] = '.';
		break;
	case 4:
		line[14] = '.';
		line[13] = '.';
		break;
	case 3:
		line[14] = '*';
		line[13] = '.';
		break;
	case 2:
		line[13] = '.';
		break;
	case 1:
		line[13] = '*';
		break;
	}
	Q_ASSERT( line[16] == '\0' );
	lcd_line2(line);
}


/**
 * Display the current state of what we are setting on the bottom line.
 *
 * When setting the alarm, display "On" or "OFF", hours, minutes, and countdown
 * timer.  When setting the time, display blank space, hours, minutes, seconds,
 * and countdown timer.  The layout is arranged so that the hours and minutes
 * appear in the same place on the top line in alarm and time setting.
 */
static void displaySettingTime(struct TimeSetter *me)
{
	char line[17];
	char separator;

	switch (get_time_mode()) {
	case NORMAL_MODE:
		separator = ':';
		break;
	case DECIMAL_MODE:
		separator = '.';
		break;
	default:
		separator = 0;
		Q_ASSERT( 0 );
	}

	switch (me->settingWhich) {
	case SETTING_TIME:
		snprintf(line, 17, "    %02u%c%02u%c%02u    ",
			 me->setTime[0], separator, me->setTime[1], separator,
			 me->setTime[2]);
		break;
	case SETTING_ALARM:
		snprintf(line, 17, "%s %02u%c%02u       ",
			 (me->alarmOn ? "On " : "OFF"),
			 me->setTime[0], separator, me->setTime[1]);
		break;
	default:
		Q_ASSERT( 0 );
	}
	Q_ASSERT( line[16] == '\0' );
	displayWithTimeout(me, line);
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


/** Wait 1.5 seconds in the time setting states between changes of dots. */
#define TSET_TOUT ((37*3)/2)

/** Have six stages of timeout in the time setting states. */
#define N_TSET_TOUTS 6


static QState setState(struct TimeSetter *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		post((&timedisplay), SETTING_TIME_SIGNAL, 0);
		me->timeSetChanged = 0;
		return Q_HANDLED();
	case UPDATE_TIME_SET_SIGNAL:
		/* We get this whenever the user changes one of the hours,
		   mintues, or seconds.  We have to change the displayed
		   setting time, reset the timeouts, and re-display the name
		   and dots. */
		me->timeSetChanged = 73; /* Need a true value?  Why not 73? */
		/* We don't change the signal that QP sends us on timeout,
		   since the signal is specific to the child set state. */
		QActive_arm_sig((QActive*)me, TSET_TOUT, 0);
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
		return Q_TRAN(top);
	case UPDATE_TIME_TIMEOUT_SIGNAL:
		/* Any of our child states can time out.  When they do, we get
		   this signal to tell us that, and we abort the time setting.
		   The child states use their own signals to do the timeout in
		   order to avoid a race condition between them with
		   Q_TIMEOUT_SIG, which is why we don't handle Q_TIMEOUT_SIG
		   here (except to assert that it shouldn't happen.) */
		me->timeSetChanged = 0;
		return Q_TRAN(top);
	case Q_TIMEOUT_SIG:
		Q_ASSERT( 0 );
		return Q_HANDLED();
	case Q_EXIT_SIG:
		post((&timedisplay), SETTING_TIME_FINISHED_SIGNAL, 0);
		QActive_disarm((QActive*)me);
		LCD_LINE2_ROM("                ");
		lcd_cursor_off();
		lcd_clear();
		return Q_HANDLED();
	}
	return Q_SUPER(top);
}


/**
 * This state is here so we can give the TimeDisplay a moment to stop updating.
 *
 * The problem is that when we send TimeDisplay a SETTING_TIME_SIGNAL, there
 * could already be a tick signal in its queue.  So it may overwrite the top
 * line of the display after we have written the top line with the menu name.
 * But if we wait for one tick here, that gives time for TimeDisplay's queue to
 * be processed, including the SETTING_TIME_SIGNAL.  So even if there is a tick
 * event there, it will be processed before we update the top line.
 */
static QState setTimePauseState(struct TimeSetter *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		QActive_arm((QActive*)me, 1);
		return Q_HANDLED();
	case Q_TIMEOUT_SIG:
		return Q_TRAN(setHoursState);
	}
	return Q_SUPER(setState);
}


static QState setTimeState(struct TimeSetter *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("> setTimeState\r\n");
		get_times(me->setTime);
		displaySettingTime(me);
		return Q_HANDLED();
	case UPDATE_TIME_SET_SIGNAL:
		me->setTimeouts = N_TSET_TOUTS;
		displaySettingTime(me);
		return Q_SUPER(setState);
	case Q_EXIT_SIG:
		SERIALSTR("< setTimeState");
		if (me->timeSetChanged) {
			SERIALSTR(" changed\r\n");
			set_times(me->setTime);
		} else {
			SERIALSTR(" no change\r\n");
		}
		return Q_HANDLED();
	}
	return Q_SUPER(setState);
}


/**
 * @see setTimePauseState()
 */
static QState setAlarmPauseState(struct TimeSetter *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		QActive_arm((QActive*)me, 1);
		return Q_HANDLED();
	case Q_TIMEOUT_SIG:
		return Q_TRAN(setAlarmOnOffState);
	}
	return Q_SUPER(setState);
}


static QState setAlarmOnOffState(struct TimeSetter *me)
{
	static const char Q_ROM setAlarmOnOffName[] = "Alarm:";

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		Q_ASSERT( me->settingWhich == SETTING_ALARM );
		QActive_arm_sig((QActive*)me, TSET_TOUT,
				UPDATE_ALARM_TIMEOUT_SIGNAL);
		me->setTimeouts = N_TSET_TOUTS;
		me->alarmOn = get_alarm_state(&alarm);
		displayMenuName(setAlarmOnOffName);
		/* We have to get the alarm times here since the hours and
		   minutes are displayed when we are setting the on/off
		   state. */
		get_alarm_times(&alarm, me->setTime);
		displaySettingTime(me);
		lcd_set_cursor(1, 0);
		return Q_HANDLED();
	case UPDATE_TIME_SET_SIGNAL:
		me->setTimeouts = N_TSET_TOUTS;
		displaySettingTime(me);
		return Q_SUPER(setState);
	case UPDATE_ALARM_TIMEOUT_SIGNAL:
		Q_ASSERT( me->setTimeouts );
		me->setTimeouts --;
		if (0 == me->setTimeouts) {
			post(me, UPDATE_TIME_TIMEOUT_SIGNAL, 0);
		} else {
			displaySettingTime(me);
			QActive_arm_sig((QActive*)me, TSET_TOUT,
					UPDATE_ALARM_TIMEOUT_SIGNAL);
			post(me, UPDATE_TIME_SET_CURSOR_SIGNAL, 0);
		}
		return Q_HANDLED();
	case UPDATE_TIME_SET_CURSOR_SIGNAL:
		lcd_set_cursor(1, 0);
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		if (me->alarmOn) {
			me->alarmOn = 0;
		} else {
			me->alarmOn = 73;
		}
		displaySettingTime(me);
		post(me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		if (me->alarmOn) {
			return Q_TRAN(setHoursState);
		} else {
			/* The user wants the alarm off.  If that's a new
			   setting, then tell the alarm to go off. */
			if (me->timeSetChanged) {
				set_alarm_times(&timekeeper, me->setTime, 0);
				post(&alarm, ALARM_OFF_SIGNAL, 0);
			}
			return Q_TRAN(setState);
		}
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_HANDLED();
	}
	return Q_SUPER(setState);
}


static QState setAlarmState(struct TimeSetter *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("> setAlarmState\r\n");
		displaySettingTime(me);
		return Q_HANDLED();
	case UPDATE_TIME_SET_SIGNAL:
		me->setTimeouts = N_TSET_TOUTS;
		displaySettingTime(me);
		return Q_SUPER(setState);
	case Q_EXIT_SIG:
		SERIALSTR("< setAlarmState ");
		if (me->timeSetChanged) {
			set_alarm_times(&timekeeper, me->setTime, 1);
		} else {
			if (me->alarmOn) {
				SERIALSTR("no change, on\r\n");
			} else {
				SERIALSTR("no change, off\r\n");
			}
		}
		return Q_HANDLED();
	}
	return Q_SUPER(setState);
}


/**
 * This state has two parent states - setTimeState() and setAlarmState().
 *
 * The parent state is selected dynamically based on which of the current time
 * or the alarm time we are setting right now.  That decision is based on
 * me->settingWhich, which should not change for the duration of any one time
 * setting user operation.  settingWhich is set by the code that begins the
 * transition to here (in top()), and unset when we exit setState().
 */
static QState setHoursState(struct TimeSetter *me)
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
		lcd_set_cursor(1, 5);
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
		lcd_set_cursor(1, 5);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(setMinutesState);
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_HANDLED();
	}

	switch (me->settingWhich) {
	case SETTING_TIME:
		return Q_SUPER(setTimeState);
	case SETTING_ALARM:
		return Q_SUPER(setAlarmState);
	default:
		Q_ASSERT(0);
		return Q_SUPER(setTimeState);
	}
}


/**
 * @see setHoursState()
 */
static QState setMinutesState(struct TimeSetter *me)
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
		lcd_set_cursor(1, 8);
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
		lcd_set_cursor(1, 8);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		switch (me->settingWhich) {
		case SETTING_TIME:
			return Q_TRAN(setSecondsState);
		case SETTING_ALARM:
			/* We don't set the seconds on the alarm, so don't
			   transition to that state. */
			return Q_TRAN(setState);
		default:
			Q_ASSERT( 0 );
			break;
		}
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_HANDLED();
	}

	switch (me->settingWhich) {
	case SETTING_TIME:
		return Q_SUPER(setTimeState);
	case SETTING_ALARM:
		return Q_SUPER(setAlarmState);
	default:
		Q_ASSERT(0);
		return Q_SUPER(setTimeState);
	}
}


/**
 * @see setHoursState()
 */
static QState setSecondsState(struct TimeSetter *me)
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
		lcd_set_cursor(1, 11);
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
		lcd_set_cursor(1, 11);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(setState);
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_HANDLED();
	}

	switch (me->settingWhich) {
	case SETTING_TIME:
		return Q_SUPER(setTimeState);
	case SETTING_ALARM:
		return Q_SUPER(setAlarmState);
	default:
		Q_ASSERT(0);
		return Q_SUPER(setTimeState);
	}
}
