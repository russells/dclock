#include "timedisplay.h"
#include "timekeeper.h"
#include "lcd.h"
#include "dclock.h"
#include <stdio.h>


static QState initial              (struct TimeDisplay *me);
static QState top                  (struct TimeDisplay *me);
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
	return Q_TRAN(top);
}


static void displayTime(struct TimeDisplay *me, uint32_t dseconds)
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


static QState top(struct TimeDisplay *me)
{
	switch(Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->ready = 73;
		return Q_HANDLED();
	case TICK_DECIMAL_SIGNAL:
		displayTime(me, Q_PAR(me));
		return Q_HANDLED();
	case SETTING_TIME_SIGNAL:
		return Q_TRAN(setting);
	}
	return Q_SUPER(QHsm_top);
}


static QState setting(struct TimeDisplay *me)
{
	switch (Q_SIG(me)) {
	case TICK_DECIMAL_SIGNAL:
		return Q_HANDLED();
	case SETTING_TIME_FINISHED_SIGNAL:
		/* Display the time as we leave this state so we don't end up
		   having to wait for the next second. */
		displayTime(me, get_dseconds(&timekeeper));
		return Q_TRAN(top);
	}
	return Q_SUPER(top);
}
