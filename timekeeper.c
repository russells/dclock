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
#include "time.h"
#include "twi.h"
#include "twi-status.h"
#include "rtc.h"
#include "serial.h"
#include "alarm.h"
#include "bsp.h"
#include "timedisplay.h"
#include <stdio.h>


Q_DEFINE_THIS_FILE;


struct Timekeeper timekeeper;


static QState tkInitial                (struct Timekeeper *me);
static QState topState                 (struct Timekeeper *me);
static QState startupState             (struct Timekeeper *me);
static QState readRTCState             (struct Timekeeper *me);
static QState setupRTCState            (struct Timekeeper *me);
static QState runningState             (struct Timekeeper *me);
static QState tkSetTimeState           (struct Timekeeper *me);
static QState tkSetAlarmState          (struct Timekeeper *me);

static void inc_decimaltime(struct Timekeeper *me);
static void inc_normaltime(struct Timekeeper *me);
static void rtc_to_normal(uint8_t *bytes, struct NormalTime *normaltime);
static void normal_to_rtc(struct NormalTime *normaltime, uint8_t *bytes);
static uint8_t checkRTCdata(uint8_t *bytes);
static uint8_t checkRTCalarm(uint8_t *bytes);
static void setupRTCdata(uint8_t *bytes);
static void start_rtc_twi_read(struct Timekeeper *me,
			       uint8_t reg, uint8_t nbytes);
static void default_times(struct Timekeeper *me);
static void set_alarm_alarm_times(uint8_t *bytes);

static void setup_108_125(struct Timekeeper *me);
static void synchronise_108_125(struct Timekeeper *me);

void timekeeper_ctor(void)
{
	QActive_ctor((QActive*)(&timekeeper), (QStateHandler)(&tkInitial));
	timekeeper.decimaltime = 50000;
	timekeeper.normaltime = *it2ntp(timekeeper.decimaltime);
	timekeeper.ready = 73;
	/* We need to start in normal mode since we do things with normal time
	   and the alarm very early on. */
	timekeeper.mode = NORMAL_MODE;
}


static QState tkInitial(struct Timekeeper *me)
{
	return Q_TRAN(&startupState);
}


static QState topState(struct Timekeeper *me)
{
	switch (Q_SIG(me)) {
	case WATCHDOG_SIGNAL:
		BSP_watchdog();
		return Q_HANDLED();
	case NORMAL_MODE_SIGNAL:
		me->mode = NORMAL_MODE;
		return Q_HANDLED();
	case DECIMAL_MODE_SIGNAL:
		me->mode = DECIMAL_MODE;
		return Q_HANDLED();
	}
	return Q_SUPER(QHsm_top);
}


static QState startupState(struct Timekeeper *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		QActive_arm((QActive*)me, 1);
		return Q_HANDLED();
	case Q_TIMEOUT_SIG:
		return Q_TRAN(readRTCState);
	}
	return Q_SUPER(topState);
}


static QState readRTCState(struct Timekeeper *me)
{
	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		start_rtc_twi_read(me, 0, 19); /* Start from register 0, 19
						  bytes */
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
		if (0xf8 == me->twiRequest0.status) {
			return Q_HANDLED();
		} else {
			default_times(me);
			return Q_TRAN(setupRTCState);
		}

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
		if (0xf8 != me->twiRequest1.status) {
			return Q_TRAN(setupRTCState);
		}
		if (0 != checkRTCdata(me->twiRequest1.bytes)) {
			return Q_TRAN(setupRTCState);
		}
		rtc_to_normal(me->twiBuffer1, &me->normaltime);
		me->decimaltime = normal_to_decimal(me->normaltime);
		if (0 == checkRTCalarm(me->twiRequest1.bytes)) {
			set_alarm_alarm_times(me->twiBuffer1 + 7);
			if (me->twiBuffer1[14] & 0b1) {
				post((&alarm), ALARM_ON_SIGNAL, 0);
			} else {
				post((&alarm), ALARM_OFF_SIGNAL, 0);
			}
		}
		return Q_TRAN(runningState);
	}
	return Q_SUPER(topState);
}


