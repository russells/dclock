#include "alarm.h"
#include "time.h"
#include "serial.h"
#include "timesetter.h"
#include "dclock.h"
#include "lcd.h"

#include <stdio.h>

Q_DEFINE_THIS_FILE;

struct Alarm alarm;


static QState initialState(struct Alarm *me);
static QState topState(struct Alarm *me);
static QState onState(struct Alarm *me);
static QState onDecimalState(struct Alarm *me);
static QState onNormalState(struct Alarm *me);
static QState offState(struct Alarm *me);
static QState ignoreButtonsState(struct Alarm *me);
static QState alarmedState(struct Alarm *me);
static QState alarmedOnState(struct Alarm *me);
static QState alarmedOffState(struct Alarm *me);


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
		}
		return Q_HANDLED();
	case DECIMAL_MODE_SIGNAL:
		if (me->armed) {
			return Q_TRAN(onDecimalState);
		}
		return Q_HANDLED();
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
		return Q_HANDLED();
	case ALARM_ON_SIGNAL:
		return Q_HANDLED();
	}
	return Q_SUPER(topState);
}


static QState onDecimalState(struct Alarm *me)
{
	uint32_t thetime;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("> alarm onDecimalState\r\n");
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		thetime = Q_PAR(me);
		if (thetime == me->decimalAlarmTime) {
			return Q_TRAN(alarmedOnState);
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
		SERIALSTR("> alarm onNormalState\r\n");
		return Q_HANDLED();
	case TICK_NORMAL_SIGNAL:
		thetimep = it2ntp(Q_PAR(me));
		if (thetimep->s == me->normalAlarmTime.s
		    && thetimep->m == me->normalAlarmTime.m
		    && thetimep->h == me->normalAlarmTime.h) {
			return Q_TRAN(alarmedOnState);
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
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		return Q_HANDLED();
	case ALARM_OFF_SIGNAL:
		return Q_HANDLED();
	}
	return Q_SUPER(topState);
}


static QState ignoreButtonsState(struct Alarm *me)
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
		QActive_arm((QActive*)me, 100);
		me->enterBrightness = lcd_get_brightness();
		switch (me->enterBrightness) {
		case 4:
			me->onBrightness = 4;
			me->offBrightness = 3;
			break;
		case 0:
			me->onBrightness = 2;
			me->offBrightness = 1;
			break;
		default:
			me->onBrightness = me->enterBrightness + 1;
			me->offBrightness = me->enterBrightness;
			break;
		}
		return Q_HANDLED();
	case Q_TIMEOUT_SIG:
		return Q_TRAN(topState);
	case BUTTON_SELECT_RELEASE_SIGNAL:
		return Q_TRAN(offState);
	case Q_EXIT_SIG:
		SERIALSTR("Alarm stopped\r\n");
		LCD_LINE2_ROM("                ");
		lcd_set_brightness(me->enterBrightness);
		return Q_HANDLED();
	}
	return Q_SUPER(ignoreButtonsState);
}


static QState alarmedOnState(struct Alarm *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		LCD_LINE2_ROM("Alarm!          ");
		QActive_arm((QActive*)me, 20);
		lcd_set_brightness(me->onBrightness);
		return Q_HANDLED();
	case Q_TIMEOUT_SIG:
		return Q_TRAN(alarmedOffState);
	}
	return Q_SUPER(alarmedState);
}


static QState alarmedOffState(struct Alarm *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		LCD_LINE2_ROM("                ");
		QActive_arm((QActive*)me, 20);
		lcd_set_brightness(me->offBrightness);
		return Q_HANDLED();
	case Q_TIMEOUT_SIG:
		return Q_TRAN(alarmedOnState);
	}
	return Q_SUPER(alarmedState);
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
