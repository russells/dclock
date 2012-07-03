/**
 * @file
 */

#include "dclock.h"
#include "buttons.h"
#include "display.h"
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
	display_init();
	dclock_ctor();
	buttons_ctor();
	BSP_init(); /* initialize the Board Support Package */

	QF_run();

	goto startmain;
}

void dclock_ctor(void)
{
	QActive_ctor((QActive *)(&dclock), (QStateHandler)&dclockInitial);
	dclock.dseconds = 12345;
}


static QState dclockInitial(struct DClock *me)
{
	return Q_TRAN(&dclockState);
}


static QState dclockState(struct DClock *me)
{
	uint8_t d32counter;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		display_clear();
		DISPLAY_LINE1_ROM("    A clock!");
		DISPLAY_LINE2_ROM(V);
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
			QActive_post((QActive*)me, TICK_DECIMAL_SIGNAL, 0);
		}
		return Q_HANDLED();

	case TICK_DECIMAL_SIGNAL: {
		char line[16];
		uint32_t sec;
		uint8_t h, m, s;

		me->dseconds++;
		if (me->dseconds >= 100000) {
			me->dseconds = 0;
		}

		display_clear();
		sec = me->dseconds;
		s = sec % 100;
		sec /= 100;
		m = sec % 100;
		sec /= 100;
		h = sec % 100;
		snprintf(line, 16, "%02u.%02u.%02u", h, m, s);
		display_line1(line);
		serial_send(line);
		SERIALSTR("\r");
		return Q_HANDLED();
	}

	case BUTTON_SELECT_PRESS_SIGNAL:
		SERIALSTR("\r\nSelect\r\n");
		DISPLAY_LINE2_ROM("Select       ");
		return Q_HANDLED();
	case BUTTON_UP_PRESS_SIGNAL:
		SERIALSTR("\r\nUp\r\n");
		DISPLAY_LINE2_ROM("Up           ");
		BSP_inc_brightness();
		return Q_HANDLED();
	case BUTTON_DOWN_PRESS_SIGNAL:
		SERIALSTR("\r\nDown\r\n");
		DISPLAY_LINE2_ROM("Down         ");
		BSP_dec_brightness();
		return Q_HANDLED();

	case BUTTON_SELECT_LONG_PRESS_SIGNAL:
		SERIALSTR("\r\nSelect long\r\n");
		DISPLAY_LINE2_ROM("Select long  ");
		return Q_HANDLED();
	case BUTTON_UP_LONG_PRESS_SIGNAL:
		SERIALSTR("\r\nUp long\r\n");
		DISPLAY_LINE2_ROM("Up long      ");
		return Q_HANDLED();
	case BUTTON_DOWN_LONG_PRESS_SIGNAL:
		SERIALSTR("\r\nDown long\r\n");
		DISPLAY_LINE2_ROM("Down long    ");
		return Q_HANDLED();

	case BUTTON_SELECT_REPEAT_SIGNAL:
		SERIALSTR("\r\nSelect repeat\r\n");
		DISPLAY_LINE2_ROM("Select repeat");
		return Q_HANDLED();
	case BUTTON_UP_REPEAT_SIGNAL:
		SERIALSTR("\r\nUp repeat\r\n");
		DISPLAY_LINE2_ROM("Up repeat    ");
		BSP_inc_brightness();
		return Q_HANDLED();
	case BUTTON_DOWN_REPEAT_SIGNAL:
		SERIALSTR("\r\nDown repeat\r\n");
		DISPLAY_LINE2_ROM("Down repeat  ");
		BSP_dec_brightness();
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}
