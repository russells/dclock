#ifndef timesetter_h_INCLUDED
#define timesetter_h_INCLUDED

#include "qpn_port.h"


/**
 * Create the decimal clock.
 */
void timesetter_ctor(void);

/**
 */
struct TimeSetter {
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


extern struct TimeSetter timesetter;


#endif
