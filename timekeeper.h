#ifndef timekeeper_h_INCLUDED
#define timekeeper_h_INCLUDED

#include "qpn_port.h"
#include "twi.h"
#include "time.h"
#include "alarm.h"


struct Timekeeper {
	QActive super;

	/** The decimal time, as one number. */
	uint32_t decimaltime;

	/** The normal time, as hours, minutes, and seconds. */
	struct NormalTime normaltime;

	/** The alarm time, only used when we set the alarm time. */
	struct NormalTime normalalarmtime;

	/** Set to zero every 108 normal seconds, for synchronisation. */
	uint8_t normal108Count;

	/** Used for synchronising the decimal and normal seconds. */
	uint8_t decimal125Count;

	/** Decimal or normal mode. */
	uint8_t mode;

	/** Holder for the first TWI request. */
	struct TWIRequest twiRequest0;
	uint8_t twiBuffer0[12];

	/** Holder for the second TWI request. */
	struct TWIRequest twiRequest1;
	uint8_t twiBuffer1[20];

	/** This contains the addresses of one or both of the TWIRequests
	    above.  When we do consecutive TWI operations (which means keeping
	    control of the bus between the operations and only receiving a
	    result after both have finished) we fill in both pointers.  For a
	    single operation, only fill in the first pointer. */
        struct TWIRequest *twiRequestAddresses[2];

	uint8_t ready;
};


extern struct Timekeeper timekeeper;


void timekeeper_ctor(void);

uint32_t get_decimal_time(void);
struct NormalTime get_normal_time(void);
void get_times(uint8_t *dtimes);
void set_times(uint8_t *dtimes);

void set_alarm_times(struct Timekeeper *me, uint8_t *dtimes, uint8_t on);

#endif
