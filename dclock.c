/**
 * @file
 */

#include "dclock.h"
#include "buttons.h"
#include "lcd.h"
#include "serial.h"
#include "version.h"
#include "bsp.h"
#include "toggle-pin.h"
#include <string.h>
#include <stdio.h>


/** The only active DClock. */
struct DClock dclock;


Q_DEFINE_THIS_FILE;

static QState dclockInitial          (struct DClock *me);
static QState dclockState            (struct DClock *me);
static QState dclockSetState         (struct DClock *me);
static QState dclockSetUpdatingState (struct DClock *me);
static QState dclockSetHoursState    (struct DClock *me);
static QState dclockSetMinutesState  (struct DClock *me);
static QState dclockSetSecondsState  (struct DClock *me);


static QEvent dclockQueue[4];
static QEvent buttonsQueue[4];

QActiveCB const Q_ROM Q_ROM_VAR QF_active[] = {
	{ (QActive *)0              , (QEvent *)0      , 0                        },
	{ (QActive *)(&buttons)     , buttonsQueue     , Q_DIM(buttonsQueue)      },
	{ (QActive *)(&dclock)      , dclockQueue      , Q_DIM(dclockQueue)       },
};
/* If QF_MAX_ACTIVE is incorrectly defined, the compiler says something like:
   lapclock.c:68: error: size of array ‘Q_assert_compile’ is negative
 */
Q_ASSERT_COMPILE(QF_MAX_ACTIVE == Q_DIM(QF_active) - 1);


int main(int argc, char **argv)
{
 startmain:
	TOGGLE_BEGIN();
	BSP_startmain();
	serial_init();
	serial_send_rom(startup_message);
	lcd_init();
	dclock_ctor();
	buttons_ctor();
	BSP_init(); /* initialize the Board Support Package */

	QF_run();

	goto startmain;
}

void dclock_ctor(void)
{
	QActive_ctor((QActive *)(&dclock), (QStateHandler)&dclockInitial);
	/* Using this value as the initial time allows us to test the code that
	   moves the time across the display each minute. */
	dclock.dseconds = 11745;
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


static QState dclockState(struct DClock *me)
{
	uint8_t d32counter;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		lcd_clear();
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
			fff(me);
			QActive_post((QActive*)me, TICK_DECIMAL_SIGNAL, 0);
		}
		return Q_HANDLED();

	case TICK_DECIMAL_SIGNAL: {
		char line[17];
		uint32_t sec;
		uint8_t h, m, s;
		uint8_t spaces;

		inc_dseconds(me);

		//lcd_clear();
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
		return Q_HANDLED();
	}

	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(dclockSetHoursState);
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		BSP_inc_brightness();
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		BSP_dec_brightness();
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}


static void displaySettingTime(struct DClock *me)
{
	char line[17];
	snprintf(line, 17, "%02u.%02u.%02u        ", me->setHours,
		 me->setMinutes, me->setSeconds);
	Q_ASSERT( strlen(line) <= 16 );
	lcd_line1(line);
}


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
		displaySettingTime(me);
		fff(me);
		QActive_post((QActive*)me, UPDATE_TIME_SET_CURSOR_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_SELECT_LONG_PRESS_SIGNAL:
	case BUTTON_SELECT_REPEAT_SIGNAL:
	case BUTTON_UP_LONG_PRESS_SIGNAL:
	case BUTTON_DOWN_LONG_PRESS_SIGNAL:
		/* Make sure we ignore these button signals, as parent states
		   may do things with them that will interfere with time
		   setting. */
		return Q_HANDLED();
	case Q_EXIT_SIG:
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
		return Q_HANDLED();
	}
	return Q_SUPER(dclockState);
}


static QState dclockSetUpdatingState(struct DClock *me)
{
	switch (Q_SIG(me)) {
	case UPDATE_TIME_SET_SIGNAL:
		/* When any of the child states update the time variables, they
		   generate this signal.  We want to note that the time has
		   changed, and then let our parent state change the time on
		   the display. */
		me->timeSetChanged = 73; /* Need a true value?  Why not 73? */
		return Q_SUPER(dclockSetState);
	}
	return Q_SUPER(dclockSetState);
}


static QState dclockSetHoursState(struct DClock *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		LCD_LINE2_ROM("Setting hours   ");
		lcd_set_cursor(0, 1);
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		Q_ASSERT( me->setHours <= 9 );
		if (9 == me->setHours) {
			me->setHours = 0;
		} else {
			me->setHours++;
		}
		fff(me);
		QActive_post((QActive*)me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		Q_ASSERT( me->setHours <= 9 );
		if (0 == me->setHours) {
			me->setHours = 9;
		} else {
			me->setHours--;
		}
		fff(me);
		QActive_post((QActive*)me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case UPDATE_TIME_SET_CURSOR_SIGNAL:
		lcd_set_cursor(0, 1);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(dclockSetMinutesState);
	}
	return Q_SUPER(dclockSetUpdatingState);
}


static QState dclockSetMinutesState(struct DClock *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		LCD_LINE2_ROM("Setting minutes ");
		lcd_set_cursor(0, 4);
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		Q_ASSERT( me->setMinutes <= 99 );
		if (99 == me->setMinutes) {
			me->setMinutes = 0;
		} else {
			me->setMinutes++;
		}
		fff(me);
		QActive_post((QActive*)me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		Q_ASSERT( me->setMinutes <= 99 );
		if (0 == me->setMinutes) {
			me->setMinutes = 99;
		} else {
			me->setMinutes--;
		}
		fff(me);
		QActive_post((QActive*)me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case UPDATE_TIME_SET_CURSOR_SIGNAL:
		lcd_set_cursor(0, 4);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(dclockSetSecondsState);
	}
	return Q_SUPER(dclockSetUpdatingState);
}


static QState dclockSetSecondsState(struct DClock *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		LCD_LINE2_ROM("Setting seconds ");
		lcd_set_cursor(0, 7);
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
	case BUTTON_UP_REPEAT_SIGNAL:
		Q_ASSERT( me->setSeconds <= 99 );
		if (99 == me->setSeconds) {
			me->setSeconds = 0;
		} else {
			me->setSeconds++;
		}
		fff(me);
		QActive_post((QActive*)me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
	case BUTTON_DOWN_REPEAT_SIGNAL:
		Q_ASSERT( me->setSeconds <= 99 );
		if (0 == me->setSeconds) {
			me->setSeconds = 99;
		} else {
			me->setSeconds--;
		}
		fff(me);
		QActive_post((QActive*)me, UPDATE_TIME_SET_SIGNAL, 0);
		return Q_HANDLED();
	case UPDATE_TIME_SET_CURSOR_SIGNAL:
		lcd_set_cursor(0, 7);
		return Q_HANDLED();
	case BUTTON_SELECT_PRESS_SIGNAL:
		return Q_TRAN(dclockState);
	}
	return Q_SUPER(dclockSetUpdatingState);
}
