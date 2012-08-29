/**
 * @file
 */

#include "alarm.h"
#include "bsp.h"
#include "buttons.h"
#include "dclock.h"
#include "lcd.h"
#include "serial.h"
#include "timedisplay.h"
#include "timekeeper.h"
#include "timesetter.h"
#include "toggle-pin.h"
#include "twi.h"
#include "version.h"


Q_DEFINE_THIS_FILE;


static QEvent twiQueue[4];
static QEvent buttonsQueue[4];
static QEvent alarmQueue[4];
static QEvent timekeeperQueue[4];
static QEvent timesetterQueue[4];
static QEvent timedisplayQueue[4];

/* The order of these objects is important, firstly because it determines
   priority, but also because it determines the order in which they are
   initialised.  For instance, timekeeper sends a signal to twi when dclock
   starts, so twi much be initialised first by placing it earlier in this
   list.

   A note about priorities: later listed AOs have a higher priority.  If there
   are any queued events for higher priority AOs, they get dispatched first,
   and lower priority AOs don't have their events dispatched until the higher
   priority AOs event queues are empty.

   In our case, the most important relative priority here is that alarm must be
   higher priority than timekeeper.  When the alarm is turned off, both alarm
   and timekeeper have events dispatched for that.  But timekeeper asks alarm
   its alarm state before writing to the RTC, and if alarm's event has not been
   processed yet it will get the wrong answer.  So make sure alarm's queue is
   processed before timekeeper's.

   Yes, this is weird and fragile, and should be fixed.
 */
QActiveCB const Q_ROM Q_ROM_VAR QF_active[] = {
	{ (QActive *)0              , (QEvent *)0      , 0                        },
	{ (QActive *)(&twi)         , twiQueue         , Q_DIM(twiQueue)          },
	{ (QActive *)(&timedisplay) , timedisplayQueue , Q_DIM(timedisplayQueue)  },
	{ (QActive *)(&buttons)     , buttonsQueue     , Q_DIM(buttonsQueue)      },
	{ (QActive *)(&timekeeper)  , timekeeperQueue  , Q_DIM(timekeeperQueue)   },
	{ (QActive *)(&alarm)       , alarmQueue       , Q_DIM(alarmQueue)        },
	{ (QActive *)(&timesetter)  , timesetterQueue  , Q_DIM(timesetterQueue)   },
};
/* If QF_MAX_ACTIVE is incorrectly defined, the compiler says something like:
   lapclock.c:68: error: size of array ‘Q_assert_compile’ is negative
 */
Q_ASSERT_COMPILE(QF_MAX_ACTIVE == Q_DIM(QF_active) - 1);


int main(int argc, char **argv)
{
	uint8_t mcusr;

 startmain:
	mcusr = MCUSR;
	MCUSR = 0;
	TOGGLE_BEGIN();
	BSP_startmain();	/* Disables the watchdog timer. */
	serial_init();
	serial_send_rom(startup_message);
	serial_drain();
	SERIALSTR("*** Reset reason:");
	if (mcusr & (1 << WDRF)) SERIALSTR(" WD");
	if (mcusr & (1 << BORF)) SERIALSTR(" BO");
	if (mcusr & (1 << EXTRF)) SERIALSTR(" EXT");
	if (mcusr & (1 << PORF)) SERIALSTR(" PO");
	SERIALSTR("\r\n");
	twi_ctor();
	timekeeper_ctor();
	lcd_init();
	buttons_ctor();
	alarm_ctor();
	timedisplay_ctor();
	timesetter_ctor();

	/* Drain the serial output just before the watchdog timer is
	   reenabled. */
	serial_drain();
	/* Initialize the BSP.  Enables the watchdog timer. */
	BSP_init();

	QF_run();

	goto startmain;
}


void QF_onStartup(void)
{
	Q_ASSERT(twi.ready);
	Q_ASSERT(timekeeper.ready);
	Q_ASSERT(buttons.ready);
	Q_ASSERT(alarm.ready);
	Q_ASSERT(timedisplay.ready);
	Q_ASSERT(timesetter.ready);

	serial_drain();

	BSP_QF_onStartup();
}
