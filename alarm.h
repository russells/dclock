#ifndef alarm_h_INCLUDED
#define alarm_h_INCLUDED

#include "time.h"
#include "qpn_port.h"

struct Alarm {
	QActive super;
	uint32_t decimalAlarmTime;
	struct NormalTime normalAlarmTime;
	uint32_t decimalSnoozeTime;
	struct NormalTime normalSnoozeTime;
	uint16_t alarmSoundCount;
	uint8_t snoozeCount;
	uint8_t turnOff;
	uint8_t armed;
	/** Set true when we are able to receive signals. */
	uint8_t ready;
};

extern struct Alarm alarm;

void alarm_ctor(void);

void get_alarm_times(struct Alarm *me, uint8_t *dtimes);
void set_alarm_times(struct Alarm *me, uint8_t *dtimes);

uint8_t get_alarm_state(struct Alarm *me);
void set_alarm_state(struct Alarm *me, uint8_t onoff);

#endif
