#ifndef rtc_h_INCLUDED
#define rtc_h_INCLUDED

/* PCF8563 */
#define NXP_PCF8563_I2C_ADDR 0x51

/* Dallas DS3232 as on the Freetonics board. */
#define DALLAS_DS3232_I2C_ADDR 0b1101000

#define RTC_ADDR DALLAS_DS3232_I2C_ADDR

#endif
