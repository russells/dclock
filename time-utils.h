#ifndef time_utils_h_INCLUDED
#define time_utils_h_INCLUDED

#include <stdint.h>

uint32_t rtc_time_to_decimal_time(const uint8_t *bytes);
void decimal_time_to_rtc_time(uint32_t dtime, uint8_t *bytes);

void decimal_time_to_dtimes(uint32_t sec, uint8_t *times);
uint32_t dtimes_to_decimal_time(uint8_t *times);

uint8_t inc_hours(uint8_t h);
uint8_t dec_hours(uint8_t h);
uint8_t inc_minutes(uint8_t h);
uint8_t dec_minutes(uint8_t h);
uint8_t inc_seconds(uint8_t h);
uint8_t dec_seconds(uint8_t h);


#endif
