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

	BUTTON_1_SIGNAL,
	BUTTON_SELECT_PRESS_SIGNAL,
	BUTTON_SELECT_LONG_PRESS_SIGNAL,
	BUTTON_SELECT_REPEAT_SIGNAL,

	BUTTON_2_SIGNAL,
	BUTTON_UP_PRESS_SIGNAL,
	BUTTON_UP_LONG_PRESS_SIGNAL,
	BUTTON_UP_REPEAT_SIGNAL,

	BUTTON_3_SIGNAL,
	BUTTON_DOWN_PRESS_SIGNAL,
	BUTTON_DOWN_LONG_PRESS_SIGNAL,
	BUTTON_DOWN_REPEAT_SIGNAL,

	/**
	 * Sent once per second so we can confirm that the event loop is
	 * running.
	 */
	WATCHDOG_SIGNAL,
	/**
	 * Sent to the clock 32 times per decimal second.
	 */
	TICK_DECIMAL_32_SIGNAL,
	/**
	 * Sent to the clock once per decimal second.
	 */
	TICK_DECIMAL_SIGNAL,
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
	uint32_t dseconds;
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