static QState setupRTCState(struct Timekeeper *me)
{
	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->twiBuffer1[0] = 0; /* Register 0 */
		setupRTCdata(me->twiBuffer1+1);

		me->twiRequest1.qactive = (QActive*)me;
		me->twiRequest1.signal = TWI_REPLY_1_SIGNAL;
		me->twiRequest1.bytes = me->twiBuffer1;
		me->twiRequest1.nbytes = 16;
		me->twiRequest1.address = RTC_ADDR << 1; /* |0 for write. */
		me->twiRequest1.count = 0;
		me->twiRequest1.status = 0;
		me->twiRequestAddresses[0] = &(me->twiRequest1);
		me->twiRequestAddresses[1] = 0;

		SERIALSTR("setupRTCState, bytes=");
		for (uint8_t i=0; i<16; i++) {
			SERIALSTR(" ");
			serial_send_hex_int(me->twiBuffer1[i]);
		}
		SERIALSTR("\r\n");

		post(&twi, TWI_REQUEST_SIGNAL,
		     (QParam)((uint16_t)(&(me->twiRequestAddresses))));
		return Q_HANDLED();
	case TWI_REPLY_1_SIGNAL:
		me->decimaltime = 50000;
		me->normaltime.h = 12;
		me->normaltime.m = 0;
		me->normaltime.s = 0;
		me->normaltime.pad = 0;
		SERIALSTR("setupRTCState > runningState\r\n");
		return Q_TRAN(runningState);
	}
	return Q_SUPER(topState);
}


static QState runningState(struct Timekeeper *me)
{
	uint8_t d32counter;

	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		SERIALSTR("runningState\r\n");
		setup_108_125(me);
		set_time_mode(NORMAL_MODE);
		BSP_enable_rtc_interrupt();
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
		inc_decimaltime(me);
		if (me->decimal125Count < 124) {
			/* Only count to 124 decimal seconds.  Each 125th
			   decimal second is counted by the code that
			   synchronises the decimal and normal seconds at
			   125/108 second boundaries. */
			me->decimal125Count ++;
			post_r((&alarm), TICK_DECIMAL_SIGNAL, me->decimaltime);
			post_r((&timedisplay), TICK_DECIMAL_SIGNAL, me->decimaltime);
		}
		return Q_HANDLED();

	case TICK_NORMAL_SIGNAL:
		inc_normaltime(me);
		post((&alarm), TICK_NORMAL_SIGNAL, nt2it(me->normaltime));
		post((&timedisplay), TICK_NORMAL_SIGNAL, nt2it(me->normaltime));
		synchronise_108_125(me);
		return Q_HANDLED();

	case SET_DECIMAL_TIME_SIGNAL:
		me->decimaltime = (uint32_t)(Q_PAR(me));
		me->normaltime = decimal_to_normal(me->decimaltime);
		setup_108_125(me);
		return Q_TRAN(tkSetTimeState);

	case SET_NORMAL_TIME_SIGNAL:
		me->normaltime = it2nt(Q_PAR(me));
		me->decimaltime = normal_to_decimal(me->normaltime);
		setup_108_125(me);
		return Q_TRAN(tkSetTimeState);

	case SET_NORMAL_ALARM_SIGNAL:
		me->normalalarmtime = it2nt(Q_PAR(me));
		return Q_TRAN(tkSetAlarmState);
	}
	return Q_SUPER(topState);
}


