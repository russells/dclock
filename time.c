#include "time.h"
#include "qpn_port.h"
#include "dclock.h"
#include "timekeeper.h"
#include "timedisplay.h"
#include "alarm.h"
#include "serial.h"

#include <stdio.h>


Q_DEFINE_THIS_FILE;

/**
 * @file
 *
 * @todo Make these dependent on the current clock mode (decimal or normal.)
 */


void decimal_to_dtimes(uint32_t sec, uint8_t *times)
{
	Q_ASSERT( sec <= 99999 );
	times[2] = sec % 100;
	sec /= 100;
	times[1] = sec % 100;
	sec /= 100;
	times[0] = sec % 10;
}


uint32_t dtimes_to_decimal(uint8_t *dtimes)
{
	/* These three values are all unsigned and can all be zero, so we don't
	   need to check the lower bound. */
	Q_ASSERT( dtimes[0] <= 9 );
	Q_ASSERT( dtimes[1] <= 99 );
	Q_ASSERT( dtimes[2] <= 99 );
	return (dtimes[0] * 10000L) + (dtimes[1] * 100L) + dtimes[2];
}


struct NormalTime decimal_to_normal(uint32_t dtime)
{
	struct NormalTime normaltime;

	Q_ASSERT( dtime <= 99999 );

	dtime = (dtime * 108L) / 125L;
	normaltime.s = dtime % 60;
	dtime /= 60L;
	normaltime.m = dtime % 60;
	dtime /= 60L;
	normaltime.h = dtime;
	normaltime.pad = 0;
	return normaltime;
}


uint32_t normal_to_decimal(struct NormalTime ntime)
{
	uint32_t seconds;

	Q_ASSERT( ntime.s < 60 );
	Q_ASSERT( ntime.m < 60 );
	Q_ASSERT( ntime.h < 24 );

	seconds = ntime.s + (ntime.m * 60L) + (ntime.h * 3600L);
	return (seconds * 125L) / 108L;
}


uint32_t normal_day_seconds(struct NormalTime *ntp)
{
	return ntp->s + (60L * ntp->m) + (3600L * ntp->h);
}


#define MAKE_INC(name,maxn,maxd)					\
	uint8_t inc_##name(uint8_t v)					\
	{								\
		switch (get_time_mode()) {				\
		case NORMAL_MODE:					\
			Q_ASSERT( v <= maxn );				\
			if (maxn == v)					\
				return 0;				\
			else						\
				return v+1;				\
		case DECIMAL_MODE:					\
			Q_ASSERT( v <= maxd );				\
			if (maxd == v)					\
				return 0;				\
			else						\
				return v+1;				\
		default:						\
			Q_ASSERT(0);					\
			return 0;					\
		}							\
	}

#define MAKE_DEC(name,maxn,maxd)					\
	uint8_t dec_##name(uint8_t v)					\
	{								\
		switch (get_time_mode()) {				\
		case NORMAL_MODE:					\
			Q_ASSERT( v <= maxn );				\
			if (0 == v)					\
				return maxn;				\
			else						\
				return v-1;				\
		case DECIMAL_MODE:					\
			Q_ASSERT( v <= maxd );				\
			if (0 == v)					\
				return maxd;				\
			else						\
				return v-1;				\
		default:						\
			Q_ASSERT(0);					\
			return 0;					\
		}							\
	}

MAKE_INC(hours,23,9);
MAKE_DEC(hours,23,9);
MAKE_INC(minutes,59,99);
MAKE_DEC(minutes,59,99);
MAKE_INC(seconds,59,99);
MAKE_DEC(seconds,59,99);


uint8_t get_time_mode(void)
{
	return timekeeper.mode;
}


void set_time_mode(uint8_t mode)
{
	int sig;

	switch (mode) {
	case NORMAL_MODE:
		sig = NORMAL_MODE_SIGNAL;
		SERIALSTR("> normal mode\r\n");
		break;
	case DECIMAL_MODE:
		sig = DECIMAL_MODE_SIGNAL;
		SERIALSTR("> decimal mode\r\n");
		break;
	default:
		Q_ASSERT( 0 );
		/* Return keeps the compiler happy - otherwise it complains
		   about sig being used uninitialised. */
		return;
	}
	post((&timekeeper), sig, 0);
	post((&timedisplay), sig, 0);
	post((&alarm), sig, 0);
}


void toggle_time_mode(void)
{
	switch (get_time_mode()) {
	case NORMAL_MODE:
		set_time_mode(DECIMAL_MODE);
		break;
	case DECIMAL_MODE:
		set_time_mode(NORMAL_MODE);
		break;
	default:
		Q_ASSERT( 0 );
	}
}


void print_normal_time(struct NormalTime nt)
{
	char buf[10];

	snprintf(buf, 10, "%02d:%02d:%02d", nt.h, nt.m, nt.s);
	serial_send(buf);
}


void print_decimal_time(uint32_t dt)
{
	char buf[10];

	snprintf(buf, 10, "%06ld", dt);
	buf[8] = buf[6];
	buf[7] = buf[5];
	buf[6] = buf[4];
	buf[5] = '.';
	buf[4] = buf[3];
	buf[3] = buf[2];
	buf[2] = '.';
	serial_send(buf);
}
