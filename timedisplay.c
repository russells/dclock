#include "timedisplay.h"
#include "timekeeper.h"
#include "time.h"
#include "alarm.h"
#include "lcd.h"
#include "dclock.h"
#include "serial.h"
#include "bsp.h"
#include <stdio.h>
#include <stdlib.h>


Q_DEFINE_THIS_FILE;


static QState initial              (struct TimeDisplay *me);
static QState top                  (struct TimeDisplay *me);
static QState normal               (struct TimeDisplay *me);
static QState decimal              (struct TimeDisplay *me);
static QState setting              (struct TimeDisplay *me);
static QState alarming             (struct TimeDisplay *me);
static QState alarming1            (struct TimeDisplay *me);
static QState alarming2            (struct TimeDisplay *me);


static void lights_on(uint8_t num);
static void lights_off(void);


struct TimeDisplay timedisplay;


void timedisplay_ctor(void)
{
	QActive_ctor((QActive*)(&timedisplay), (QStateHandler)initial);
	timedisplay.ready = 0;
	timedisplay.statuses = 0;
}


static QState initial(struct TimeDisplay *me)
{
	lcd_clear();
	LCD_LINE1_ROM("    A clock!");
	LCD_LINE2_ROM(V);
	return Q_TRAN(decimal);	/* FIXME wait for a signal in top() before
				   doing the transition to the correct
				   state. */
}


static void displayDecimalTime(struct TimeDisplay *me, uint32_t dseconds)
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


static void displayNormalTime(struct TimeDisplay *me, struct NormalTime nt)
{
	char line[17];
	uint8_t spaces;

	spaces = nt.m % 9;
	for (uint8_t i=0; i<spaces; i++) {
		line[i] = ' ';
	}
	snprintf(line+spaces, 17-spaces, "%02u:%02u:%02u%s",
		 nt.h, nt.m, nt.s, "        ");
	Q_ASSERT( line[16] == '\0' );
	lcd_line1(line);
}


/* static void displayAlarmTime(struct TimeDisplay *me) */
/* { */
/* 	char buf[17]; */
/* 	uint8_t alarmtimes[3]; */
/* 	uint8_t nowtimes[3]; */
/* 	uint8_t i; */
/* 	uint8_t spaces; */
/* 	char sep; */

/* 	get_alarm_times(&alarm, alarmtimes); */
/* 	get_times(nowtimes); */
/* 	spaces = nowtimes[1] % 6; */
/* 	if (me->mode == DECIMAL_MODE) { */
/* 		sep = '.'; */
/* 	} else { */
/* 		sep = ':'; */
/* 	} */

/* 	for (i=0; i<spaces; i++) { */
/* 		buf[i] = ' '; */
/* 	} */
/* 	snprintf(buf+spaces, 17-spaces, "Alarm:%02d%c%02d", */
/* 		 alarmtimes[0], sep, alarmtimes[1]); */
/* 	LCD_LINE2_ROM("Alarm on        "); */
/* 	i = spaces + 11; */
/* 	while (i<16) { */
/* 		buf[i] = ' '; */
/* 		i++; */
/* 	} */
/* 	buf[i] = '\0'; */
/* 	Q_ASSERT( buf[16] == '\0' ); */
/* 	lcd_line2(buf); */
/* } */


/**
 * Display a (possibly moving) string on the bottom line.
 *
 * @param s the string
 * @param n the length of the string.  Saves calling strlen() here.
 * @param move -1 = don't move, 0 = move with hours, 1 = move with minutes, 2 =
 * move with seconds.
 */
static void displayMovingBottomString(const char *s, uint8_t n, int8_t move)
{
	uint8_t times[3];
	uint8_t spaces;
	char buf[17];
	uint8_t i;

	Q_ASSERT( n <= 16 );
	Q_ASSERT( s[n] == '\0' );
	if (-1 != move) {
		Q_ASSERT( move >= 0 );
		Q_ASSERT( move <= 2);
		get_times(times);
		spaces = times[move] % (17 - n);
		i = 0;
		while (i < spaces) {
			buf[i] = ' ';
			i++;
		}
	} else {
		spaces = 0;
		i = 0;
	}
	snprintf(buf+spaces, 17-spaces, s);
	i = spaces + n;
	while (i<16) {
		buf[i] = ' ';
		i++;
	}
	buf[i] = '\0';
	Q_ASSERT( buf[16] == '\0' );
	lcd_line2(buf);
}


