/*
 * uart_link.h - Interrupt-driven UART2 link to the ESP32 (reused from Lab 3).
 */
#ifndef UART_LINK_H
#define UART_LINK_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"

#define MAX_MSG_LEN	64

typedef struct {
	char message[MAX_MSG_LEN];
} TMessage;

/* Lines received from the ESP32 ('\n'-terminated, '\r' stripped) */
extern QueueHandle_t rxQueue;

/* Link statistics, handy when debugging wiring */
extern volatile uint32_t statLines;
extern volatile uint32_t statUartErrors;
extern volatile uint32_t statQueueFull;

/* Creates rxQueue and the TX mutex, then enables UART2 and its interrupt */
void uart_link_init(uint32_t baud_rate);

/* Sends one line; blocks (without busy-waiting) until the previous line is out.
 * Safe to call from several tasks. Must not be called from an ISR. */
void uart_link_send(const char *line);

#endif /* UART_LINK_H */
