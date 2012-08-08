#ifndef timedisplay_h_INCLUDED
#define timedisplay_h_INCLUDED

#include "qpn_port.h"

struct TimeDisplay {
	QActive super;
	uint8_t ready;
};

extern struct TimeDisplay timedisplay;

void timedisplay_ctor(void);

#endif
