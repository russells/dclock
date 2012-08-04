/**
 * @file
 *
 * Connect to a device via TWI and transfer data.
 *
 * The request functions allow two transactions to occur.  The two can be any
 * combination of read or write, and the TWI addresses can be different.  The
 * normal use case is that the first transaction is a write to a particular
 * device, setting an internal register address, and the second transaction is
 * a read to or write from the same device, beginning at that register address.
 *
 * We run two state machines here.  The QP-nano state machine is a very simple
 * QHsm.  In addition, the interrupt handler is implemented as an informal FSM
 * indexed by function pointer.  The real interrupt handler calls through that
 * function pointer to the handler, and the handler sets the next state by
 * changing the function pointer.
 */


#include "twi.h"
#include "twi-status.h"
//#include "wordclock-signals.h"
#include "serial.h"

#include "dclock.h"

#include "cpu-speed.h"
#include <util/delay.h>


Q_DEFINE_THIS_FILE;


/**
 * Interface to a TWI slave.
 */
struct TWI twi;


static QState twiInitial        (struct TWI *me);
static QState twiState          (struct TWI *me);
static QState twiBusyState      (struct TWI *me);

typedef void (*TWIInterruptHandler)(struct TWI *me);

static void twi_init(void);


/**
 * This function gets called from the TWI interrupt handler.
 *
 * This function pointer implements the interrupt state machine, and is set to
 * different handlers depending on what state we expect the TWI bus to be in at
 * the next interrupt.  Each handler is expected to check for its own errors,
 * reenable the bus for the next part of the transaction, and then set the
 * handler to the correct function for the next TWI bus state.
 */
volatile TWIInterruptHandler twint;

/**
 * Atomically set the interrupt handler state function pointer.
 *
 * Only call this when setting the function pointer with interrupts on, as it
 * will ensure interrupts are off before changing the function pointer.  If you
 * want to set the function pointer inside an interrupt handler (or inside one
 * of the interrupt state machine functions) don't bother calling this - just
 * set it.
 *
 * @param handler the interrupt state machine function pointer
 *
 * @param twcr if @e set_twcr is non-zero, also write @e twcr into TWCR, the
 * TWI Control Register
 *
 * @param set_twcr if non-zero, write @e twcr into TWCR
 */
static void set_twint(TWIInterruptHandler handler,
		      uint8_t twcr, uint8_t set_twcr);

static void send_start(struct TWI *me);

static void twint_null(struct TWI *me);
static void twint_start_sent(struct TWI *me);
static void twint_MT_address_sent(struct TWI *me);
static void twint_MR_address_sent(struct TWI *me);
static void twint_MT_data_sent(struct TWI *me);
static void twint_MR_data_received(struct TWI *me);

static void twi_int_error(struct TWI *me, uint8_t status);

static void start_request(struct TWI *);


void twi_ctor(void)
{
	SERIALSTR("twi_ctor()\r\n");

	QActive_ctor((QActive*)(&twi), (QStateHandler)&twiInitial);
	twi_init();
	twi.requests[0] = 0;
	twi.requests[1] = 0;
	twi.requestIndex = 0;
	twi.ready = 0;
}


/**
 * Set up the TWI bit rate and default interrupt function.
 */
static void twi_init(void)
{
	cli();
	set_twint(twint_null, 0, 0);
	TWCR = 0;
	TWSR = 1;		/* Prescaler = 4^1 = 4 */
	TWBR=10;		/* Approx 160kbits/s SCL */
	DDRC |= (1 << 5);
	DDRC |= (1 << 4);
	PORTC |= (1 << 5);
	PORTC |= (1 << 4);
	sei();
}


static QState twiInitial(struct TWI *me)
{
	return Q_TRAN(twiState);
}


static QState twiState(struct TWI *me)
{
	struct TWIRequest **requestp;
	struct TWIRequest *request;

	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		twi.ready = 73;
		return Q_HANDLED();

	case TWI_REQUEST_SIGNAL:
		//SERIALSTR("TWI Got TWI_REQUEST_SIGNAL\r\n");
		requestp = (struct TWIRequest **)((uint16_t)Q_PAR(me));
		Q_ASSERT( requestp );
		request = *requestp;
		Q_ASSERT( request );
		Q_ASSERT( ! me->requests[0] );
		me->requests[0] = request;
		requestp++;
		request = *requestp;
		Q_ASSERT( ! me->requests[1] );
		me->requests[1] = request;
		me->requestIndex = 0;
		return Q_TRAN(twiBusyState);
	}
	return Q_SUPER(&QHsm_top);
}