static void displayStatus(struct TimeDisplay *me)
{
	/* Step through the list of statuses from highest to lowest priority,
	   showing the first one that's active. */
	if (me->statuses & DSTAT_ALARM_RUNNING) {
		displayMovingBottomString("** ALARM **", 11, 2);
	} else if (me->statuses & DSTAT_SNOOZE) {
		displayMovingBottomString("Zzz..", 5, 2);
	} else if (me->statuses & DSTAT_ALARM) {
		displayMovingBottomString("A", 1, 1);
	} else {
		LCD_LINE2_ROM("                ");
	}

}


void display_status_on(enum DisplayStatus ds)
{
	timedisplay.statuses |= ds;
	displayStatus(&timedisplay);
}


void display_status_off(enum DisplayStatus ds)
{
	timedisplay.statuses &= ~ds;
	displayStatus(&timedisplay);
}


static QStateHandler getModeState(struct TimeDisplay *me)
{
	switch (me->mode) {
	default:
		Q_ASSERT( 0 );
		/*FALLTHROUGH*/
	case NORMAL_MODE:
		return (QStateHandler)(&normal);
	case DECIMAL_MODE:
		return (QStateHandler)(&decimal);
	}
}


static QState top(struct TimeDisplay *me)
{
	switch(Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->ready = 73;
		me->mode = 0;
		return Q_HANDLED();
	case NORMAL_MODE_SIGNAL:
		return Q_TRAN(normal);
	case DECIMAL_MODE_SIGNAL:
		return Q_TRAN(decimal);
	case SETTING_TIME_SIGNAL:
		return Q_TRAN(setting);
	case ALARM_ON_SIGNAL:
		display_status_on(DSTAT_ALARM);
		return Q_TRAN(getModeState(me));
	case ALARM_OFF_SIGNAL:
		display_status_off(DSTAT_ALARM);
		return Q_TRAN(getModeState(me));
	case ALARM_RUNNING_SIGNAL:
		SERIALSTR("timedisplay ALARM_RUNNING_SIGNAL\r\n");
		return Q_TRAN(alarming1);
	}
	return Q_SUPER(QHsm_top);
}


static QState normal(struct TimeDisplay *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->mode = NORMAL_MODE;
		displayNormalTime(me, get_normal_time());
		displayStatus(me);
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		/* In normal mode, ignore decimal seconds. */
		return Q_HANDLED();
	case TICK_NORMAL_SIGNAL:
		displayNormalTime(me, it2nt(Q_PAR(me)));
		displayStatus(me);
		return Q_HANDLED();
	}
	return Q_SUPER(top);
}


static QState decimal(struct TimeDisplay *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->mode = DECIMAL_MODE;
		/* Display the time as we enter this state so we don't end up
		   having to wait for the next second. */
		displayDecimalTime(me, get_decimal_time());
		displayStatus(me);
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		displayDecimalTime(me, Q_PAR(me));
		displayStatus(me);
		return Q_HANDLED();
	case TICK_NORMAL_SIGNAL:
		/* In decimal mode, ignore normal seconds. */
		return Q_HANDLED();
	}
	return Q_SUPER(top);
}


static QState setting(struct TimeDisplay *me)
{
	switch (Q_SIG(me)) {
	case TICK_DECIMAL_SIGNAL:
	case TICK_NORMAL_SIGNAL:
		return Q_HANDLED();
	case SETTING_TIME_FINISHED_SIGNAL:
		return Q_TRAN(getModeState(me));
	}
	return Q_SUPER(top);
}


