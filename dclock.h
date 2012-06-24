#ifndef dclock_h_INCLUDED
#define dclock_h_INCLUDED

#include "qpn_port.h"

/* Testing - stop the app and busy loop. */
#define FOREVER for(;;)

enum ButtonNumbers {
	BUTTON1 = 1,
	BUTTON2,
	BUTTON3,
};

enum DClockSignals {
	/**
	 * Button press.
	 */
	BUTTONS_UP_SIGNAL = Q_USER_SIG,

	BUTTON_1_DOWN_SIGNAL,
	BUTTON_1_PRESS_SIGNAL,
	BUTTON_1_LONG_PRESS_SIGNAL,
	BUTTON_1_REPEAT_SIGNAL,

	BUTTON_2_DOWN_SIGNAL,
	BUTTON_2_PRESS_SIGNAL,
	BUTTON_2_LONG_PRESS_SIGNAL,
	BUTTON_2_REPEAT_SIGNAL,

	BUTTON_3_DOWN_SIGNAL,
	BUTTON_3_PRESS_SIGNAL,
	BUTTON_3_LONG_PRESS_SIGNAL,
	BUTTON_3_REPEAT_SIGNAL,

	/**
	 * Sent once per second so we can confirm that the event loop is
	 * running.
	 */
	WATCHDOG_SIGNAL,
	/**
	 * Send to the timekeeper 32 times a second.
	 */
	TICK32_SIGNAL,
	MAX_PUB_SIG,
	MAX_SIG,
};


/**
 * Create the decimal clock.
 */
void dclock_ctor(void);

/**
 */
struct DClock {
	QActive super;
};


struct DClock dclock;


/**
 * Call this just before calling QActive_post() or QActive_postISR().
 *
 * It checks that there is room in the event queue of the receiving state
 * machine.  QP-nano does this check itself anyway, but the assertion from
 * QP-nano will always appear at the same line in the same file, so we won't
 * know which state machine's queue is full.  If this check is done in user
 * code instead of library code we can tell them apart.
 */
#define fff(o) Q_ASSERT(((QActive*)(o))->nUsed <= Q_ROM_BYTE(QF_active[((QActive*)(o))->prio].end))

#endif
