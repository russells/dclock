#ifndef time_arithmetic_h_INCLUDED
#define time_arithmetic_h_INCLUDED

#include <stdint.h>

/* The caller must include qpn_port.h and declare Q_DEFINE_THIS_FILE before
   this file is included. */

#define MAKE_MODE_INC_DEC(name,max)			\
	inline static uint8_t inc_##name(uint8_t v)	\
	{						\
		Q_ASSERT( v <= max );			\
		if (max == v) {				\
			return 0;			\
		} else {				\
			return v+1;			\
		}					\
	}						\
							\
	inline static uint8_t dec_##name(uint8_t v)	\
	{						\
		Q_ASSERT( v <= max );			\
		if (0 == v) {				\
			return max;			\
		} else {				\
			return v-1;			\
		}					\
	}

MAKE_MODE_INC_DEC(decimal_hours,9);
MAKE_MODE_INC_DEC(decimal_minutes,99);
MAKE_MODE_INC_DEC(decimal_seconds,99);
MAKE_MODE_INC_DEC(normal_hours,23);
MAKE_MODE_INC_DEC(normal_minutes,59);
MAKE_MODE_INC_DEC(normal_seconds,59);

#endif
