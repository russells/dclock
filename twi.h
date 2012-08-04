#ifndef twi_h_INCLUDED
#define twi_h_INCLUDED

#include "qpn_port.h"


/**
 * Request to read from or write to a TWI device.
 */
struct TWIRequest {
	QActive *qactive;	/**< Where to send the result. */
	int signal;		/**< Signal to use when finished. */
	uint8_t *bytes;		/**< Where to get or put the data. */
	uint8_t address;	/**< I2C address. */
	uint8_t nbytes;		/**< Number of bytes to read or write. */
	uint8_t count;		/**< Number of bytes done. */
	uint8_t status;		/**< Return status to caller. */
};


/**
 * This represents a state machine implementing TWI, and one or two requests.
 */
struct TWI {
	QActive super;
	/** Pointers to the current request.  These must be volatile as they're
	    used by the TWI interrupt handler. */
	struct TWIRequest volatile *requests[2];
	/** The request currently being handled by the TWI and associated
	    interrupt handler. */
	uint8_t requestIndex;
	/** Set true when we are able to receive signals. */
	uint8_t ready;
};


enum TWICodes {
	TWI_OK = 0,		/**< Everything went ok. */
	TWI_QUEUE_FULL,		/**< Too many requests. */
	TWI_NACK,		/**< Some part of the transaction NACKEd. */
};


extern struct TWI twi;


void twi_ctor(void);


#endif
