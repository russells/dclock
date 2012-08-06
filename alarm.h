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
	/** Set true when we are able to receive signals. */
	uint8_t ready;
};

extern struct Alarm alarm;

void alarm_ctor(void);

uint32_t get_alarm_dseconds(struct Alarm *me);
void set_alarm_dseconds(struct Alarm *me, uint32_t dseconds);

#endif
