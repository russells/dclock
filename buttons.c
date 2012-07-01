#include "buttons.h"
#include "bsp.h"
#include "dclock.h"


/** The number of ticks before we send a button long press event. */
#define LONG_PRESS 50

/** The number of ticks before we start sending button repeat events. */
#define FIRST_REPEAT 40

/** The number of ticks between button repeat events. */
#define REPEAT_PERIOD 15


struct Buttons buttons;


static QState buttonsInitial(struct Buttons *me);
static QState buttonsState(struct Buttons *me);
static QState buttonDownState(struct Buttons *me);
static QState buttonLongState(struct Buttons *me);
static QState buttonRepeatingState(struct Buttons *me);


/* FIXME: add states to express the first repeat and extra repeats. */


void
buttons_ctor(void)
{
	QActive_ctor((QActive *)(&buttons), (QStateHandler)&buttonsInitial);

}


static QState buttonsInitial(struct Buttons *me)
{
	return Q_TRAN(&buttonsState);
}


static QState buttonsState(struct Buttons *me)
{
	uint8_t button;

	switch (Q_SIG(me)) {
	case TICK_DECIMAL_32_SIGNAL:
		button = BSP_getButton();
		switch (button) {
		default: /* Shouldn't happen - handle as though nothing is
			    pressed. */
			/*FALLTHROUGH*/
		case 0:
			QActive_post((QActive*)me, BUTTONS_UP_SIGNAL, 0);
			break;
		case 1:
			QActive_post((QActive*)me, BUTTON_1_DOWN_SIGNAL, 1);
			break;
		case 2:
			QActive_post((QActive*)me, BUTTON_2_DOWN_SIGNAL, 2);
			break;
		case 3:
			QActive_post((QActive*)me, BUTTON_3_DOWN_SIGNAL, 3);
			break;
		}
		break;

	case BUTTONS_UP_SIGNAL:
		me->whichButton = 0;
		me->repeatCount = 0;
		break;

	case BUTTON_1_DOWN_SIGNAL:
	case BUTTON_2_DOWN_SIGNAL:
	case BUTTON_3_DOWN_SIGNAL:
		button = (uint8_t)(Q_PAR(me));
		if (0 == me->whichButton) {
			me->whichButton = button;
			return Q_HANDLED();
		}
		if (button == me->whichButton) {
			/* The same button is still pressed, we're counting
			   ticks to see how long it's down. */
			me->repeatCount++;
			if (me->repeatCount >= 3) {
				return Q_TRAN(buttonDownState);
			} else {
				return Q_HANDLED();
			}
		}
		else {
			/* Ah nu!  A different button has been pressed while we
			   were counting ticks.  Beached az! */
			me->repeatCount = 1;
			me->whichButton = button;
			return Q_HANDLED();
		}
		break;
	}
	return Q_SUPER(&QHsm_top);
}


static QState buttonDownState(struct Buttons *me)
{
	uint8_t button;

	switch (Q_SIG(me)) {

	case BUTTONS_UP_SIGNAL:
		/* If we have been monitoring this button for less than the
		   long press period, send a single button press event.  If we
		   have been monitoring this button for longer, long press and
		   maybe repeat signals would have been sent from
		   buttonLongState() and buttonRepeatingState(). */
		switch (me->whichButton) {
		case 1:
			QActive_post(((QActive*)(&dclock)),
				     BUTTON_1_PRESS_SIGNAL, 0);
			break;
		case 2:
			QActive_post(((QActive*)(&dclock)),
				     BUTTON_2_PRESS_SIGNAL, 0);
			break;
		case 3:
			QActive_post(((QActive*)(&dclock)),
				     BUTTON_3_PRESS_SIGNAL, 0);
			break;
		}
		/* Return to the top state, as there are no buttons pressed. */
		return Q_TRAN(buttonsState);

	case BUTTON_1_DOWN_SIGNAL:
	case BUTTON_2_DOWN_SIGNAL:
	case BUTTON_3_DOWN_SIGNAL:
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
		} else {
			return Q_HANDLED();
		}
		break;

	case Q_EXIT_SIG:
		me->whichButton = 0;
		me->repeatCount = 0;
		break;
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
			QActive_post(((QActive*)(&dclock)),
				     BUTTON_1_LONG_PRESS_SIGNAL, 0);
			break;
		case 2:
			QActive_post(((QActive*)(&dclock)),
				     BUTTON_2_LONG_PRESS_SIGNAL, 0);
			break;
		case 3:
			QActive_post(((QActive*)(&dclock)),
				     BUTTON_3_LONG_PRESS_SIGNAL, 0);
			break;
		}
		me->repeatCount = 0;
		return Q_HANDLED();

	case BUTTONS_UP_SIGNAL:
		return Q_TRAN(buttonsState);

	case BUTTON_1_DOWN_SIGNAL:
	case BUTTON_2_DOWN_SIGNAL:
	case BUTTON_3_DOWN_SIGNAL:
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

	case Q_EXIT_SIG:
		me->whichButton = 0;
		me->repeatCount = 0;
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
		break;

	case BUTTONS_UP_SIGNAL:
		return Q_TRAN(buttonsState);

	case BUTTON_1_DOWN_SIGNAL:
	case BUTTON_2_DOWN_SIGNAL:
	case BUTTON_3_DOWN_SIGNAL:
		button = (uint8_t)(Q_PAR(me));
		if (button != me->whichButton) {
			/* Beached az! */
			return Q_TRAN(buttonsState);
		}
		me->repeatCount++;
		if (me->repeatCount >= REPEAT_PERIOD) {
			switch (me->whichButton) {
			case 1:
				QActive_post(((QActive*)(&dclock)),
					     BUTTON_1_REPEAT_SIGNAL, 0);
				break;
			case 2:
				QActive_post(((QActive*)(&dclock)),
					     BUTTON_2_REPEAT_SIGNAL, 0);
				break;
			case 3:
				QActive_post(((QActive*)(&dclock)),
					     BUTTON_3_REPEAT_SIGNAL, 0);
				break;
			}
			me->repeatCount = 0;
		}
		return Q_HANDLED();
	}
	return Q_SUPER(buttonLongState);
}

