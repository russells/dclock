#include "alarm.h"
#include "time.h"
#include "serial.h"
#include "timesetter.h"
#include "timedisplay.h"
#include "dclock.h"
#include "lcd.h"
#include "bsp.h"

#include <stdio.h>

Q_DEFINE_THIS_FILE;

#include "time-arithmetic.h"

struct Alarm alarm;


static QState initialState(struct Alarm *me);
static QState topState(struct Alarm *me);
static QState onState(struct Alarm *me);
static QState onDecimalState(struct Alarm *me);
static QState onNormalState(struct Alarm *me);
static QState offState(struct Alarm *me);
static QState alarmButtonsState(struct Alarm *me);
static QState alarmedState(struct Alarm *me);
static QState alarmTurningOffNowState(struct Alarm *me);
static QState snoozeState(struct Alarm *me);
static QState snoozeNormalState(struct Alarm *me);
static QState snoozeDecimalState(struct Alarm *me);

#ifndef SNOOZE_MINUTES
#define SNOOZE_MINUTES 5
#endif
#ifndef MAX_SNOOZE_COUNT
#define MAX_SNOOZE_COUNT 4
#endif
#ifndef ALARM_SOUND_SECONDS
#define ALARM_SOUND_SECONDS 30
#endif
#define ALARM_SOUND_COUNT (37 * ALARM_SOUND_SECONDS) /* Approximate */


void get_alarm_times(struct Alarm *me, uint8_t *times)
{
	uint32_t sec;

	switch (get_time_mode()) {
	case DECIMAL_MODE:
		sec = me->decimalAlarmTime;
		Q_ASSERT( sec <= 99999 );
		times[2] = 0;	/* Force seconds to 0. */
		sec /= 100;
		times[1] = sec % 100;
		sec /= 100;
		times[0] = sec % 10;
		break;
	case NORMAL_MODE:
		Q_ASSERT( me->normalAlarmTime.h <= 23 );
		Q_ASSERT( me->normalAlarmTime.m <= 59 );
		Q_ASSERT( me->normalAlarmTime.s <= 59 );
		times[0] = me->normalAlarmTime.h;
		times[1] = me->normalAlarmTime.m;
		times[2] = 0;	/* Force seconds to 0. */
		break;
	default:
		Q_ASSERT( 0 );
	}
}


void set_alarm_times(struct Alarm *me, uint8_t *times)
{
	switch (get_time_mode()) {
	case DECIMAL_MODE:
		/* These three values are all unsigned and can all be zero, so
		   we don't need to check the lower bound. */
		Q_ASSERT( times[0] <= 9 );
		Q_ASSERT( times[1] <= 99 );
		Q_ASSERT( times[2] == 0 );
		me->decimalAlarmTime = (times[0] * 10000L)
			+ (times[1] * 100L) + times[2];
		me->normalAlarmTime = decimal_to_normal(me->decimalAlarmTime);
		SERIALSTR("alarm time: ");
		print_decimal_time(me->decimalAlarmTime);
		SERIALSTR(" (");
		print_normal_time(me->normalAlarmTime);
		SERIALSTR(")\r\n");
		break;
	case NORMAL_MODE:
		Q_ASSERT( times[0] <= 23 );
		Q_ASSERT( times[1] <= 59 );
		Q_ASSERT( times[2] == 0 );
		me->normalAlarmTime.h = times[0];
		me->normalAlarmTime.m = times[1];
		me->normalAlarmTime.s = times[2];
		me->normalAlarmTime.pad = 0;
		me->decimalAlarmTime = normal_to_decimal(me->normalAlarmTime);
		SERIALSTR("alarm time: ");
		print_normal_time(me->normalAlarmTime);
		SERIALSTR(" (");
		print_decimal_time(me->decimalAlarmTime);
		SERIALSTR(")\r\n");
		break;
	default:
		Q_ASSERT( 0 );
	}
	me->decimalSnoozeTime = me->decimalAlarmTime;
	me->normalSnoozeTime = me->normalAlarmTime;
}