static QState
tkSetTimeState(struct Timekeeper *me)
{
	uint8_t status;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("tkSetTimeState\r\n");
		BSP_set_decimal_32_counter(0);

		/* Set up a TWI buffer to write the time. */
		me->twiBuffer0[0] = 0x00; /* Register address. */
		normal_to_rtc(&(me->normaltime), me->twiBuffer0 + 1);
		me->twiBuffer0[4] = 0x01; /* Day */
		me->twiBuffer0[5] = 0x01; /* Date */
		me->twiBuffer0[6] = 0x01; /* Month/Century */
		me->twiBuffer0[7] = 0x99; /* Year */
		me->twiRequest0.qactive = (QActive*)me;
		me->twiRequest0.signal = TWI_REPLY_0_SIGNAL;
		me->twiRequest0.bytes = me->twiBuffer0;
		me->twiRequest0.nbytes = 8;
		me->twiRequest0.address = RTC_ADDR << 1; /* |0 for write. */
		me->twiRequest0.count = 0;
		me->twiRequest0.status = 0;
		me->twiRequestAddresses[0] = &(me->twiRequest0);
		me->twiRequestAddresses[1] = 0;

		SERIALSTR("    bytes=");
		for (uint8_t i=0; i<8; i++) {
			SERIALSTR(" ");
			serial_send_hex_int(me->twiBuffer0[i]);
		}
		SERIALSTR("\r\n");

		post(&twi, TWI_REQUEST_SIGNAL,
		     (QParam)((uint16_t)(&(me->twiRequestAddresses))));
		return Q_HANDLED();

	case TWI_REPLY_0_SIGNAL:
		status = me->twiRequest0.status;
		switch (status) {
		case 0xf8:
			SERIALSTR("tkSetTimeState: success\r\n");
			break;
		default:
			SERIALSTR("tkSetTimeState: TWI_REPLY_0_SIGNAL: ");
			serial_send_rom(twi_status_string(status));
			SERIALSTR("\r\n");
			break;
		}
		return Q_TRAN(runningState);

	case Q_EXIT_SIG:
		SERIALSTR("tkSetTimeState exits\r\n");
		return Q_HANDLED();
	}
	return Q_SUPER(runningState);
}


static QState tkSetAlarmState(struct Timekeeper *me)
{
	uint8_t status;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		SERIALSTR("tkSetAlarmState\r\n");
		BSP_set_decimal_32_counter(0);

		/* Set up a TWI buffer to write the time. */
		me->twiBuffer0[0] = 0x07; /* Register address. */
		normal_to_rtc(&(me->normalalarmtime), me->twiBuffer0 + 1);
		me->twiBuffer0[4] = 0x00; /* Alarm 1 day */
		me->twiBuffer0[5] = 0x00; /* Alarm 2 minute */
		me->twiBuffer0[6] = 0x00; /* Alarm 2 hour */
		me->twiBuffer0[7] = 0x00; /* Alarm 2 day */
		if (get_alarm_state(&alarm)) {
			SERIALSTR("   alarm is on\r\n");
			me->twiBuffer0[8] = 0x81; /* /EOSC=1 A1IE=1 */
		} else {
			SERIALSTR("   alarm is off\r\n");
			me->twiBuffer0[8] = 0x80;
		}
		me->twiRequest0.qactive = (QActive*)me;
		me->twiRequest0.signal = TWI_REPLY_0_SIGNAL;
		me->twiRequest0.bytes = me->twiBuffer0;
		me->twiRequest0.nbytes = 9;
		me->twiRequest0.address = RTC_ADDR << 1; /* |0 for write. */
		me->twiRequest0.count = 0;
		me->twiRequest0.status = 0;
		me->twiRequestAddresses[0] = &(me->twiRequest0);
		me->twiRequestAddresses[1] = 0;

		SERIALSTR("    bytes=");
		for (uint8_t i=0; i<9; i++) {
			SERIALSTR(" ");
			serial_send_hex_int(me->twiBuffer0[i]);
		}
		SERIALSTR("\r\n");

		post(&twi, TWI_REQUEST_SIGNAL,
		     (QParam)((uint16_t)(&(me->twiRequestAddresses))));
		return Q_HANDLED();

	case TWI_REPLY_0_SIGNAL:
		status = me->twiRequest0.status;
		switch (status) {
		case 0xf8:
			SERIALSTR("tkSetAlarmState: success\r\n");
			break;
		default:
			SERIALSTR("tkSetAlarmState: TWI_REPLY_0_SIGNAL: ");
			serial_send_rom(twi_status_string(status));
			SERIALSTR("\r\n");
			break;
		}
		return Q_TRAN(runningState);

	case Q_EXIT_SIG:
		SERIALSTR("tkSetAlarmState exits\r\n");
		return Q_HANDLED();
	}
	return Q_SUPER(runningState);
}


