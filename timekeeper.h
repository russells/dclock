#ifndef timekeeper_h_INCLUDED
#define timekeeper_h_INCLUDED

#include "qpn_port.h"
#include "twi.h"

struct Timekeeper {
	QActive super;
	uint32_t dseconds;

	/** Holder for the first TWI request. */
	struct TWIRequest twiRequest0;
	uint8_t twiBuffer0[12];

	/** Holder for the second TWI request. */
	struct TWIRequest twiRequest1;
	uint8_t twiBuffer1[12];

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

uint32_t get_dseconds(struct Timekeeper *me);
void set_dseconds(struct Timekeeper *me, uint32_t ds);
uint32_t rtc_time_to_decimal_time(const uint8_t *bytes);

#endif
