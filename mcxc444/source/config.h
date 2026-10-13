/*
 * config.h - Pin plan, task priorities and timings for the MCXC444.
 *
 * Pins marked "proposed" have not been tested yet. Check them against the
 * MCXC444 reference manual (Signal Multiplexing chapter) and the board
 * schematic before wiring.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ---------- UART2 link to ESP32 (from Lab 3, tested) ---------- */
#define UART_BAUD		9600
#define UART_TX_PTE22		22	/* PTE22 ALT4 -> ESP32 GPIO2 (RX) */
#define UART_RX_PTE23		23	/* PTE23 ALT4 <- ESP32 GPIO1 (TX) */

/* ---------- Onboard RGB LED, TPM0 PWM (from Lab 2) ---------- */
#define RED_PIN			31	/* PTE31, TPM0_CH4 */
#define GREEN_PIN		5	/* PTD5,  TPM0_CH5 */
#define BLUE_PIN		29	/* PTE29, TPM0_CH2 */

/* ---------- Sensors (proposed) ---------- */
#define SOUND_PIN		12	/* PTA12 (header D3), PORTA interrupt, sound DO */
#define TOUCH_PIN		13	/* PTA13 (header D8), PORTA interrupt, touch DO */
#define US_TRIG_PIN		21	/* PTE21, GPIO output, HC-SR04 TRIG */
#define US_ECHO_PIN		20	/* PTE20, TPM1_CH0 input capture, HC-SR04 ECHO via 1k/2k divider */

/* ---------- Buzzer (proposed) ---------- */
#define BUZZER_PIN		2	/* PTB2 (header A2); TPM2_CH0 if a passive buzzer needs PWM */

/* ---------- Task priorities (configMAX_PRIORITIES = 5) ---------- */
#define PRIO_SEAT		3	/* seat state machine, reacts to ISR events */
#define PRIO_SENSOR		2
#define PRIO_ACTUATOR		2
#define PRIO_UART_TX		1

/* ---------- Timing ---------- */
#define SENSOR_PERIOD_MS	500
#define STATUS_PERIOD_MS	1000
#define OCCUPIED_CM		60	/* closer than this = someone at the seat */
#define AWAY_TIMEOUT_S		60	/* short for the demo; real library would use ~30 min */

#endif /* CONFIG_H */