void alarm_ctor(void)
{
	SERIALSTR("alarm_ctor()\r\n");
	serial_drain();
	QActive_ctor((QActive*)(&alarm), (QStateHandler)&initialState);

	alarm.decimalAlarmTime = 50000;
	alarm.normalAlarmTime.h =  12;
	alarm.normalAlarmTime.m = 0;
	alarm.normalAlarmTime.s = 0;
	alarm.normalAlarmTime.pad = 0;

	alarm.decimalSnoozeTime = 0;
	alarm.normalSnoozeTime.h =  0;
	alarm.normalSnoozeTime.m = 0;
	alarm.normalSnoozeTime.s = 0;
	alarm.normalSnoozeTime.pad = 0;
	alarm.snoozeCount = 0;

	alarm.ready = 0;
	alarm.armed = 0;
}


static QState initialState(struct Alarm *me)
{
	SERIALSTR("alarm initialState()\r\n");
	serial_drain();
	return Q_TRAN(offState);
}


static QState topState(struct Alarm *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("> alarm topState\r\n");
		me->ready = 73;
		return Q_HANDLED();
	case ALARM_OFF_SIGNAL:
		SERIALSTR("alarm ALARM_OFF_SIGNAL\r\n");
		return Q_TRAN(offState);
	case ALARM_ON_SIGNAL:
		SERIALSTR("alarm ALARM_ON_SIGNAL\r\n");
		switch (get_time_mode()) {
		case NORMAL_MODE:
			return Q_TRAN(onNormalState);
		case DECIMAL_MODE:
			return Q_TRAN(onDecimalState);
		default:
			Q_ASSERT( 0 );
			return Q_HANDLED();
		}
	case NORMAL_MODE_SIGNAL:
		if (me->armed) {
			return Q_TRAN(onNormalState);
		} else {
			return Q_TRAN(offState);
		}
	case DECIMAL_MODE_SIGNAL:
		if (me->armed) {
			return Q_TRAN(onDecimalState);
		} else {
			return Q_TRAN(offState);
		}
	case BUTTON_SELECT_PRESS_SIGNAL:
	case BUTTON_SELECT_LONG_PRESS_SIGNAL:
	case BUTTON_SELECT_REPEAT_SIGNAL:
	case BUTTON_SELECT_RELEASE_SIGNAL:
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_LONG_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
	case BUTTON_UP_RELEASE_SIGNAL:
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_LONG_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
	case BUTTON_DOWN_RELEASE_SIGNAL:
		post(&timesetter, Q_SIG(me), 0);
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}


static QState onState(struct Alarm *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("> alarm onState\r\n");
		me->armed = 73;
		me->decimalSnoozeTime = me->decimalAlarmTime;
		me->normalSnoozeTime = me->normalAlarmTime;
		me->snoozeCount = 0;
		post((&timedisplay), ALARM_ON_SIGNAL, 0);
		return Q_HANDLED();
	case ALARM_ON_SIGNAL:
		return Q_HANDLED();
	case Q_EXIT_SIG:
		return Q_HANDLED();
	}
	return Q_SUPER(topState);
}


static QState onDecimalState(struct Alarm *me)
{
	uint32_t thetime;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("> alarm onDecimalState ");
		print_decimal_time(me->decimalAlarmTime);
		SERIALSTR("\r\n");
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		thetime = Q_PAR(me);
		if (thetime == me->decimalAlarmTime) {
			return Q_TRAN(alarmedState);
		} else {
			return Q_HANDLED();
		}
	}
	return Q_SUPER(onState);
}


static QState onNormalState(struct Alarm *me)
{
	struct NormalTime *thetimep;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("> alarm onNormalState ");
		print_normal_time(me->normalAlarmTime);
		SERIALSTR("\r\n");
		return Q_HANDLED();
	case TICK_NORMAL_SIGNAL:
		thetimep = it2ntp(Q_PAR(me));
		if (thetimep->s == me->normalAlarmTime.s
		    && thetimep->m == me->normalAlarmTime.m
		    && thetimep->h == me->normalAlarmTime.h) {
			return Q_TRAN(alarmedState);
		} else {
			return Q_HANDLED();
		}
	}
	return Q_SUPER(onState);
}


