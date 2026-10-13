/*
 * uart_link.c - Interrupt-driven UART2 link to the ESP32.
 *
 * Reused from our Lab 3 demo (2271DEMO_blink_debug.c):
 *  - RX: ISR collects bytes into a line and posts it to rxQueue with xQueueSendFromISR
 *  - TX: sender fills send_buffer and enables TIE; the ISR sends one byte per interrupt
 * Added for the project: a TX mutex so several tasks can send safely.
 *
 * Week 11 TODO: replace line framing with the binary frames in packet.h
 * (start byte, length, XOR checksum, ACK/NAK).
 */
#include <string.h>
#include "board.h"
#include "fsl_debug_console.h"
#include "semphr.h"
#include "config.h"
#include "uart_link.h"

#define UART2_INT_PRIO	128
#define QLEN		5

QueueHandle_t rxQueue;
static SemaphoreHandle_t txMutex;
static char send_buffer[MAX_MSG_LEN];

volatile uint32_t statLines = 0;
volatile uint32_t statUartErrors = 0;
volatile uint32_t statQueueFull = 0;

void uart_link_init(uint32_t baud_rate)
{
	// Create the queue first so the ISR never uses a NULL queue
	rxQueue = xQueueCreate(QLEN, sizeof(TMessage));
	txMutex = xSemaphoreCreateMutex();
	configASSERT(rxQueue != NULL && txMutex != NULL);

	NVIC_DisableIRQ(UART2_FLEXIO_IRQn);

	SIM->SCGC4 |= SIM_SCGC4_UART2_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK;

	UART2->C2 &= ~(UART_C2_TE_MASK | UART_C2_RE_MASK);

	// PTE22 = UART2_TX, PTE23 = UART2_RX (ALT4)
	PORTE->PCR[UART_TX_PTE22] &= ~PORT_PCR_MUX_MASK;
	PORTE->PCR[UART_TX_PTE22] |= PORT_PCR_MUX(4);
	PORTE->PCR[UART_RX_PTE23] &= ~PORT_PCR_MUX_MASK;
	PORTE->PCR[UART_RX_PTE23] |= PORT_PCR_MUX(4);

	// Baud rate with integer rounding
	uint32_t bus_clk = CLOCK_GetBusClkFreq();
	uint32_t sbr = (bus_clk + (baud_rate * 8)) / (baud_rate * 16);
	UART2->BDH &= ~UART_BDH_SBR_MASK;		// BDH first!
	UART2->BDH |= ((sbr >> 8) & UART_BDH_SBR_MASK);
	UART2->BDL = (uint8_t)(sbr & 0xFF);

	// 8N1, no loopback
	UART2->C1 &= ~(UART_C1_LOOPS_MASK | UART_C1_RSRC_MASK |
			UART_C1_PE_MASK | UART_C1_M_MASK);

	// Receiver + receive interrupt on. Transmitter stays off until we send.
	UART2->C2 |= (UART_C2_RIE_MASK | UART_C2_RE_MASK);

	NVIC_SetPriority(UART2_FLEXIO_IRQn, UART2_INT_PRIO);
	NVIC_ClearPendingIRQ(UART2_FLEXIO_IRQn);
	NVIC_EnableIRQ(UART2_FLEXIO_IRQn);
}

void UART2_FLEXIO_IRQHandler(void)
{
	static char recv_buffer[MAX_MSG_LEN];	// static: must survive between interrupts
	static int recv_ptr = 0;
	static int send_ptr = 0;
	BaseType_t hpw = pdFALSE;

	/* ---- Transmit: only when we are actually sending (TIE on) ---- */
	if ((UART2->C2 & UART_C2_TIE_MASK) && (UART2->S1 & UART_S1_TDRE_MASK)) {
		if (send_buffer[send_ptr] == '\0') {
			send_ptr = 0;
			UART2->C2 &= ~UART_C2_TIE_MASK;
			UART2->C2 &= ~UART_C2_TE_MASK;
		} else {
			UART2->D = send_buffer[send_ptr++];
		}
	}

	/* ---- Receive ---- */
	uint8_t s1 = UART2->S1;
	if (s1 & (UART_S1_OR_MASK | UART_S1_NF_MASK | UART_S1_FE_MASK)) {
		statUartErrors++;			// cleared by the read of D below
	}

	if (s1 & (UART_S1_RDRF_MASK | UART_S1_OR_MASK)) {
		char rx = UART2->D;			// reading D clears RDRF

		if (rx == '\r') {
			// ignore the '\r' from Serial.println
		} else if (rx == '\n') {
			TMessage msg;
			recv_buffer[recv_ptr] = '\0';
			strncpy(msg.message, recv_buffer, MAX_MSG_LEN - 1);
			msg.message[MAX_MSG_LEN - 1] = '\0';
			statLines++;
			if (xQueueSendFromISR(rxQueue, &msg, &hpw) != pdTRUE) {
				statQueueFull++;
			}
			recv_ptr = 0;
		} else if (recv_ptr < MAX_MSG_LEN - 1) {
			recv_buffer[recv_ptr++] = rx;
		} else {
			recv_ptr = 0;			// line too long: discard it
		}
	}

	portYIELD_FROM_ISR(hpw);
}

void uart_link_send(const char *line)
{
	if (xSemaphoreTake(txMutex, portMAX_DELAY) != pdTRUE)
		return;

	while (UART2->C2 & UART_C2_TIE_MASK) {
		vTaskDelay(pdMS_TO_TICKS(5));		// block until the previous line is out
	}
	strncpy(send_buffer, line, MAX_MSG_LEN - 1);
	send_buffer[MAX_MSG_LEN - 1] = '\0';

	UART2->C2 |= UART_C2_TE_MASK;
	UART2->C2 |= UART_C2_TIE_MASK;		// TDRE is set, so the ISR fires immediately

	xSemaphoreGive(txMutex);
}
