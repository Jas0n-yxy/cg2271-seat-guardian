/*
 * sensors.h - Ultrasonic (polled), sound and touch (interrupt-driven) sensors.
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"

/* Given by PORTA_IRQHandler, taken by SeatTask */
extern SemaphoreHandle_t noiseSem;
extern SemaphoreHandle_t touchSem;

/* Creates the semaphores, then configures the pins and enables the interrupts */
void sensors_init(void);

/* Triggers the HC-SR04 and returns the distance in cm, or -1 if no echo.
 * Called from SensorTask only. */
int ultrasonic_read_cm(void);

#endif /* SENSORS_H */