static QState offState(struct Alarm *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->armed = 0;
		post((&timedisplay), ALARM_OFF_SIGNAL, 0);
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		return Q_HANDLED();
	case ALARM_OFF_SIGNAL:
		return Q_HANDLED();
	}
	return Q_SUPER(topState);
}


static QState alarmButtonsState(struct Alarm *me)
{
	switch (Q_SIG(me)) {
	case BUTTON_SELECT_PRESS_SIGNAL:
	case BUTTON_SELECT_LONG_PRESS_SIGNAL:
	case BUTTON_SELECT_REPEAT_SIGNAL:
	case BUTTON_SELECT_RELEASE_SIGNAL:
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_LONG_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
	case BUTTON_UP_RELEASE_SIGNAL:
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_LONG_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
	case BUTTON_DOWN_RELEASE_SIGNAL:
		return Q_HANDLED();
	}
	return Q_SUPER(&topState);
}


static QState alarmedState(struct Alarm *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("Alarm\r\n");
		QActive_arm_sig((QActive*)me, ALARM_SOUND_COUNT,
				ALARM_RUNNING_TIMEOUT_SIGNAL);
		me->turnOff = 0;
		me->alarmSoundCount = 0;
		post((&timedisplay), ALARM_RUNNING_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_SELECT_PRESS_SIGNAL:
		/* We want the alarm to stop sounding as soon as a button is
		   pressed, but at that stage we haven't decided whether to
		   snooze or turn off. */
		post((&timedisplay), ALARM_STOPPED_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_SELECT_LONG_PRESS_SIGNAL:
		return Q_HANDLED();
	case BUTTON_SELECT_REPEAT_SIGNAL:
		/* The way to turn off the alarm (rather than snoozing) is to
		   hold down the select button for a long press time and one
		   repeat. */
		/** A long press and one repeat is 48 ticks, at 37 Hz that's
		    about 1.3 seconds.  That's a convenient period, only
		    because it means that we only have to handle the first
		    repeat signal.  If a different period is required, we would
		    have to use another timer signal, count repeats, or use the
		    long press signal (which gives 30 ticks.) */
		me->turnOff = 73;
		post((&timedisplay), ALARM_STOPPED_SIGNAL, 0);
		return Q_TRAN(alarmTurningOffNowState);
	case BUTTON_UP_RELEASE_SIGNAL:
	case BUTTON_DOWN_RELEASE_SIGNAL:
	case BUTTON_SELECT_RELEASE_SIGNAL:
		/* If the alarm is snoozed by the user, start counting snooze
		   periods again. */
		me->snoozeCount = 0;
		post(me, ALARM_STOPPED_SIGNAL, 0);
		return Q_HANDLED();

	case ALARM_RUNNING_TIMEOUT_SIGNAL:
		post((&timedisplay), ALARM_STOPPED_SIGNAL, 0);
		post(me, ALARM_STOPPED_SIGNAL, 0);
		return Q_HANDLED();

	case ALARM_STOPPED_SIGNAL:
		if (me->turnOff) {
			/* me->turnOff will only be true in response to a long
			   press of the select button. */
			SERIALSTR("(me->turnOff)\r\n");
			return Q_TRAN(offState);
		} else if (me->snoozeCount >= MAX_SNOOZE_COUNT) {
			SERIALSTR("(me->snoozeCount==");
			serial_send_int(me->snoozeCount);
			SERIALSTR(")\r\n");
			return Q_TRAN(offState);
		} else {
			switch (get_time_mode()) {
			case NORMAL_MODE:
				return Q_TRAN(snoozeNormalState);
			case DECIMAL_MODE:
				return Q_TRAN(snoozeDecimalState);
			default:
				Q_ASSERT( 0 );
			}
		}
		/* There needs to be a return or Q_ASSERT() in every branch of
		   the ifs before we get here in this switch case. */
	case Q_EXIT_SIG:
		SERIALSTR("Alarm stopped\r\n");
		return Q_HANDLED();
	}
	return Q_SUPER(alarmButtonsState);
}


static QState alarmTurningOffNowState(struct Alarm *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		QActive_arm_sig((QActive*)me, 1, ALARM_BEEP_TIMEOUT_SIGNAL);
		BSP_buzzer_on();
		return Q_HANDLED();
	case ALARM_BEEP_TIMEOUT_SIGNAL:
		/* Don't transition back to the parent on our timeout signal,
		   because that will result in coming back here at the next
		   button repeat, and making the buzzer make short beeps
		   continuously.  Instead, we turn the buzzer off, and let the
		   parent state handle all the events it wants. */
		BSP_buzzer_off();
		return Q_HANDLED();
	case BUTTON_SELECT_REPEAT_SIGNAL:
		/* We have to ignore this signal, as well, since the parent
		   state responds to it with a transition here, which would
		   mean an exit action and an entry action for this state, and
		   continous short beeps. */
		return Q_HANDLED();
	case ALARM_RUNNING_TIMEOUT_SIGNAL:
		/* Our parent state arms the timer with this signal.  It could
		   be in the queue when we get here, and we don't want it to be
		   handled, so ignore it.  If we've got here, the user has held
		   down the select butotn, and we'll leave when that button is
		   released, so we don't need the parent's timeout signal any
		   more. */
		return Q_HANDLED();
	case Q_EXIT_SIG:
		/* If the user lifts the select button before we've done the
		   timeout in this state, the buzzer will still be on.  So
		   ensure the buzzer is turned off before we leave here. */
		BSP_buzzer_off();
		return Q_HANDLED();
	}
	return Q_SUPER(alarmedState);
}