/**
 * Returns 0 for ok, non-zero for something wrong.
 */
static uint8_t checkRTCdata(uint8_t *bytes)
{
	uint8_t e = 1;
	// seconds
	if ((bytes[0] & 0x0f) > 9   ) goto ret; else e++;
	if ((bytes[0] & 0x70) > 0x50) goto ret; else e++;
	// minutes
	if ((bytes[1] & 0x0f) > 9   ) goto ret; else e++;
	if ((bytes[1] & 0x70) > 0x50) goto ret; else e++;
	// hours
	if ((bytes[2] & 0x0f) > 9   ) goto ret; else e++;
	// Force 24 hour mode.
	if ( bytes[2] & 0x40)         goto ret; else e++;
	if ((bytes[2] & 0x30) > 0x20) goto ret; else e++;

	// A1IE is used for our own alarm purposes.
	// EOSC /BBSQW /CONF /RS2 /RS1 /INTCN /A2IE ?A1IE
	if ((bytes[14]& 0xfe) !=0x80) goto ret; else e++;

	/* @todo work out why we always get 0xC9 out of this register. */
	// /OSF /BB32kHz /CRATE1 /CRATE0 /EN32kHz BSY? A2F? A1F?
	if ((bytes[15] & 0x80) != 0x80) goto ret; else e++;

	return 0;
 ret:
	SERIALSTR("checkRTCdata: ");
	serial_send_int(e);
	SERIALSTR("\r\n   bytes:");
	for (uint8_t i=0; i<19; i++) {
		SERIALSTR(" ");
		serial_send_int(i);
		SERIALSTR(":");
		serial_send_hex_int(bytes[i]);
	}
	SERIALSTR("\r\n");
	return e;
}


static uint8_t checkRTCalarm(uint8_t *bytes)
{
	uint8_t e = 1;
	// seconds
	if ((bytes[7] & 0x0f) > 9   ) goto ret; else e++;
	if ((bytes[7] & 0x70) > 0x50) goto ret; else e++;
	// minutes
	if ((bytes[8] & 0x0f) > 9   ) goto ret; else e++;
	if ((bytes[8] & 0x70) > 0x50) goto ret; else e++;
	// hours
	if ((bytes[9] & 0x0f) > 9   ) goto ret; else e++;
	// Force 24 hour mode.
	if ( bytes[9] & 0x40)         goto ret; else e++;
	if ((bytes[9] & 0x30) > 0x20) goto ret; else e++;

	return 0;
 ret:
	SERIALSTR("checkRTCalarm: ");
	serial_send_int(e);
	SERIALSTR("\r\n   bytes:");
	for (uint8_t i=7; i<10; i++) {
		SERIALSTR(" ");
		serial_send_int(i);
		SERIALSTR(":");
		serial_send_hex_int(bytes[i]);
	}
	SERIALSTR("\r\n");
	return e;
}

static void setupRTCdata(uint8_t *bytes)
{
	bytes[0] = 0;		/* seconds */
	bytes[1] = 0;		/* minues */
	bytes[2] = 0x12;	/* hours */
	bytes[3] = 0x01;	/* day */
	bytes[4] = 0x01;	/* date */
	bytes[5] = 0x01;	/* month/century */
	bytes[6] = 0x99;	/* year */

	bytes[7] = 0;
	bytes[8] = 0;
	bytes[9] = 0;
	bytes[10] = 0;
	bytes[11] = 0;
	bytes[12] = 0;
	bytes[13] = 0;

	bytes[14] = 0x80;	/* EOSC etc */
	bytes[15] = 0;		/* OSF, BB32kHz etc */
}


static void inc_decimaltime(struct Timekeeper *me)
{
	/* There is no need to disable interrupts while we access or update
	   dseconds, even though it's a four byte variable, as we never touch
	   it during an interrupt handler. */

	if (me->decimaltime > 99999) {
		SERIALSTR("me->decimaltime == ");
		char ds[12];
		snprintf(ds, 12, "%lu", me->decimaltime);
		serial_send(ds);
		SERIALSTR("\r\n");
	}
	Q_ASSERT( me->decimaltime <= 99999 );
	if (99999 == me->decimaltime) {
		me->decimaltime = 0;
	} else {
		me->decimaltime++;
	}
}


