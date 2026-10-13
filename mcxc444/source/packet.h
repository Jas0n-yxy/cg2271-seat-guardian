/*
 * packet.h - Board-to-board frame format shared by the MCXC444 and the ESP32.
 *
 * Frame: [0xAA][type][len][payload (len bytes)][xor]
 *   xor = type ^ len ^ payload[0] ^ ... ^ payload[len-1]
 *
 * This file is the single source of truth. Copies live in
 * mcxc444/source/packet.h and esp32/seat_guardian/packet.h; keep all three identical.
 *
 * Status: defined for Week 11 (packet protocol). Weeks 9-10 still use the
 * newline-terminated text messages from Lab 3.
 */
#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#define PKT_START        0xAA
#define PKT_MAX_PAYLOAD  32

typedef enum {
	PKT_STATUS  = 0x01,	/* MCXC444 -> ESP32: seat state + readings */
	PKT_COMMAND = 0x02,	/* ESP32 -> MCXC444: action for LED / buzzer / timers */
	PKT_ACK     = 0x03,
	PKT_NAK     = 0x04,
} PacketType;

/* Seat states, same order on both boards */
typedef enum {
	SEAT_FREE = 0,
	SEAT_OCCUPIED,
	SEAT_AWAY,
	SEAT_ON_HOLD,
	SEAT_ABANDONED,
} SeatStateId;

/* Actions returned by the LLM / Telegram assistant */
typedef enum {
	ACT_NONE = 0,
	ACT_FREE,
	ACT_WARN,
	ACT_RESERVE,
	ACT_HOLD,	/* hold_min carries the minutes */
} SeatAction;

typedef struct __attribute__((packed)) {
	uint8_t  state;		/* SeatStateId */
	uint16_t distance_cm;
	uint8_t  noise_events;	/* since last status */
	uint8_t  touch_events;	/* since last status */
	uint16_t away_s;	/* seconds since the occupant left */
} StatusPayload;

typedef struct __attribute__((packed)) {
	uint8_t  action;	/* SeatAction */
	uint8_t  hold_min;
	uint16_t buzz_ms;
} CommandPayload;

static inline uint8_t pkt_checksum(uint8_t type, uint8_t len, const uint8_t *payload)
{
	uint8_t x = type ^ len;
	for (uint8_t i = 0; i < len; i++)
		x ^= payload[i];
	return x;
}

#endif /* PACKET_H */