/**
 * Wait here until the interrupt state machine tells us it's finished the
 * TWI requests.  Reject any further TWI requests.
 */
static QState twiBusyState(struct TWI *me)
{
	uint8_t index;
	uint8_t sreg;
	struct TWIRequest **requestp;

	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		//SERIALSTR("TWI > twiBusyState\r\n");
		start_request(me);
		return Q_HANDLED();

	case Q_EXIT_SIG:
		//SERIALSTR("TWI < twiBusyState\r\n");
		sreg = SREG;
		cli();
		me->requests[0] = 0;
		me->requests[1] = 0;
		me->requestIndex = 0;
		SREG = sreg;
		return Q_HANDLED();

	case TWI_REQUEST_SIGNAL:
		SERIALSTR("TWI got excess TWI_REQUEST_SIGNAL\r\n");
		requestp = (struct TWIRequest **)((uint16_t)Q_PAR(me));
		if (requestp[0] && requestp[0]->signal) {
			requestp[0]->status = TWI_QUEUE_FULL;
			post(requestp[0]->qactive, requestp[0]->signal,
			     (QParam)((uint16_t)requestp[0]));
		}
		if (requestp[1] && requestp[1]->signal) {
			requestp[1]->status = TWI_QUEUE_FULL;
			post(requestp[1]->qactive, requestp[1]->signal,
			     (QParam)((uint16_t)requestp[1]));
		}
		return Q_HANDLED();

	case TWI_REPLY_SIGNAL:
		index = (uint8_t) Q_PAR(me);
		//SERIALSTR("TWI got TWI_REPLY_SIGNAL index=");
		//serial_send_int(index);
		//SERIALSTR("\r\n");
		post(me->requests[index]->qactive, me->requests[index]->signal,
		     (QParam)((uint16_t)(me->requests[index])));
		return Q_HANDLED();

	case TWI_FINISHED_SIGNAL:
		return Q_TRAN(twiState);

	}
	return Q_SUPER(twiState);
}


/**
 * Called at the very start of a request.
 *
 * The request can be either a single request, or a chain of two.  The chaining
 * of requests (with a REPEATED START) is handled later in the interrupt
 * handler.
 */
static void start_request(struct TWI *me)
{
	Q_ASSERT( ! me->requestIndex );
	/*
	SERIALSTR("TWI &request=");
	serial_send_hex_int((uint16_t)(me->requests[0]));
	SERIALSTR(" addr=");
	serial_send_hex_int(me->requests[0]->address & 0xfe);
	if (me->requests[0]->address & 0b1) {
		SERIALSTR("(r)");
	} else {
		SERIALSTR("(w)");
	}
	SERIALSTR(" nbytes=");
	serial_send_int(me->requests[0]->nbytes);
	SERIALSTR("\r\n");
	if (me->requests[1]) {
		SERIALSTR("    &request=");
		serial_send_hex_int((uint16_t)(me->requests[1]));
		SERIALSTR(" addr=");
		serial_send_hex_int(me->requests[1]->address & 0xfe);
		if (me->requests[1]->address & 0b1) {
			SERIALSTR("(r)");
		} else {
			SERIALSTR("(w)");
		}
		SERIALSTR(" nbytes=");
		serial_send_int(me->requests[1]->nbytes);
		SERIALSTR("\r\n");
	}
	serial_drain();
	*/
	me->requests[0]->count = 0;
	send_start(me);
}


/**
 * Interrupt handler for the TWI.  Not much work is done by this function -
 * it's all done by calling the interrupt state function.
 */
SIGNAL(TWI_vect)
{
	//static uint8_t counter = 0;

	//counter ++;
	//if (0 == counter)
	//SERIALSTR(",");
	if (! twi.requests[twi.requestIndex]) {
		twint = twint_null;
	}
	(*twint)(&twi);
}


static void set_twint(TWIInterruptHandler handler,
		      uint8_t twcr, uint8_t set_twcr)
{
	uint8_t sreg;

	sreg = SREG;
	cli();
	twint = handler;
	if (set_twcr) {
		TWCR = twcr;
	}
	SREG = sreg;
}


