/**
 * @file
 */

#include "dclock.h"
#include "buttons.h"
#include "display.h"
#include "bsp.h"
#include "toggle-pin.h"
#include <string.h>


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
}


static QState dclockInitial(struct DClock *me)
{
	return Q_TRAN(&dclockState);
}


static QState dclockState(struct DClock *me)
{
	static uint8_t counter;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		display_clear();
		DISPLAY_LINE1_ROM("    A clock!");
		DISPLAY_LINE2_ROM(V);
		counter = 0;
		return Q_HANDLED();
	case WATCHDOG_SIGNAL:
		BSP_watchdog(me);
		/* Test the assertion code. */
		counter++;
		if (counter >= 10) {
			Q_ASSERT( 0 );
		}
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}
