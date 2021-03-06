#ifndef buttons_h_INCLUDED
#define buttons_h_INCLUDED

#include "dclock.h"
#include "qpn_port.h"


/**
 * @brief Remember the state of the buttons.
 *
 * One of these objects handles all of the buttons.  We can handle only one
 * button down at any one time.  Once one button is pressed, the other buttons
 * are ignored until that button is released.
 */
struct Buttons {
	QActive super;
	/**
	 * Which button is down.
	 */
	uint8_t whichButton;
	/**
	 * How long (20ths of a second) has this button been held down.  Must
	 * be set to 0 each time a new button is pressed.
	 */
	uint8_t repeatCount;
	/**
	 * Set true when we are able to receive signals.
	 */
	uint8_t ready;
};


extern struct Buttons buttons;


void buttons_ctor(void);


#endif