static void send_start(struct TWI *me)
{
	//SERIALSTR("send_start()\r\n");
	set_twint(twint_start_sent,
		  (1 << TWINT) |
		  (1 << TWSTA) |
		  (1 << TWEN ) |
		  (1 << TWIE ),
		  1);
}


/**
 * Default interrupt handler that disables the TWI.
 */
static void twint_null(struct TWI *me)
{
	/* Notify that we have been called.  This should never happen. */
	SERIALSTR("<TWI>");
	/* Disable the TWI.  We need to set TWINT in order to reset the
	   internal value of TWINT. */
	TWCR = (1 << TWINT);
}


/**
 * Handle an error detected during the interrupt handler.
 */
static void twi_int_error(struct TWI *me, uint8_t status)
{
	uint8_t index;

	index = me->requestIndex;
	/*
	SERIALSTR("<E:");
	serial_send_int(index);
	SERIALSTR(":0x");
	serial_send_hex_int(status);
	SERIALSTR(":&request=0x");
	serial_send_hex_int((uint16_t)(me->requests[index]));
	SERIALSTR(">");
	*/
	twint = twint_null;
	/* Transmit a STOP. */
	TWCR =  (1 << TWINT) |
		(1 << TWSTO) |
		(1 << TWEN );
	me->requests[index]->status = status;
	postISR((QActive*)me, TWI_REPLY_SIGNAL,
		(QParam)((uint16_t)(index)));
	postISR((QActive*)me, TWI_FINISHED_SIGNAL, 0);
}


/**
 * Called when we expect to have sent a start condition and need to next send
 * the TWI bus address (SLA+R/W).
 */
static void twint_start_sent(struct TWI *me)
{
	uint8_t status;

	status = TWSR & 0xf8;
	switch (status) {
	case TWI_08_START_SENT:
	case TWI_10_REPEATED_START_SENT:
		//SERIALSTR("<SS");
		if (me->requests[me->requestIndex]->address & 0b1) {
			//SERIALSTR(":R>");
			twint = twint_MR_address_sent;
		} else {
			//SERIALSTR(":T>");
			twint = twint_MT_address_sent;
		}
		/* Address includes R/W */
		TWDR = me->requests[me->requestIndex]->address;
		TWCR =  (1 << TWINT) |
			(1 << TWEN ) |
			(1 << TWIE );
		break;
	default:
		twi_int_error(me, status);
		break;
	}
}


/**
 * Called in MT mode, when we are sending data.
 */
static void twint_MT_address_sent(struct TWI *me)
{
	uint8_t status;

	//SERIALSTR("<MTA>");

	status = TWSR & 0xf8;
	switch (status) {
	case TWI_18_MT_SLA_W_TX_ACK_RX:
		//SERIALSTR("<MTAA>");
		/* We've sent an address or previous data, and got an ACK.  If
		   there is data to send, send the first byte.  If not,
		   finish. */
		if (me->requests[me->requestIndex]->nbytes) {
			uint8_t data = me->requests[me->requestIndex]->bytes[0];
			me->requests[me->requestIndex]->count ++;
			TWDR = data;
			twint = twint_MT_data_sent;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE );
		} else {
			/* No more data. */
			twint = twint_null;
			TWCR =  (1 << TWINT) |
				(1 << TWSTO) |
				(1 << TWEN ) |
				(1 << TWIE );
		}
		break;

	case TWI_20_MT_SLA_W_TX_NACK_RX:
		/* We've sent an address or previous data, and got a NACK. */
		//SERIALSTR("<MTAN>");
		twi_int_error(me, status);
		break;

	default:
		//SERIALSTR("<MTA?>");
		twi_int_error(me, status);
		break;
	}
}


/**
 * Called in master transmitter mode, after data has been sent.
 */
