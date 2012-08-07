#include "alarm.h"
#include "time-utils.h"
#include "serial.h"
#include "dclock.h"
#include "lcd.h"

Q_DEFINE_THIS_FILE;

struct Alarm alarm;


static QState initialState(struct Alarm *me);
static QState topState(struct Alarm *me);
static QState onState(struct Alarm *me);
static QState offState(struct Alarm *me);
static QState ignoreButtonsState(struct Alarm *me);
static QState alarmedState(struct Alarm *me);
static QState alarmedOnState(struct Alarm *me);
static QState alarmedOffState(struct Alarm *me);


uint32_t get_alarm_dseconds(struct Alarm *me)
{
	return me->alarmTime;
}


void set_alarm_dseconds(struct Alarm *me, uint32_t dseconds)
{
	me->alarmTime = dseconds;
}


void get_alarm_dtimes(struct Alarm *me, uint8_t *ph, uint8_t *pm, uint8_t *ps)
{
	uint32_t sec = me->alarmTime;
	Q_ASSERT( sec <= 99999 );
	*ps = sec % 100;
	sec /= 100;
	*pm = sec % 100;
	sec /= 100;
	*ph = sec % 10;
}


void set_alarm_dtimes(struct Alarm *me, uint8_t h, uint8_t m, uint8_t s)
{
	/* These three values are all unsigned and can all be zero, so we don't
	   need to check the lower bound. */
	Q_ASSERT( h <= 9 );
	Q_ASSERT( m <= 99 );
	Q_ASSERT( s <= 99 );
	set_alarm_dseconds(me, (h * 10000L) + (m * 100L) + s);
}


void alarm_ctor(void)
{
	SERIALSTR("alarm_ctor()\r\n");
	serial_drain();
	QActive_ctor((QActive*)(&alarm), (QStateHandler)&initialState);
	alarm.alarmTime = 11749;
	alarm.ready = 0;
}


static QState initialState(struct Alarm *me)
{
	SERIALSTR("alarm initialState()\r\n");
	serial_drain();
	return Q_TRAN(onState);
}


static QState topState(struct Alarm *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("alarm topState Q_ENTRY\r\n");
		serial_drain();
		me->ready = 73;
		return Q_HANDLED();
	case ALARM_OFF_SIGNAL:
		return Q_TRAN(offState);
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
		post(&dclock, Q_SIG(me), 0);
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}


static QState onState(struct Alarm *me)
{
	uint32_t thetime;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->armed = 73;
		return Q_HANDLED();
	case ALARM_OFF_SIGNAL:
		return Q_TRAN(offState);
	case ALARM_ON_SIGNAL:
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		thetime = Q_PAR(me);
		if (thetime == me->alarmTime) {
			return Q_TRAN(alarmedOnState);
		} else {
			return Q_HANDLED();
		}
	}
	return Q_SUPER(topState);
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
	case ALARM_ON_SIGNAL:
		return Q_TRAN(onState);
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
		return Q_TRAN(topState);
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