static void inc_normaltime(struct Timekeeper *me)
{
	Q_ASSERT( me->normaltime.h < 24 );
	Q_ASSERT( me->normaltime.m < 60 );
	Q_ASSERT( me->normaltime.s < 60 );

	me->normaltime.s ++;
	if (me->normaltime.s == 60) {
		me->normaltime.s = 0;
		me->normaltime.m ++;
		if (me->normaltime.m == 60) {
			me->normaltime.m = 0;
			me->normaltime.h ++;
			if (me->normaltime.h == 24) {
				me->normaltime.h = 0;
			}
		}
	}
}


static void
normal_to_rtc(struct NormalTime *normaltime, uint8_t *bytes)
{
	Q_ASSERT( normaltime->h < 24 );
	Q_ASSERT( normaltime->m < 60 );
	Q_ASSERT( normaltime->s < 60 );
	bytes[0] = (normaltime->s % 10) | ((normaltime->s / 10) << 4);
	bytes[1] = (normaltime->m % 10) | ((normaltime->m / 10) << 4);
	bytes[2] = (normaltime->h % 10) | ((normaltime->h / 10) << 4);
}


static void
rtc_to_normal(uint8_t *bytes, struct NormalTime *normaltime)
{
	/* hours */
	normaltime->h = (bytes[2] & 0x0f) + (((bytes[2] & 0x30) >> 4) * 10);
	/* minutes */
	normaltime->m = (bytes[1] & 0x0f) + (((bytes[1] & 0x70) >> 4) * 10);
	/* seconds */
	normaltime->s = (bytes[0] & 0x0f) + (((bytes[0] & 0x70) >> 4) * 10);
	normaltime->pad = 0;
}


uint32_t
get_decimal_time(void)
{
	return timekeeper.decimaltime;
}


static void
set_decimal_time(uint32_t ds)
{
	post_r((&timekeeper), SET_DECIMAL_TIME_SIGNAL, (QParam)ds);
}


struct NormalTime
get_normal_time(void)
{
	return timekeeper.normaltime;
}


static void
set_normal_time(struct NormalTime nt)
{
	post_r((&timekeeper), SET_NORMAL_TIME_SIGNAL, nt2it(nt));
}


static void
start_rtc_twi_read(struct Timekeeper *me, uint8_t reg, uint8_t nbytes)
{
	static char buffer[100];

	snprintf(buffer, 99, "tk>TWI: reg=%d nbytes=%d\r\n", reg, nbytes);
	serial_send(buffer);

	Q_ASSERT( nbytes <= 20 );

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
	me->twiRequest1.address = (RTC_ADDR << 1) | 0b1;
	me->twiRequest1.count = 0;
	me->twiRequest1.status = 0;
	me->twiRequestAddresses[1] = &(me->twiRequest1);

	post(&twi, TWI_REQUEST_SIGNAL, (QParam)((uint16_t)(&(me->twiRequestAddresses))));
}


void get_times(uint8_t *times)
{
	switch (get_time_mode()) {
	case DECIMAL_MODE:
		Q_ASSERT( timekeeper.decimaltime <= 99999 );
		decimal_to_dtimes(timekeeper.decimaltime, times);
		break;
	case NORMAL_MODE:
		Q_ASSERT( timekeeper.normaltime.h <= 23 );
		Q_ASSERT( timekeeper.normaltime.m <= 59 );
		Q_ASSERT( timekeeper.normaltime.s <= 59 );
		times[0] = timekeeper.normaltime.h;
		times[1] = timekeeper.normaltime.m;
		times[2] = timekeeper.normaltime.s;
		break;
	default:
		Q_ASSERT( 0 );
		break;
	}
}


