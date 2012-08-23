#ifndef timedisplay_h_INCLUDED
#define timedisplay_h_INCLUDED

#include "qpn_port.h"

struct TimeDisplay {
	QActive super;
	uint8_t mode;
	uint8_t statuses;
	uint8_t ready;

	/** The LCD brightness when the alarm started running. */
	uint8_t preAlarmBrightness;
	/** The brighter brightness when we flash the display. */
	uint8_t onBrightness;
	/** The duller brightness when we flash the display. */
	uint8_t offBrightness;
};

extern struct TimeDisplay timedisplay;

/**
 * A list of status entries to put in the bottom line of the display.
 *
 * These must be powers of two, as they are used to make a bitmap in the
 * statuses field of TimeDisplay.
 */
enum DisplayStatus {
	DSTAT_ALARM = 0x01,
	DSTAT_SNOOZE = 0x02,
	DSTAT_ALARM_RUNNING = 0x04,
};

void timedisplay_ctor(void);

void display_status_on(enum DisplayStatus ds);
void display_status_off(enum DisplayStatus ds);

#endif