static QState alarming(struct TimeDisplay *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->preAlarmBrightness = lcd_get_brightness();
		switch (me->preAlarmBrightness) {
		case 4:
			me->onBrightness = 4;
			me->offBrightness = 3;
			break;
		case 0:
			me->onBrightness = 2;
			me->offBrightness = 1;
			break;
		default:
			me->onBrightness = me->preAlarmBrightness + 1;
			me->offBrightness = me->preAlarmBrightness;
			break;
		}
		display_status_on(DSTAT_ALARM_RUNNING);
		me->volume = 0;
		return Q_HANDLED();
	case ALARM_STOPPED_SIGNAL:
		SERIALSTR("timedisplay ALARM_STOPPED_SIGNAL\r\n");
		return Q_TRAN(getModeState(me));
	case Q_EXIT_SIG:
		lcd_set_brightness(me->preAlarmBrightness);
		display_status_off(DSTAT_ALARM_RUNNING);
		lights_off();
		BSP_buzzer_off();
		return Q_HANDLED();
	}
	return Q_SUPER(getModeState(me));
}


#define ON_OFF_TIME 20


static QState alarming1(struct TimeDisplay *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		//SERIALSTR("<1>");
		QActive_arm((QActive*)me, ON_OFF_TIME);
		lcd_set_brightness(me->onBrightness);
		lights_off();
		BSP_buzzer_off();
		return Q_HANDLED();
	case Q_TIMEOUT_SIG:
		return Q_TRAN(alarming2);
	}
	return Q_SUPER(alarming);
}


static QState alarming2(struct TimeDisplay *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		//SERIALSTR("<2>");
		QActive_arm((QActive*)me, 1);
		lcd_set_brightness(me->offBrightness);
		lights_on(0);
		/* Slowly increase the volume. */
		if (! me->volume) {
			me->volume = 1;
		} else if (me->volume < 16) {
			me->volume <<= 1;
		} else if (me->volume < 128) {
			me->volume = (me->volume / 2) * 3;
		} else {
			me->volume = 255;
		}
		BSP_buzzer_on(me->volume);
		me->onOffTime = 0;
		return Q_HANDLED();
	case Q_TIMEOUT_SIG:
		me->onOffTime ++;
		if (me->onOffTime >= ON_OFF_TIME) {
			return Q_TRAN(alarming1);
		} else {
			QActive_arm((QActive*)me, 1);
			lights_on(me->onOffTime);
			return Q_HANDLED();
		}
	}
	return Q_SUPER(alarming);
}


static uint8_t rgb[6];


static void lights_on(uint8_t num)
{
	uint16_t colorselector;

	/* Give each LED a 25% chance of being turned off in this cycle.
	   Without something like this, the LEDs combine to give a largely
	   white colour. */
	colorselector = (uint16_t) random();
	if (colorselector & 0b000000000011) {
		rgb[0] = 0;
	} else {
		rgb[0] = (uint8_t) random();
	}
	if (colorselector & 0b000000001100) {
		rgb[1] = 0;
	} else {
		rgb[1] = (uint8_t) random();
	}
	if (colorselector & 0b000000110000) {
		rgb[2] = 0;
	} else {
		rgb[2] = (uint8_t) random();
	}
	if (colorselector & 0b000011000000) {
		rgb[3] = 0;
	} else {
		rgb[3] = (uint8_t) random();
	}
	if (colorselector & 0b001100000000) {
		rgb[4] = 0;
	} else {
		rgb[4] = (uint8_t) random();
	}
	if (colorselector & 0b110000000000) {
		rgb[5] = 0;
	} else {
		rgb[5] = (uint8_t) random();
	}
	/* Every second time, halve the brightness to make the LEDs flash
	   more. */
	if (num & 0x1) {
		for (uint8_t i=0; i<6; i++) {
			if (rgb[i] < 128) {
				rgb[i] >>= 1;
			}
		}
	}
	BSP_leds(rgb);
}


static void lights_off(void)
{
	rgb[0] = 0;
	rgb[1] = 0;
	rgb[2] = 0;
	rgb[3] = 0;
	rgb[4] = 0;
	rgb[5] = 0;
	BSP_leds(rgb);
}
