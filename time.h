#ifndef time_h_INCLUDED
#define time_h_INCLUDED

#include <stdint.h>

/**
 * Contains the three parts of a normal time - hours, minutes, and seconds.
 *
 * This gets passed around when we are getting and setting the time.
 *
 * Why pass around a struct, you ask?  Madness!, you say.  But I've
 * disassembled the code that does this and it's not that much different to
 * passing around a uint32_t, which we do elsewhere.
 */
struct NormalTime {
	uint8_t h;
	uint8_t m;
	uint8_t s;
	uint8_t pad;
};

struct NormalTime decimal_to_normal(uint32_t dtime);
uint32_t normal_to_decimal(struct NormalTime ntime);

void decimal_to_dtimes(uint32_t sec, uint8_t *times);
uint32_t dtimes_to_decimal(uint8_t *times);

uint8_t inc_hours(uint8_t h);
uint8_t dec_hours(uint8_t h);
uint8_t inc_minutes(uint8_t h);
uint8_t dec_minutes(uint8_t h);
uint8_t inc_seconds(uint8_t h);
uint8_t dec_seconds(uint8_t h);

void print_normal_time(struct NormalTime nt);
void print_decimal_time(uint32_t dt);

uint32_t normal_day_seconds(struct NormalTime *ntp);

/**
 * Convert a NormalTime to its integer representation.
 *
 * The parameter that is passed around in QP-nano events can be a 32 bit
 * integer.  We want to pass around a four byte struct NormalTime, and this
 * function (along with ntp2it(), it2nt(), and it2ntp()) helps us to convert
 * between the 32 bit integer and the struct.
 */
inline uint32_t nt2it(struct NormalTime nt)
{
	uint32_t *p = (uint32_t *)(&nt);
	return *p;
}

/**
 * Convert a pointer to NormalTime to its integer representation.
 *
 * @see nt2it()
 */
inline uint32_t ntp2it(struct NormalTime *nt)
{
	uint32_t *p = (uint32_t *)nt;
	return *p;
}

/**
 * Get a NormalTime from its integer representation.
 *
 * @see nt2it()
 */
inline struct NormalTime it2nt(uint32_t it)
{
	struct NormalTime *ntp = (struct NormalTime *)(&it);
	return *ntp;
}

/**
 * Get a pointer to NormalTime from its integer representation.
 *
 * @see nt2it()
 */
inline struct NormalTime *it2ntp(uint32_t it)
{
	struct NormalTime *ntp = (struct NormalTime *)(&it);
	return ntp;
}


#define NORMAL_MODE 'N'
#define DECIMAL_MODE 'D'

uint8_t get_time_mode(void);
void set_time_mode(uint8_t mode);
void toggle_time_mode(void);

#endif
