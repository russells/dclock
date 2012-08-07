#ifndef dclock_h_INCLUDED
#define dclock_h_INCLUDED

#include "qpn_port.h"
#include "twi.h"

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
	BUTTON_SELECT_RELEASE_SIGNAL,

	BUTTON_2_SIGNAL,
	BUTTON_UP_PRESS_SIGNAL,
	BUTTON_UP_LONG_PRESS_SIGNAL,
	BUTTON_UP_REPEAT_SIGNAL,
	BUTTON_UP_RELEASE_SIGNAL,

	BUTTON_3_SIGNAL,
	BUTTON_DOWN_PRESS_SIGNAL,
	BUTTON_DOWN_LONG_PRESS_SIGNAL,
	BUTTON_DOWN_REPEAT_SIGNAL,
	BUTTON_DOWN_RELEASE_SIGNAL,

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
	/**
	 * Sent when the user is setting the time and the time is changed.
	 */
	UPDATE_TIME_SET_SIGNAL,
	/**
	 * Sent when the user is setting the time and we need to update the lcd
	 * cursor position to indicate to the user what value is being changed.
	 * This happens when each time setting state (hours, minutes, or
	 * seconds) is entered, and when the time has been changed.
	 */
	UPDATE_TIME_SET_CURSOR_SIGNAL,
	/**
	 * Sent when the time setting states get no input for a while and time
	 * out.
	 */
	UPDATE_TIME_TIMEOUT_SIGNAL,
	UPDATE_HOURS_TIMEOUT_SIGNAL,
	UPDATE_MINUTES_TIMEOUT_SIGNAL,
	UPDATE_SECONDS_TIMEOUT_SIGNAL,
	UPDATE_ALARM_TIMEOUT_SIGNAL,

	/**
	 * The time has been changed, so set it and write to the RTC.
	 */
	SET_TIME_SIGNAL,

	TWI_REQUEST_SIGNAL,
	TWI_REPLY_SIGNAL,
	TWI_FINISHED_SIGNAL,
	TWI_REPLY_0_SIGNAL,
	TWI_REPLY_1_SIGNAL,

	ALARM_ON_SIGNAL,
	ALARM_OFF_SIGNAL,

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
	/** Set false when we enter the time setting states, true when we
	    change the time in the time setting states.  Used to decide whether
	    or not to set the current time after we've finished in the time
	    setting states.  Later, will also indicate whether we update the
	    time in the RTC when we exit those states.  */
	uint8_t timeSetChanged;
	/** The new time used by the time setting states.  Hours, minutes, and
	    seconds. */
	uint8_t setTime[3];
	/** Count timeouts in the time setting states. */
	uint8_t setTimeouts;
	/** Set true when we are able to receive signals. */
	uint8_t ready;
	/** Indicates whether we are setting the time or the alarm. */
	uint8_t settingWhich;
	/** A temporary holder for the alarm state. */
	uint8_t alarmOn;
};


#define SETTING_ALARM 'A'
#define SETTING_TIME 'T'


struct DClock dclock;


/**
 * Call this instead of calling QActive_post().
 *
 * It checks that there is room in the event queue of the receiving state
 * machine.  QP-nano does this check itself anyway, but the assertion from
 * QP-nano will always appear at the same line in the same file, so we won't
 * know which state machine's queue is full.  If this check is done in user
 * code instead of library code we can tell them apart.
 */
#define post(o, sig, par)						\
	do {								\
		QActive *_me = (QActive *)(o);				\
		QActiveCB const Q_ROM *ao = &QF_active[_me->prio];	\
		Q_ASSERT(_me->nUsed < Q_ROM_BYTE(ao->end));		\
		QActive_post(_me, sig, (QParam)par);			\
	} while (0)

/**
 * Call this instead of calling QActive_postISR().
 *
 * @see post()
 */
#define postISR(o, sig, par)						\
	do {								\
		QActive *_me = (QActive *)(o);				\
		QActiveCB const Q_ROM *ao = &QF_active[_me->prio];	\
		Q_ASSERT(_me->nUsed < Q_ROM_BYTE(ao->end));		\
		QActive_postISR(_me, sig, (QParam)par);			\
	} while (0)

/**
 * Some of our objects have a ready member, that they set when they are able to
 * receive events.  For those objects, call this function to post an event.
 *
 * @see post()
 */
#define post_r(o, sig, par)			\
	do {					\
		if (o->ready)			\
			post(o, sig, par);	\
	} while (0)

/**
 * @see post_r()
 */
#define postISR_r(o, sig, par)			\
	do {					\
		if (o->ready)			\
			postISR(o, sig, par);	\
	} while (0)

#endif
