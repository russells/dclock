#include "twi-status.h"
#include "qpn_port.h"


static const char status0x08[] Q_ROM = "A START condition has been transmitted";
static const char status0x10[] Q_ROM = "A repeated START condition has been transmitted";
static const char status0x18[] Q_ROM = "SLA+W has been transmitted; ACK has been received";
static const char status0x20[] Q_ROM = "SLA+W has been transmitted; NOT ACK has been received";
static const char status0x28[] Q_ROM = "Data byte has been transmitted; ACK has been received";
static const char status0x30[] Q_ROM = "Data byte has been transmitted; NOT ACK has been received";
static const char status0x38[] Q_ROM = "Arbitration lost in SLA+W or data bytes OR Arbitration lost in SLA+R or NOT ACK bit";
static const char status0x40[] Q_ROM = "SLA+R has been transmitted; ACK has been received";
static const char status0x48[] Q_ROM = "SLA+R has been transmitted; NOT ACK has been received";
static const char status0x50[] Q_ROM = "Data byte has been received; ACK has been returned";
static const char status0x58[] Q_ROM = "Data byte has been received; NOT ACK has been returned";
static const char status0x60[] Q_ROM = "Own SLA+W has been received; ACK has been returned";
static const char status0x68[] Q_ROM = "Arbitration lost in SLA+R/W as master; own SLA+W has been received; ACK has been returned";
static const char status0x70[] Q_ROM = "General call address has been received; ACK has been returned";
static const char status0x78[] Q_ROM = "Arbitration lost in SLA+R/W as master; General call address has been received; ACK has been returned";
static const char status0x80[] Q_ROM = "Previously addressed with own SLA+W; data has been received; ACK has been returned";
static const char status0x88[] Q_ROM = "Previously addressed with own SLA+W; data has been received; NOT ACK has been returned";
static const char status0x90[] Q_ROM = "Previously addressed with general call; data has been re- ceived; ACK has been returned";
static const char status0x98[] Q_ROM = "Previously addressed with general call; data has been received; NOT ACK has been returned";
static const char status0xA0[] Q_ROM = "A STOP condition or repeated START condition has been received while still addressed as slave";
static const char status0xA8[] Q_ROM = "Own SLA+R has been received; ACK has been returned";
static const char status0xB0[] Q_ROM = "Arbitration lost in SLA+R/W as master; own SLA+R has been received; ACK has been returned";
static const char status0xB8[] Q_ROM = "Data byte in TWDR has been transmitted; ACK has been received";
static const char status0xC0[] Q_ROM = "Data byte in TWDR has been transmitted; NOT ACK has been received";
static const char status0xC8[] Q_ROM = "Last data byte in TWDR has been transmitted (TWEA = “0”); ACK has been received";

static PGM_P const statuses[] PROGMEM = {
	status0x08, status0x10, status0x18, status0x20,
	status0x28, status0x30, status0x38, status0x40,
	status0x48, status0x50, status0x58, status0x60,
	status0x68, status0x70, status0x78, status0x80,
	status0x88, status0x90, status0x98, status0xA0,
	status0xA8, status0xB0, status0xB8, status0xC0,
	status0xC8,
};


Q_ROM const char Q_ROM * twi_status_string(uint8_t status)
{
	static Q_ROM const char success[] = "Success";
	static Q_ROM const char unknown[] = "Unknown TWI status";

	status &= 0xf8;		/* Ensure status is masked correctly. */
	if (status == 0xf8) {
		return success;
	}
	if (status < TWI_08_START_SENT
	    || status > TWI_C8_ST_LASTDATA_TX_NACK_RX) {
		return unknown;
	}
	status >>= 3;
	status -= 1;
	return (const char *) Q_ROM_PTR(statuses[status]);
}
