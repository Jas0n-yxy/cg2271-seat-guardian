/*
 * sensors.c - Sensor drivers.
 *
 *  - Sound and touch: PORTA GPIO interrupts. The ISR only clears the flag and
 *    gives a binary semaphore (Lab 3 Part 6 pattern); debouncing and counting
 *    happen in SeatTask.
 *  - Ultrasonic HC-SR04: polled from SensorTask every SENSOR_PERIOD_MS.
 *
 * Meet-up #1 / #2 work is marked TODO.
 */
#include "board.h"
#include "fsl_debug_console.h"
#include "task.h"
#include "config.h"
#include "sensors.h"

SemaphoreHandle_t noiseSem;
SemaphoreHandle_t touchSem;

/* ---------------- Sound + touch: PORTA interrupts ---------------- */

static void initPortAInterrupts(void)
{
	NVIC_DisableIRQ(PORTA_IRQn);
	SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK;

	// GPIO inputs, like Lab 3 isr_poll.c.
	// Sound: interrupt on EITHER edge (IRQC = 0b1011), because sound modules differ
	// in polarity (some idle HIGH and pulse LOW on a clap). SeatTask debounces.
	// Touch: rising edge (IRQC = 0b1001); confirmed working on 13 Oct.
	PORTA->PCR[SOUND_PIN] = PORT_PCR_MUX(1) | PORT_PCR_IRQC(0b1011);
	PORTA->PCR[TOUCH_PIN] = PORT_PCR_MUX(1) | PORT_PCR_IRQC(0b1001);
	GPIOA->PDDR &= ~((1 << SOUND_PIN) | (1 << TOUCH_PIN));

	NVIC_SetPriority(PORTA_IRQn, 1);
	NVIC_ClearPendingIRQ(PORTA_IRQn);
	NVIC_EnableIRQ(PORTA_IRQn);
}

void PORTA_IRQHandler(void)
{
	BaseType_t hpw = pdFALSE;
	uint32_t flags = PORTA->ISFR;

	PORTA->ISFR = flags;				// write 1 to clear, before giving (see Lab 3 Part 6)
	NVIC_ClearPendingIRQ(PORTA_IRQn);

	if (flags & (1 << SOUND_PIN)) {
		xSemaphoreGiveFromISR(noiseSem, &hpw);
	}
	if (flags & (1 << TOUCH_PIN)) {
		xSemaphoreGiveFromISR(touchSem, &hpw);
	}

	portYIELD_FROM_ISR(hpw);
}

/* ---------------- Ultrasonic HC-SR04 ---------------- */

/*
 * Bring-up version: ECHO is read as a plain GPIO and timed with TPM1 running
 * free at 1 MHz (1 count = 1 us). SensorTask busy-waits for at most ~30 ms per
 * reading; higher-priority tasks and ISRs can still pre-empt it.
 * Week 11 TODO: switch ECHO to TPM1_CH0 input capture (PTE20 ALT3) with an
 * interrupt, so the task blocks instead of polling.
 */
#define US_TIMEOUT_US	30000	/* ~5 m round trip; HC-SR04 max range is ~4 m */

static inline uint16_t usNow(void)
{
	return (uint16_t)TPM1->CNT;
}

static inline uint16_t usSince(uint16_t start)
{
	return (uint16_t)(usNow() - start);	/* 16-bit wrap is fine below 65 ms */
}

static inline int echoHigh(void)
{
	return (GPIOE->PDIR >> US_ECHO_PIN) & 1;
}

static void initUltrasonic(void)
{
	SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK;

	// TRIG: GPIO output, idle low
	PORTE->PCR[US_TRIG_PIN] = PORT_PCR_MUX(1);
	GPIOE->PDDR |= (1 << US_TRIG_PIN);
	GPIOE->PCOR = (1 << US_TRIG_PIN);

	// ECHO: GPIO input (through the voltage divider)
	PORTE->PCR[US_ECHO_PIN] = PORT_PCR_MUX(1);
	GPIOE->PDDR &= ~(1 << US_ECHO_PIN);

	// TPM1 free-running at 1 MHz. Its clock source (SIM_SOPT2 TPMSRC = MCGIRCLK,
	// 8 MHz) is shared by all TPMs and is set up in actuators_init().
	SIM->SCGC6 |= SIM_SCGC6_TPM1_MASK;
	TPM1->SC = 0;					// stop while configuring
	TPM1->CNT = 0;
	TPM1->MOD = 0xFFFF;
	TPM1->SC = TPM_SC_PS(3) | TPM_SC_CMOD(1);	// 8 MHz / 8 = 1 MHz, start
}

int ultrasonic_read_cm(void)
{
	uint16_t t0;

	// Previous echo must have finished
	t0 = usNow();
	while (echoHigh()) {
		if (usSince(t0) > US_TIMEOUT_US)
			return -2;			// ECHO stuck high: check wiring
	}

	// 10 us trigger pulse (12 us for margin)
	GPIOE->PSOR = (1 << US_TRIG_PIN);
	t0 = usNow();
	while (usSince(t0) < 12) { }
	GPIOE->PCOR = (1 << US_TRIG_PIN);

	// Wait for ECHO to rise
	t0 = usNow();
	while (!echoHigh()) {
		if (usSince(t0) > US_TIMEOUT_US)
			return -1;			// no echo started: power, wiring or divider too weak
	}

	// Measure how long ECHO stays high
	t0 = usNow();
	while (echoHigh()) {
		if (usSince(t0) > US_TIMEOUT_US)
			return -1;			// nothing in range
	}
	uint16_t width = usSince(t0);

	return width / 58;				// sound: ~58 us per cm, there and back
}

/* ---------------- Init ---------------- */

void sensors_init(void)
{
	// Semaphores first so the ISR never gives a NULL handle
	noiseSem = xSemaphoreCreateBinary();
	touchSem = xSemaphoreCreateBinary();
	configASSERT(noiseSem != NULL && touchSem != NULL);

	initUltrasonic();
	initPortAInterrupts();
}
