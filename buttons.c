#include "buttons.h"
#include "alarm.h"
#include "serial.h"
#include "bsp.h"
#include "dclock.h"

/**
 * @file Handle button presses, and send them to other active objects.
 *
 * @note Button 1 is select, button 2 is up, button 3 is down.  Conversion from
 * button numbers to particular buttons is done in several places.
 *
 * @todo Decide if the button long press signals are really needed, or if
 * consumers of button events can merely decide that there's been a long press
 * if there were any repeats.
 *
 * @todo Do we need to send button up signals to consumers of button events?
 */


Q_DEFINE_THIS_FILE;


/** The number of ticks before we send a button long press event.  Based on
    sampling the buttons at about 37 Hz (32/0.864).*/
#define LONG_PRESS 30

/** The number of ticks before we start sending button repeat events. */
#define FIRST_REPEAT 18

/** The number of ticks between button repeat events. */
#define REPEAT_PERIOD 7


struct Buttons buttons;


static QState buttonsInitial(struct Buttons *me);
static QState buttonsState(struct Buttons *me);
static QState buttonDownState(struct Buttons *me);
static QState buttonLongState(struct Buttons *me);
static QState buttonRepeatingState(struct Buttons *me);


void
buttons_ctor(void)
{
	QActive_ctor((QActive *)(&buttons), (QStateHandler)&buttonsInitial);
	buttons.whichButton = 0;
	buttons.repeatCount = 0;
	buttons.ready = 0;
}


static QState buttonsInitial(struct Buttons *me)
{
	return Q_TRAN(&buttonsState);
}


static QState buttonsState(struct Buttons *me)
{
	uint8_t button;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		me->ready = 73;
		return Q_HANDLED();
	case TICK_DECIMAL_32_SIGNAL:
		button = BSP_getButton();
		switch (button) {
		default: /* Shouldn't happen - handle as though nothing is
			    pressed. */
			/*FALLTHROUGH*/
		case 0:
			post(me, BUTTONS_UP_SIGNAL, 0);
			break;
		case 1:
			post(me, BUTTON_1_SIGNAL, 1);
			break;
		case 2:
			post(me, BUTTON_2_SIGNAL, 2);
			break;
		case 3:
			post(me, BUTTON_3_SIGNAL, 3);
			break;
		}
		return Q_HANDLED();

	case BUTTONS_UP_SIGNAL:
		me->whichButton = 0;
		me->repeatCount = 0;
		return Q_HANDLED();

	case BUTTON_1_SIGNAL:
	case BUTTON_2_SIGNAL:
	case BUTTON_3_SIGNAL:
		button = (uint8_t)(Q_PAR(me));
		if (0 == me->whichButton) {
			me->whichButton = button;
		}
		if (button == me->whichButton) {
			/* The same button is still pressed, we're counting
			   ticks to see how long it's down. */
			me->repeatCount++;
			if (me->repeatCount >= 3) {
				return Q_TRAN(buttonDownState);
			}
		}
		else {
			/* Ah nu!  A different button has been pressed while we
			   were counting ticks.  Beached az! */
			me->repeatCount = 1;
			me->whichButton = button;
		}
		return Q_HANDLED();
	}
	return Q_SUPER(&QHsm_top);
}


static QState buttonDownState(struct Buttons *me)
{
	uint8_t button;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		switch (me->whichButton) {
		case 1:
			post(&alarm, BUTTON_SELECT_PRESS_SIGNAL, 0);
			break;
		case 2:
			post(&alarm, BUTTON_UP_PRESS_SIGNAL, 0);
			break;
		case 3:
			post(&alarm, BUTTON_DOWN_PRESS_SIGNAL, 0);
			break;
		}
		return Q_HANDLED();

	case BUTTONS_UP_SIGNAL:
		return Q_TRAN(buttonsState);

	case BUTTON_1_SIGNAL:
	case BUTTON_2_SIGNAL:
	case BUTTON_3_SIGNAL:
		button = (uint8_t)(Q_PAR(me));
		if (button != me->whichButton) {
			/* Beached az! */
			return Q_TRAN(buttonsState);
		}
		/* We have a button down, and it's the same one that we've been
		   monitoring up to now. */
		me->repeatCount++;
		if (me->repeatCount == LONG_PRESS) {
			return Q_TRAN(buttonLongState);
		}
		return Q_HANDLED();

	case Q_EXIT_SIG:
		switch (me->whichButton) {
		case 1:
			post(&alarm, BUTTON_SELECT_RELEASE_SIGNAL, 0);
			break;
		case 2:
			post(&alarm, BUTTON_UP_RELEASE_SIGNAL, 0);
			break;
		case 3:
			post(&alarm, BUTTON_DOWN_RELEASE_SIGNAL, 0);
			break;
		}
		me->whichButton = 0;
		me->repeatCount = 0;
		return Q_HANDLED();
	}
	return Q_SUPER(buttonsState);
}


static QState buttonLongState(struct Buttons *me)
{
	uint8_t button;

	switch (Q_SIG(me)) {
	case Q_ENTRY_SIG:
		switch (me->whichButton) {
		case 1:
			post(&alarm, BUTTON_SELECT_LONG_PRESS_SIGNAL, 0);
			break;
		case 2:
			post(&alarm, BUTTON_UP_LONG_PRESS_SIGNAL, 0);
			break;
		case 3:
			post(&alarm, BUTTON_DOWN_LONG_PRESS_SIGNAL, 0);
			break;
		}
		me->repeatCount = 0;
		return Q_HANDLED();

	case BUTTONS_UP_SIGNAL:
		return Q_TRAN(buttonsState);

	case BUTTON_1_SIGNAL:
	case BUTTON_2_SIGNAL:
	case BUTTON_3_SIGNAL:
		button = (uint8_t)(Q_PAR(me));
		if (button != me->whichButton) {
			return Q_TRAN(buttonsState);
		} else {
			me->repeatCount ++;
			if (me->repeatCount >= FIRST_REPEAT) {
				return Q_TRAN(buttonRepeatingState);
			}
		}
		return Q_HANDLED();
	}
	return Q_SUPER(buttonDownState);
}



static QState buttonRepeatingState(struct Buttons *me)
{
	uint8_t button;

	switch (Q_SIG(me)) {

	case Q_ENTRY_SIG:
		me->repeatCount = 0;
		return Q_HANDLED();

	case BUTTONS_UP_SIGNAL:
		return Q_TRAN(buttonsState);

	case BUTTON_1_SIGNAL:
	case BUTTON_2_SIGNAL:
	case BUTTON_3_SIGNAL:
		button = (uint8_t)(Q_PAR(me));
		if (button != me->whichButton) {
			/* Beached az! */
			return Q_TRAN(buttonsState);
		}
		me->repeatCount++;
		if (me->repeatCount >= REPEAT_PERIOD) {
			switch (me->whichButton) {
			case 1:
				post(&alarm, BUTTON_SELECT_REPEAT_SIGNAL, 0);
				break;
			case 2:
				post(&alarm, BUTTON_UP_REPEAT_SIGNAL, 0);
				break;
			case 3:
				post(&alarm, BUTTON_DOWN_REPEAT_SIGNAL, 0);
				break;
			}
			me->repeatCount = 0;
		}
		return Q_HANDLED();
	}
	return Q_SUPER(buttonLongState);
}

