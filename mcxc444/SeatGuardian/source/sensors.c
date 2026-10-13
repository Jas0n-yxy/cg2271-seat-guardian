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

	// GPIO, input, interrupt on rising edge (IRQC = 0b1001), like Lab 3 isr_poll.c
	// TODO (meet-up #2): check each module's digital output polarity and
	// change to falling edge (0b1010) if it pulls LOW when triggered.
	const int pins[] = { SOUND_PIN, TOUCH_PIN };
	for (int i = 0; i < 2; i++) {
		PORTA->PCR[pins[i]] = PORT_PCR_MUX(1) | PORT_PCR_IRQC(0b1001);
		GPIOA->PDDR &= ~(1 << pins[i]);
	}

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

static void initUltrasonic(void)
{
	SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK;

	// TRIG: GPIO output, idle low
	PORTE->PCR[US_TRIG_PIN] = PORT_PCR_MUX(1);
	GPIOE->PDDR |= (1 << US_TRIG_PIN);
	GPIOE->PCOR = (1 << US_TRIG_PIN);

	// TODO (meet-up #1): ECHO on PTE20 as TPM1_CH0 input capture (ALT3).
	//  1. Power HC-SR04 from 5 V; ECHO -> 1 kOhm -> PTE20, PTE20 -> 2 kOhm -> GND
	//     (5 V echo would damage the 3.3 V pin).
	//  2. Enable TPM1 clock (SIM->SCGC6), choose a prescaler so one count = 1 us
	//     or similar, set CH0 to capture on both edges (ELSA + ELSB).
	//  3. Alternative if input capture is awkward: plain GPIO input and read
	//     TPM1->CNT on each edge from a PORT interrupt (PORTE has no IRQ on
	//     MCXC444, so the echo pin would have to move to PORTA/PORTC/PORTD).
}

int ultrasonic_read_cm(void)
{
	// 10 us trigger pulse. At 48 MHz a short loop is enough; check on a scope
	// or just test the result.
	GPIOE->PSOR = (1 << US_TRIG_PIN);
	for (volatile int i = 0; i < 120; i++) { }
	GPIOE->PCOR = (1 << US_TRIG_PIN);

	// TODO (meet-up #1): measure the ECHO high time in microseconds,
	// time out after ~30 ms (no object), then:
	//     distance_cm = echo_us / 58;
	return -1;
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