void set_times(uint8_t *times)
{
	uint32_t dt;
	struct NormalTime nt;

	switch (get_time_mode()) {
	case DECIMAL_MODE:
		/* These three values are all unsigned and can all be zero, so
		   we don't need to check the lower bound. */
		Q_ASSERT( times[0] <= 9 );
		Q_ASSERT( times[1] <= 99 );
		Q_ASSERT( times[2] <= 99 );
		dt = dtimes_to_decimal(times);
		set_decimal_time(dt);
		SERIALSTR("Time set to ");
		print_decimal_time(dt);
		SERIALSTR("\r\n");
		break;
	case NORMAL_MODE:
		Q_ASSERT( times[0] <= 23 );
		Q_ASSERT( times[1] <= 59 );
		Q_ASSERT( times[2] <= 59 );
		nt.h = times[0];
		nt.m = times[1];
		nt.s = times[2];
		nt.pad = 0;
		set_normal_time(nt);
		SERIALSTR("Time set to ");
		print_normal_time(nt);
		SERIALSTR("\r\n");
		break;
	default:
		Q_ASSERT( 0 );
		break;
	}
}


static void set_alarm_alarm_times(uint8_t *bytes)
{
	struct NormalTime nat;
	uint32_t dat;

	rtc_to_normal(bytes, &nat);
	nat.s = 0;
	dat = normal_to_decimal(nat);
	set_normal_alarm_time(&alarm, nat);
	set_decimal_alarm_time(&alarm, dat);
}


void set_alarm_times(struct Timekeeper *me, uint8_t *times, uint8_t on)
{
	uint32_t dat;
	struct NormalTime nat;

	switch (get_time_mode()) {
	default:
		Q_ASSERT( 0 );
	case DECIMAL_MODE:
		/* These three values are all unsigned and can all be zero, so
		   we don't need to check the lower bound. */
		Q_ASSERT( times[0] <= 9 );
		Q_ASSERT( times[1] <= 99 );
		Q_ASSERT( times[2] == 0 );
		dat = (times[0] * 10000L)
			+ (times[1] * 100L) + times[2];
		nat = decimal_to_normal(dat);
		SERIALSTR("alarm time: ");
		print_decimal_time(dat);
		SERIALSTR(" (");
		print_normal_time(nat);
		SERIALSTR(")\r\n");
		break;
	case NORMAL_MODE:
		Q_ASSERT( times[0] <= 23 );
		Q_ASSERT( times[1] <= 59 );
		Q_ASSERT( times[2] == 0 );
		nat.h = times[0];
		nat.m = times[1];
		nat.s = times[2];
		nat.pad = 0;
		dat = normal_to_decimal(nat);
		SERIALSTR("alarm time: ");
		print_normal_time(nat);
		SERIALSTR(" (");
		print_decimal_time(dat);
		SERIALSTR(")\r\n");
		break;
	}
	set_normal_alarm_time(&alarm, nat);
	set_decimal_alarm_time(&alarm, dat);
	if (on) {
		post((&alarm), ALARM_ON_SIGNAL, 0);
	} else {
		post((&alarm), ALARM_OFF_SIGNAL, 0);
	}
	post(me, SET_NORMAL_ALARM_SIGNAL, nt2it(nat));
}


static void default_times(struct Timekeeper *me)
{
	me->normaltime.h = 0x12;
	me->normaltime.m = 0x00;
	me->normaltime.s = 0x00;
	me->decimaltime = normal_to_decimal(me->normaltime);
}


static void setup_108_125(struct Timekeeper *me)
{
	uint32_t ntd;

	ntd = normal_day_seconds(&(me->normaltime));
	me->normal108Count = ntd % 108;
	me->decimal125Count = me->decimaltime % 125;
}


static void synchronise_108_125(struct Timekeeper *me)
{
	me->normal108Count ++;
	if (108 == me->normal108Count) {
		me->normal108Count = 0;
		me->decimal125Count = 0;
		BSP_set_decimal_32_counter(0);
		me->decimaltime = normal_to_decimal(me->normaltime);
		/* We only count up to 124 seconds using the CPU timer and
		   TICK_DECIMAL_32_SIGNALs, and the 125th second is counted
		   here. */
		post_r((&alarm), TICK_DECIMAL_SIGNAL, me->decimaltime);
		post_r((&timedisplay), TICK_DECIMAL_SIGNAL, me->decimaltime);
	}
}
