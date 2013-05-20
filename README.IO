Arduino and ATmega328p I/O pin allocation for the decimal clock.

Arduino		Atmega328p		Function

D0,RX		PD0/RXD/PCINT16		Serial IO
D1,TX		PD1/TXD/PCINT17		Serial IO
D2,INT0		PD2/INT0/PCINT18	RTC interrupt
D3,INT1,PWM	PD3/OC2B/INT1/PCINT19	LCD brightness
D4		PD4/XDK/T0/PCINT20	LCD D4
D5,PWM		PD5/OC0B/T1/PCINT21	LCD D5
D6,PWM		PD6/OC0A/AIN0/PCINT22	LCD D6
D7		PD7/AIN1/PCINT23	LCD D7
D8		PB0/CLKO/ICP1/PCINT0	LCD RS pin
D9,PWM		PB1/OC1A/PCINT1		LCD Enable pin
D10,PWM		PB2/SS/OC1B/PCINT2	Buzzer
D11,PWM		PB3/MOSI/OC2A/PCINT3	Programming
D12		PB4/MISO/PCINT4		Programming
D13,LED		PB5/SCK/PCINT5		Programming
A0		PC0/ADC0/PCINT8		Buttons
A1		PC1/ADC1/PCINT9		LED clock
A2		PC2/ADC2/PCINT10	LED data
A3		PC3/ADC3/PCINT11	Toggle pin (debugging)
A4		PC4/ADC4/SDA/PCINT12	RTC
A5		PC5/ADC5/SCL/PCINT13	RTC



Teensy pin allocation for the decimal clock.

Teensy	AT90USB1286			Function

1	GND				GND
2	PB7/PCINT7/OC0A/OC1C
3	PD0/OC0B/SCL/INT0
4	PD1/OC2B/SDA/INT1
5	PD2/RXD1/INT2
6	PD3/TXD1/INT3
7	PD4/ICP1
8	PD5/XCK1
9	PD6/T1
10	PD7/T0
11	PE0/-WR
12	PE1/-RD
13	PC0/A8
14	PC1/A9
15	PC2/A10
16	PC3/A11/T3
17	PC4/A12/OC3C
18	PC5/A13/OC3B
19	PC6/A14/OC3A
20	PC7/A15/IC3/CLKO
21	PF7/ADC7/TDI
22	PF6/ADC6/TDO
23	PF5/ADC5/TMS
24	PF4/ADC4/TCK
25	PF3/ADC3
26	PF2/ADC2
27	PF1/ADC1
28	PF0/ADC0
29	AREF
30	GND
31	PE6/INT6/AIN0
32	PE7/INT7/AIN1/UVCON
33	PB0/SS/PCINT0
34	PB1/PCINT1/SCLK
35	PB2/PDI/PCINT2/MOSI
36	PB3/PDO/PCINT2/MISO
37	PB4/PCINT4/OC2A
38	PB5/PCINT5/OC1A
39	PB6/PCINT6/OC1B
40	VCC
Interior
IA1	PE5/INT5/TOSC2
IA2	PE4/INT4/TOSC1
Interior
IB1	PA4/AD4
IB2	PA5/AD5
IB3	PA6/AD6
IB4	PA7/AD7
IB5	PA3/AD3
IB6	PA2/AD2
IB7	PA1/AD1
IB8	PA0/AD0
IB9	PE2/ALE
Bottom
B1	VCC
B2	GND
B3	RESET


Teensy outline

Note:
- PE4 and PE5 are closer than 0.1".
- PE3/UID does not appear.
- Roughly to scale.  0.1" is two chars high and four wide:
    +---+
    |   |
    +---+


            +-----------------------+
            |        USBCON         |
        GND .                       . VCC
            |                       |
        PB7 .                       . PB6
            |                       |
        PD0 .                       . PB5
            |                       |
        PD1 .                       . PB4
            |                       |
        PD2 .                       . PB3
            |                       |
        PD3 .                       . PB2
            |                       |
        PD4 .      PE4 . . PE5      . PB1
            |                       |
        PD5 .                       . PB0
            |                       |
        PD6 .                       . PE7
            |                       |
        PD7 .                       . PE6
            |                       |
        PE0 .                       . GND
            |                       |
        PE1 .                       . AREF
            |                       |
        PC0 .                       . PF0
            |                       |
        PC1 .        PA4.   .PA0    . PF1
            |                       |
        PC2 .        PA5.   .PA1    . PF2
            |                       |
        PC3 .        PA6.   .PA2    . PF3
            |                       |
        PC4 .    PE2.   .   .PA3    . PF4
            |          PA7          |
        PC5 .                       . PF5
            |                       |
        PC6 .                       . PF6
            |                       |
        PC7 .                       . PF7
            +-------.---.---.-------+
                   VCC  GND RST
