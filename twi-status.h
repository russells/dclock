#ifndef twi_status_h_INCLUDED
#define twi_status_h_INCLUDED

#include "qpn_port.h"

/* General status replies. */
#define TWI_08_START_SENT             0x08
#define TWI_10_REPEATED_START_SENT    0x10
#define TWI_38_ARBITRATION_LOST       0x38

/* Master transmitter mode. */
#define TWI_18_MT_SLA_W_TX_ACK_RX     0x18
#define TWI_20_MT_SLA_W_TX_NACK_RX    0x20
#define TWI_28_MT_DATA_TX_ACK_RX      0x28
#define TWI_30_MT_DATA_TX_NACK_RX     0x30

/* Master receiver mode. */
#define TWI_40_MR_SLA_R_TX_ACK_RX     0x40
#define TWI_48_MR_SLA_R_TX_NACK_RX    0x48
#define TWI_50_MR_DATA_RX_ACK_TX      0x50
#define TWI_58_MR_DATA_RX_NACK_TX     0x58

/* Slave receiver mode. */
#define TWI_60_SR_SLA_W_RX_ACK_TX     0x60
#define TWI_68_M_SR_SLA_W_RX_ACK_TX   0x68 /* We tried to be master, lost
					     arbitration, received SLA+W,
					     returned ACK. */
#define TWI_70_SR_GC_RX_ACK_TX        0x70 /* Received general call, returned
					     ACK. */
#define TWI_78_M_SR_GC_RX_ACK_TX      0x78
#define TWI_80_SR_DATA_RX_ACK_TX      0x80
#define TWI_88_SR_DATA_RX_NACK_TX     0x88
#define TWI_90_SR_GC_DATA_RX_ACK_RX   0x90
#define TWI_98_SR_GC_DATA_RX_NACK_TX  0x98
#define TWI_A0_SR_STOP_OR_RSTART_RX   0xA0

/* Slave transmitter mode. */
#define TWI_A8_ST_SLA_R_RX_ACK_TX     0xA8
#define TWI_B0_M_ST_SLA_R_RX_ACK_TX   0xB0 /* We tried to be master, lost
					     arbitration, received SLA+R,
					     returned ACK. */
#define TWI_B8_ST_DATA_TX_ACK_RX      0xB8
#define TWI_C0_ST_DATA_TX_NACK_RX     0xC0
#define TWI_C8_ST_LASTDATA_TX_NACK_RX 0xC8


Q_ROM const char * Q_ROM twi_status_string(uint8_t status);

#endif