static void twint_MT_data_sent(struct TWI *me)
{
	uint8_t status;
	uint8_t data;
	volatile struct TWIRequest *request;

	//SERIALSTR("<MTD>");

	status = TWSR & 0xf8;
	switch (status) {

	case TWI_28_MT_DATA_TX_ACK_RX:
		request = me->requests[me->requestIndex];
		if (request->count >= request->nbytes) {
			/* finished */
			request->status = 0xf8;
			postISR((QActive*)me, TWI_REPLY_SIGNAL,
				((uint16_t)(me->requestIndex)));

			if ((0 == me->requestIndex) && me->requests[1]) {
				//SERIALSTR("<MTD+>");
				me->requestIndex ++;
				Q_ASSERT( me->requestIndex == 1 );
				twint = twint_start_sent;
				TWCR =  (1 << TWINT) |
					(1 << TWEN ) |
					(1 << TWIE ) |
					(1 << TWSTA);
			} else {
				//SERIALSTR("<MTD->");
				postISR((QActive*)me, TWI_FINISHED_SIGNAL, 0);
				twint = twint_null;
				TWCR =  (1 << TWINT) |
					(1 << TWEN ) |
					(1 << TWSTO);
			}

		} else {
			//SERIALSTR("<MTD_>");
			data = request->bytes[request->count];
			request->count ++;
			TWDR = data;
			/* All good, keep going */
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE );
		}
		break;

	case TWI_30_MT_DATA_TX_NACK_RX:
		twi_int_error(me, status);
		break;

	default:
		twi_int_error(me, status);
		break;
	}
}


/**
 * Called in master receiver mode, after the address has been sent.
 */
static void twint_MR_address_sent(struct TWI *me)
{
	uint8_t status;

	//SERIALSTR("<MRA>");

	status = TWSR & 0xf8;
	switch (status) {

	case TWI_40_MR_SLA_R_TX_ACK_RX:
		switch (me->requests[me->requestIndex]->nbytes) {
		case 0:
			//SERIALSTR("<MRA0>");
			/* No data to receive, so stop now. */
			twint = twint_null;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWSTO);
			break;

		case 1:
			//SERIALSTR("<MRA1>");
			/* We only want one byte, so make sure we NACK this
			   first byte. */
			twint = twint_MR_data_received;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE );
			break;

		default:
			//SERIALSTR("<MRAx>");
			/* We want more than one byte, so we have to ACK this
			   first byte to convince the slave to continue. */
			twint = twint_MR_data_received;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWEA ) |
				(1 << TWIE );
			break;
		}
		break;

	case TWI_48_MR_SLA_R_TX_NACK_RX:
		//SERIALSTR("<MRAN>");
		twi_int_error(me, status);
		break;

	default:
		//SERIALSTR("<MRA?>");
		twi_int_error(me, status);
		break;
	}
}


/**
 * Called in master receiver mode, after data has been received.
 */
static void twint_MR_data_received(struct TWI *me)
{
	/* FIXME */
	uint8_t status;
	uint8_t data;
	volatile struct TWIRequest *request;

	//SERIALSTR("<MRD>");

	status = TWSR & 0xf8;
	switch (status) {

	case TWI_50_MR_DATA_RX_ACK_TX:
		request = me->requests[me->requestIndex];
		//SERIALSTR("<MRDA:");
		//serial_send_int(request->count);
		//SERIALSTR(">");
		data = TWDR;
		request->bytes[request->count] = data;
		request->count ++;
		if (request->count == request->nbytes - 1) {
			/* Only one more byte required, so NACK that byte. */
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE );
		} else {
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWEA ) |
				(1 << TWIE );
		}
		break;

	case TWI_58_MR_DATA_RX_NACK_TX:
		request = me->requests[me->requestIndex];
		//SERIALSTR("<MRDN:");
		//serial_send_int(request->count);
		//SERIALSTR(">");
		data = TWDR;
		request->bytes[request->count] = data;
		request->count ++;
		request->status = 0xf8;
		/* Tell the state machine we've finished this (sub-)request. */
		postISR((QActive*)me, TWI_REPLY_SIGNAL, me->requestIndex);
		/* Now check for the next request. */
		if ((0 == me->requestIndex) && me->requests[1]) {
			me->requestIndex ++;
			twint = twint_start_sent;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWIE ) |
				(1 << TWSTA);
		} else {
			postISR((QActive*)me, TWI_FINISHED_SIGNAL, 0);
			twint = twint_null;
			TWCR =  (1 << TWINT) |
				(1 << TWEN ) |
				(1 << TWSTO);
		}
		break;

	default:
		twi_int_error(me, status);
		break;
	}
}
