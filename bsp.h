/*****************************************************************************
* Product: DPP example
* Last Updated for Version: 4.0.00
* Date of the Last Update:  Apr 07, 2008
*
*                    Q u a n t u m     L e a P s
*                    ---------------------------
*                    innovating embedded systems
*
* Copyright (C) 2002-2008 Quantum Leaps, LLC. All rights reserved.
*
* This software may be distributed and modified under the terms of the GNU
* General Public License version 2 (GPL) as published by the Free Software
* Foundation and appearing in the file GPL.TXT included in the packaging of
* this file. Please note that GPL Section 2[b] requires that all works based
* on this software must also be made publicly available under the terms of
* the GPL ("Copyleft").
*
* Alternatively, this software may be distributed and modified under the
* terms of Quantum Leaps commercial licenses, which expressly supersede
* the GPL and are specifically designed for licensees interested in
* retaining the proprietary status of their code.
*
* Contact information:
* Quantum Leaps Web site:  http://www.quantum-leaps.com
* e-mail:                  info@quantum-leaps.com
*****************************************************************************/
#ifndef bsp_h_INCLUDED
#define bsp_h_INCLUDED

#include "dclock.h"

/* Must match the ticks per second generated by the AVR code. */
#define BSP_TICKS_PER_SECOND 32

void BSP_startmain();		/* Code to put right at the start of main() */

void BSP_init(void);

void BSP_QF_onStartup(void);

uint8_t BSP_getButton(void);

void BSP_lcd_init(uint8_t pwm);
void BSP_lcd_pwm(uint8_t pwm);
void BSP_lcd_pwm_on(void);
void BSP_lcd_pwm_off(void);

void BSP_leds(uint8_t *data);
void BSP_buzzer_on(void);
void BSP_buzzer_off(void);

void BSP_enable_rtc_interrupt(void);

void BSP_watchdog(void);


void BSP_set_decimal_32_counter(uint8_t dc);

#endif	/* bsp_h_INCLUDED */
