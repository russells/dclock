/**
 * @file
 *
 * @todo Work out what signals go here, and how this object communicates with
 * the main DClock.  Timekeeper should probably read the RTC, get the ticks
 * from the RTC code (32dHz interrupts), convert between decimal and real time,
 * handle the decimal or real time state, synchronise the decimal time with
 * real time every 108 seconds, and other stuff.
 */

#include "timekeeper.h"
#include "twi.h"
#include "twi-status.h"
#include "rtc.h"
#include "serial.h"
#include "alarm.h"
#include "bsp.h"
#include <stdio.h>


Q_DEFINE_THIS_FILE;


struct Timekeeper timekeeper;


static QState tkInitial                (struct Timekeeper *me);
static QState tkState                  (struct Timekeeper *me);

static void start_rtc_twi_request(struct Timekeeper *me,
				  uint8_t reg, uint8_t nbytes, uint8_t rw);

void timekeeper_ctor(void)
{
	QActive_ctor((QActive*)(&timekeeper), (QStateHandler)(&tkInitial));
	timekeeper.dseconds = 11745;
	timekeeper.ready = 73;
}


static QState tkInitial(struct Timekeeper *me)
{
	return Q_TRAN(&tkState);
}


static void inc_dseconds(struct Timekeeper *me)
{
	/* There is no need to disable interrupts while we access or update
	   dseconds, even though it's a four byte variable, as we never touch
	   it during an interrupt handler. */

	if (me->dseconds > 99999) {
		SERIALSTR("me->dseconds == ");
		char ds[12];
		snprintf(ds, 12, "%lu", me->dseconds);
		serial_send(ds);
		SERIALSTR("\r\n");
	}
	Q_ASSERT( me->dseconds <= 99999 );
	if (99999 == me->dseconds) {
		me->dseconds = 0;
	} else {
		me->dseconds++;
	}
}


static QState tkState(struct Timekeeper *me)
{
	uint8_t d32counter;

	switch(Q_SIG(me)) {

	case Q_ENTRY_SIG:
		start_rtc_twi_request(me, 0, 3, 1); /* Start from register 0, 3
						       bytes, read */
		return Q_HANDLED();

	case TWI_REPLY_0_SIGNAL:
		SERIALSTR("TWI_REPLY_0_SIGNAL: ");
		serial_send_rom(twi_status_string(me->twiRequest0.status));
		SERIALSTR("\r\n");
		SERIALSTR("   status was 0x");
		serial_send_hex_int(me->twiRequest0.status);
		SERIALSTR(" &request=");
		serial_send_hex_int((uint16_t)(&me->twiRequest0));
		SERIALSTR("\r\n");
		serial_drain();
		return Q_HANDLED();

	case TWI_REPLY_1_SIGNAL:
		SERIALSTR("TWI_REPLY_1_SIGNAL: ");
		serial_send_rom(twi_status_string(me->twiRequest1.status));
		SERIALSTR("\r\n");
		SERIALSTR("   status was 0x");
		serial_send_hex_int(me->twiRequest1.status);
		SERIALSTR(" &request=");
		serial_send_hex_int((uint16_t)(&me->twiRequest1));
		SERIALSTR("\r\n");
		SERIALSTR("    bytes=");
		serial_send_hex_int(me->twiBuffer1[0]);
		SERIALSTR(",");
		serial_send_hex_int(me->twiBuffer1[1]);
		SERIALSTR(",");
		serial_send_hex_int(me->twiBuffer1[2]);
		SERIALSTR("\r\n");
		serial_drain();
		me->dseconds = rtc_time_to_decimal_time(me->twiBuffer1);
		return Q_HANDLED();

	case TICK_DECIMAL_32_SIGNAL:
		d32counter = (uint8_t) Q_PAR(me);
		Q_ASSERT( d32counter != 0 );
		Q_ASSERT( d32counter <= 32 );
		if (d32counter == 32) {
			/* We set the decimal 1/32 second counter early, to
			   avoid the possibility that the second calculations
			   and display take much longer than the time before
			   the next timer interrupt, and therefore before the
			   next signal arrives. */
			BSP_set_decimal_32_counter(0);
			/* We've counted 32 parts of a decimal second, so tick
			   over to the next second. */
			post(me, TICK_DECIMAL_SIGNAL, 0);
		}
		return Q_HANDLED();

	case TICK_DECIMAL_SIGNAL:
		inc_dseconds(me);
		post_r((&alarm), TICK_DECIMAL_SIGNAL, me->dseconds);
		post_r((&dclock), TICK_DECIMAL_SIGNAL, me->dseconds);
		return Q_HANDLED();

	case WATCHDOG_SIGNAL:
		BSP_watchdog();
		return Q_HANDLED();
	}
	return Q_SUPER(QHsm_top);
}


uint32_t
rtc_time_to_decimal_time(const uint8_t *bytes)
{
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint32_t dtime;

	seconds = (bytes[0] & 0x0f) + (10 * ((bytes[0] & 0xf0) >> 4));
	minutes = (bytes[1] & 0x0f) + (10 * ((bytes[1] & 0xf0) >> 4));
	hours   = (bytes[2] & 0x0f) + (10 * ((bytes[2] & 0xf0) >> 4));

	dtime = ((uint32_t)seconds)
		+ (((uint32_t)minutes) * 60L)
		+ (((uint32_t)hours) * 3600L);
	dtime = (dtime * 125L) / 108L;
	return dtime;
}


uint32_t
get_dseconds(struct Timekeeper *me)
{
	return me->dseconds;
}


void set_dseconds(struct Timekeeper *me, uint32_t ds)
{
	me->dseconds = ds;
}

static void
start_rtc_twi_request(struct Timekeeper *me,
		      uint8_t reg, uint8_t nbytes, uint8_t rw)
{
	SERIALSTR("Sending a TWI request\r\n");

	me->twiRequest0.qactive = (QActive*)me;
	me->twiRequest0.signal = TWI_REPLY_0_SIGNAL;
	me->twiRequest0.bytes = me->twiBuffer0;
	me->twiBuffer0[0] = reg;
	me->twiRequest0.nbytes = 1;
	me->twiRequest0.address = RTC_ADDR << 1; /* RW = 0, write */
	me->twiRequest0.count = 0;
	me->twiRequest0.status = 0;
	me->twiRequestAddresses[0] = &(me->twiRequest0);

	me->twiRequest1.qactive = (QActive*)me;
	me->twiRequest1.signal = TWI_REPLY_1_SIGNAL;
	me->twiRequest1.bytes = me->twiBuffer1;
	me->twiRequest1.nbytes = nbytes;
	me->twiRequest1.address = (RTC_ADDR << 1) | (rw ? 1 : 0);
	me->twiRequest1.count = 0;
	me->twiRequest1.status = 0;
	me->twiRequestAddresses[1] = &(me->twiRequest1);

	post(&twi, TWI_REQUEST_SIGNAL, (QParam)((uint16_t)(&(me->twiRequestAddresses))));
}


