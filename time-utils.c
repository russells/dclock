#include "time-utils.h"
#include "qpn_port.h"

Q_DEFINE_THIS_FILE;

/**
 * @file
 *
 * @todo Make these dependent on the current clock mode (decimal or normal.)
 */


void decimal_time_to_dtimes(uint32_t sec, uint8_t *times)
{
	Q_ASSERT( sec <= 99999 );
	times[2] = sec % 100;
	sec /= 100;
	times[1] = sec % 100;
	sec /= 100;
	times[0] = sec % 10;
}


uint32_t dtimes_to_decimal_time(uint8_t *dtimes)
{
	/* These three values are all unsigned and can all be zero, so we don't
	   need to check the lower bound. */
	Q_ASSERT( dtimes[0] <= 9 );
	Q_ASSERT( dtimes[1] <= 99 );
	Q_ASSERT( dtimes[2] <= 99 );
	return (dtimes[0] * 10000L) + (dtimes[1] * 100L) + dtimes[2];
}


void decimal_time_to_rtc_time(uint32_t dtime, uint8_t *bytes)
{
	uint32_t dayseconds;
	uint8_t hours, minutes, seconds;

	Q_ASSERT( dtime <= 99999 );

	dayseconds = (dtime * 108L) / 125L;
	Q_ASSERT( dayseconds <= 86399 );

	seconds = dayseconds % 60;
	minutes = (dayseconds / 60L) % 60;
	hours = (dayseconds / 3600L) % 24;

	bytes[0] = (seconds % 10) | ((seconds / 10) << 4);
	bytes[1] = (minutes % 10) | ((minutes / 10) << 4);
	bytes[2] = (hours   % 10) | ((hours   / 10) << 4);
}


uint32_t rtc_time_to_decimal_time(const uint8_t *bytes)
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


uint8_t inc_hours(uint8_t h)
{
	Q_ASSERT( h <= 9 );
	if (9 == h)
		return 0;
	return h+1;
}


uint8_t dec_hours(uint8_t h)
{
	Q_ASSERT( h <= 9 );
	if (0 == h)
		return 9;
	return h-1;
}


uint8_t inc_minutes(uint8_t m)
{
	Q_ASSERT( m <= 99 );
	if (99 == m)
		return 0;
	return m+1;
}


uint8_t dec_minutes(uint8_t m)
{
	Q_ASSERT( m <= 99 );
	if (0 == m)
		return 99;
	return m-1;
}


uint8_t inc_seconds(uint8_t s)
{
	Q_ASSERT( s <= 99 );
	if (99 == s)
		return 0;
	return s+1;
}


uint8_t dec_seconds(uint8_t s)
{
	Q_ASSERT( s <= 99 );
	if (0 == s)
		return 99;
	return s-1;
}


