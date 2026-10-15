/*
 * main.c - Seat Guardian, MCXC444 side (FreeRTOS).
 *
 * Tasks (see docs/Seat_Guardian_Architecture.html):
 *   SeatTask     P3  seat state machine; reacts to sensor readings and ISR semaphores
 *   SensorTask   P2  polls the HC-SR04 every SENSOR_PERIOD_MS -> sensorQueue
 *   ActuatorTask P2  commands from the ESP32 (rxQueue) -> LED / buzzer
 *   UartTxTask   P1  sends the seat status to the ESP32 every STATUS_PERIOD_MS
 *
 * Project setup in MCUXpresso: Operating Systems -> RTOS, Others -> RTOS ->
 * Heap: heap 4 ONLY, Project Template -> RTOS, Redirect printf to UART.
 * In FreeRTOSConfig_Gen.h set configUSE_TIME_SLICING to 1 (project requirement).
 *
 * Debug console: COM port of the MCU-Link at 115200.
 */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "config.h"
#include "uart_link.h"
#include "sensors.h"
#include "actuators.h"
#include "packet.h"

#if (configUSE_PREEMPTION != 1) || (configUSE_TIME_SLICING != 1)
#warning "Project requires pre-emption and time slicing: set both to 1 in FreeRTOSConfig_Gen.h"
#endif

/* ---------------- Shared seat state (protected by stateMutex) ---------------- */
typedef struct {
	SeatStateId state;
	int distance_cm;
	int noise_events;	/* since last status message */
	int touch_events;	/* since last status message */
	TickType_t left_at;	/* tick when the occupant left */
} SeatState;

static SeatState seat = { SEAT_FREE, -1, 0, 0, 0 };
static SemaphoreHandle_t stateMutex;
static QueueHandle_t sensorQueue;	/* int distance_cm, SensorTask -> SeatTask */

static const char *stateName(SeatStateId s)
{
	static const char *names[] = { "FREE", "OCCUPIED", "AWAY", "ON_HOLD", "ABANDONED" };
	return (s <= SEAT_ABANDONED) ? names[s] : "?";
}

static void showState(SeatStateId s)
{
	switch (s) {
	case SEAT_FREE:      led_set_rgb(0, 100, 0);  break;	/* green */
	case SEAT_OCCUPIED:  led_set_rgb(0, 0, 100);  break;	/* blue */
	case SEAT_AWAY:
	case SEAT_ON_HOLD:   led_set_rgb(100, 40, 0); break;	/* amber */
	case SEAT_ABANDONED: led_set_rgb(100, 0, 0);  break;	/* red */
	}
}

/* ---------------- Tasks ---------------- */

static void sensorTask(void *p)
{
	TickType_t last = xTaskGetTickCount();
	while (1) {
		int cm = ultrasonic_read_cm();
#if DEBUG_PINS
		PRINTF("[US] %d cm\r\n", cm);	/* -1 = no echo, -2 = echo stuck high */
#endif
		xQueueOverwrite(sensorQueue, &cm);	/* SeatTask only needs the latest reading */
		vTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
	}
}

static void seatTask(void *p)
{
	int cm;
	while (1) {
		/* Wake for a new distance reading, or every 100 ms to check the semaphores */
		BaseType_t gotReading = xQueueReceive(sensorQueue, &cm, pdMS_TO_TICKS(100));

		if (xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE)
			continue;

		/* TODO (meet-up #2): debounce, e.g. ignore noise edges within 200 ms of the last one */
		if (xSemaphoreTake(noiseSem, 0) == pdTRUE) {
			seat.noise_events++;
			PRINTF("[EVT] noise\r\n");
		}
		/* TODO (meet-up #2): tap vs long hold */
		if (xSemaphoreTake(touchSem, 0) == pdTRUE) {
			seat.touch_events++;
			PRINTF("[EVT] touch\r\n");
			if (seat.state == SEAT_AWAY || seat.state == SEAT_ABANDONED)
				seat.state = SEAT_OCCUPIED;		/* "I'm back" */
		}

		SeatStateId before = seat.state;
		if (gotReading == pdTRUE) {
			seat.distance_cm = cm;
			int present = (cm > 0 && cm < OCCUPIED_CM);

			/* Week 11 TODO: full state machine incl. ON_HOLD from ESP32 commands */
			switch (seat.state) {
			case SEAT_FREE:
				if (present) seat.state = SEAT_OCCUPIED;
				break;
			case SEAT_OCCUPIED:
				if (!present) {
					seat.state = SEAT_AWAY;
					seat.left_at = xTaskGetTickCount();
				}
				break;
			case SEAT_AWAY:
				if (present)
					seat.state = SEAT_OCCUPIED;
				else if (xTaskGetTickCount() - seat.left_at > pdMS_TO_TICKS(AWAY_TIMEOUT_S * 1000))
					seat.state = SEAT_ABANDONED;
				break;
			case SEAT_ON_HOLD:
			case SEAT_ABANDONED:
				if (present) seat.state = SEAT_OCCUPIED;
				break;
			}
		}
		SeatStateId after = seat.state;
		xSemaphoreGive(stateMutex);

		if (after != before) {
			PRINTF("[SEAT] %s -> %s\r\n", stateName(before), stateName(after));
			showState(after);
		}
	}
}