static void inc_snooze_times(struct Alarm *me)
{
	for (uint8_t i=0; i<SNOOZE_MINUTES; i++) {
		me->normalSnoozeTime.m =
			inc_normal_minutes(me->normalSnoozeTime.m);
		if (me->normalSnoozeTime.m == 0) {
			me->normalSnoozeTime.h =
				inc_normal_hours(me->normalSnoozeTime.h);
		}
	}

	me->decimalSnoozeTime += 100 * SNOOZE_MINUTES;
	if (me->decimalSnoozeTime > 99999L) {
		me->decimalSnoozeTime -= 100000L;
	}
}


static QState snoozeState(struct Alarm *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->snoozeCount ++;
		inc_snooze_times(me);
		SERIALSTR("> snoozeState ");
		switch (get_time_mode()) {
		case NORMAL_MODE:
			print_normal_time(me->normalSnoozeTime);
			break;
		case DECIMAL_MODE:
			print_decimal_time(me->decimalSnoozeTime);
			break;
		default:
			Q_ASSERT( 0 );
		}
		SERIALSTR(" snoozeCount==");
		serial_send_int(me->snoozeCount);
		SERIALSTR("\r\n");
		display_status_on(DSTAT_SNOOZE);
		return Q_HANDLED();
	case Q_EXIT_SIG:
		SERIALSTR("< snoozeState\r\n");
		display_status_off(DSTAT_SNOOZE);
		return Q_HANDLED();
	}
	return Q_SUPER(topState);
}


static QState snoozeNormalState(struct Alarm *me)
{
	struct NormalTime *thetimep;

	switch (Q_SIG(me)) {
	case TICK_NORMAL_SIGNAL:
		thetimep = it2ntp(Q_PAR(me));
		if (thetimep->s == me->normalSnoozeTime.s
		    && thetimep->m == me->normalSnoozeTime.m
		    && thetimep->h == me->normalSnoozeTime.h) {
			return Q_TRAN(alarmedState);
		} else {
			return Q_HANDLED();
		}
	}
	return Q_SUPER(snoozeState);
}


static QState snoozeDecimalState(struct Alarm *me)
{
	uint32_t thetime;

	switch (Q_SIG(me)) {
	case TICK_DECIMAL_SIGNAL:
		thetime = Q_PAR(me);
		if (thetime == me->decimalSnoozeTime) {
			return Q_TRAN(alarmedState);
		} else {
			return Q_HANDLED();
		}
	}
	return Q_SUPER(snoozeState);
}


uint8_t get_alarm_state(struct Alarm *me)
{
	return me->armed;
}


void set_alarm_state(struct Alarm *me, uint8_t onoff)
{
	if (onoff) {
		post((QActive*)me, ALARM_ON_SIGNAL, 0);
	} else {
		post((QActive*)me, ALARM_OFF_SIGNAL, 0);
	}
}
