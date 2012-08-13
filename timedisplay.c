#include "timedisplay.h"
#include "timekeeper.h"
#include "time.h"
#include "lcd.h"
#include "dclock.h"
#include <stdio.h>


Q_DEFINE_THIS_FILE;


static QState initial              (struct TimeDisplay *me);
static QState top                  (struct TimeDisplay *me);
static QState normal               (struct TimeDisplay *me);
static QState decimal              (struct TimeDisplay *me);
static QState setting              (struct TimeDisplay *me);


struct TimeDisplay timedisplay;


void timedisplay_ctor(void)
{
	QActive_ctor((QActive*)(&timedisplay), (QStateHandler)initial);
	timedisplay.ready = 0;
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
	}
	return Q_SUPER(QHsm_top);
}


static QState normal(struct TimeDisplay *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->mode = NORMAL_MODE;
		displayNormalTime(me, get_normal_time());
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		/* In normal mode, ignore decimal seconds. */
		return Q_HANDLED();
	case TICK_NORMAL_SIGNAL:
		displayNormalTime(me, it2nt(Q_PAR(me)));
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
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		displayDecimalTime(me, Q_PAR(me));
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
		switch (me->mode) {
		case NORMAL_MODE:
			return Q_TRAN(normal);
		case DECIMAL_MODE:
			return Q_TRAN(decimal);
		}
	}
	return Q_SUPER(top);
}
