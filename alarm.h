#ifndef alarm_h_INCLUDED
#define alarm_h_INCLUDED

#include "qpn_port.h"

struct Alarm {
	QActive super;
	uint32_t alarmTime;
	uint8_t armed;
	/** The LCD brightness when we turned on the alarm. */
	uint8_t enterBrightness;
	/** The brighter brightness when we flash the display. */
	uint8_t onBrightness;
	/** The duller brightness when we flash the display. */
	uint8_t offBrightness;
};

extern struct Alarm alarm;

void alarm_ctor(void);

#endif