/* Commands from the ESP32. Weeks 9-10 text format for testing:
 *   LED,r,g,b     (0-100 each)
 *   BUZZ,ms
 * Week 11 TODO: switch to CommandPayload frames from packet.h */
static void actuatorTask(void *p)
{
	TMessage msg;
	int r, g, b, ms;

	while (1) {
		if (xQueueReceive(rxQueue, &msg, portMAX_DELAY) != pdTRUE)
			continue;

		PRINTF("[RX] %s\r\n", msg.message);
		if (sscanf(msg.message, "LED,%d,%d,%d", &r, &g, &b) == 3) {
			led_set_rgb(r, g, b);
		} else if (sscanf(msg.message, "BUZZ,%d", &ms) == 1) {
			buzzer_on();
			vTaskDelay(pdMS_TO_TICKS(ms));
			buzzer_off();
		}
	}
}

static void uartTxTask(void *p)
{
	char line[MAX_MSG_LEN];
	SeatState copy;

	while (1) {
		if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
			copy = seat;
			seat.noise_events = 0;
			seat.touch_events = 0;
			xSemaphoreGive(stateMutex);
		}

		snprintf(line, sizeof(line), "STATUS,%s,%d,%d,%d",
				stateName(copy.state), copy.distance_cm, copy.noise_events, copy.touch_events);
		PRINTF("[TX] %s\r\n", line);	/* the terminal needs \r\n */
		strncat(line, "\n", sizeof(line) - strlen(line) - 1);	/* the ESP32 splits lines on \n */
		uart_link_send(line);
#if DEBUG_PINS
		/* Raw input levels: if a level never changes, check the wiring or the module */
		PRINTF("[PIN] sound=%d touch=%d\r\n",
				(int)((GPIOA->PDIR >> SOUND_PIN) & 1), (int)((GPIOA->PDIR >> TOUCH_PIN) & 1));
#endif

		vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));
	}
}

/* ---------------- main ---------------- */

int main(void)
{
	BOARD_InitBootPins();
	BOARD_InitBootClocks();
	BOARD_InitBootPeripherals();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
	BOARD_InitDebugConsole();
#endif

	stateMutex = xSemaphoreCreateMutex();
	sensorQueue = xQueueCreate(1, sizeof(int));
	configASSERT(stateMutex != NULL && sensorQueue != NULL);

	/* Actuators first: the LED PWM setup touches the MCG, and UART2 reads the bus clock after */
	actuators_init();
	sensors_init();
	uart_link_init(UART_BAUD);
	showState(seat.state);

	PRINTF("Seat Guardian (MCXC444) started\r\n");

	xTaskCreate(seatTask,     "seat",     configMINIMAL_STACK_SIZE + 250, NULL, PRIO_SEAT,     NULL);
	xTaskCreate(sensorTask,   "sensor",   configMINIMAL_STACK_SIZE + 100, NULL, PRIO_SENSOR,   NULL);
	xTaskCreate(actuatorTask, "actuator", configMINIMAL_STACK_SIZE + 250, NULL, PRIO_ACTUATOR, NULL);
	xTaskCreate(uartTxTask,   "uart_tx",  configMINIMAL_STACK_SIZE + 250, NULL, PRIO_UART_TX,  NULL);

	vTaskStartScheduler();

	while (1) {
		__asm volatile ("nop");
	}
	return 0;
}
