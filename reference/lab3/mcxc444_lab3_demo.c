/*
 * CG2271 Assignment 3 - Lab Demo (MCXC444 side)
 *
 * ESP32 reads a light sensor and sends "LIGHT:<value>\r\n" over UART every 200 ms.
 * MCXC444 receives it on UART2 (PTE22 TX / PTE23 RX, ALT4).
 *
 * Each task blinks its OWN LED colour so the two priorities are visible:
 *   - actuatorTask (priority 2, HIGH) -> RED LED
 *       Blink speed follows the light reading: DARK = fast, MEDIUM = medium,
 *       BRIGHT = slow.
 *   - statusTask   (priority 1, LOW)  -> GREEN LED
 *       Steady 1 s heartbeat; also prints a status line and sends an ACK back
 *       to the ESP32 over UART.
 *
 * RTOS concepts used:
 *   - Queue: ISR -> actuatorTask (xQueueSendFromISR / xQueueReceive)
 *   - Mutex: protects lastReading / lastLevel shared by the two tasks
 *   - Two tasks with different priorities
 *
 * DEBUG_UART = 1 prints every raw line received from the ESP32, every level
 * change, and a once-per-second link summary (lines / errors) on the debug
 * console (COM5, 115200). Set it to 0 to hide the extra output.
 *
 * Project setup: RTOS OS, FreeRTOS heap 4 ONLY, Project Template -> RTOS.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* ---------------- Configuration ---------------- */
#define DEBUG_UART       1

#define BAUD_RATE        9600
#define UART_TX_PTE22    22
#define UART_RX_PTE23    23
#define UART2_INT_PRIO   128

#define MAX_MSG_LEN      64
#define QLEN             5

/* Light thresholds (12-bit ADC: 0-4095).
 * Our module reads HIGHER in the dark: covered > 3000, torch < 600. */
#define LIGHT_DARK_THRESHOLD    3000   // reading above this -> DARK
#define LIGHT_BRIGHT_THRESHOLD  600    // reading below this -> BRIGHT

/* Red LED half-periods (time between toggles) for each light level */
#define RED_FAST_MS     100   // DARK
#define RED_MEDIUM_MS   300   // MEDIUM
#define RED_SLOW_MS     800   // BRIGHT

/* Green heartbeat (low-priority task) */
#define GREEN_PERIOD_MS 1000

/* LED pins (active LOW on the FRDM board) */
#define RED_PIN     31  // PTE31
#define GREEN_PIN   5   // PTD5
#define BLUE_PIN    29  // PTE29

typedef enum { RED, GREEN, BLUE } TLED;

/* ---------------- Types and globals ---------------- */
typedef struct {
    char message[MAX_MSG_LEN];
} TMessage;

QueueHandle_t rxQueue;            // ISR -> actuatorTask
SemaphoreHandle_t dataMutex;      // protects lastReading / lastLevel

int lastReading = -1;             // shared, protected by dataMutex
const char *lastLevel = "NONE";   // shared, protected by dataMutex

char send_buffer[MAX_MSG_LEN];    // used by sendMessage and the ISR

/* Link statistics for debugging (written by the ISR / actuatorTask) */
volatile uint32_t statLines = 0;      // complete lines received by the ISR
volatile uint32_t statUartErrors = 0; // overrun / noise / framing errors
volatile uint32_t statQueueFull = 0;  // lines dropped because rxQueue was full
volatile uint32_t statBadMsgs = 0;    // lines that were not a valid LIGHT:<n>

/* ---------------- LEDs (actuator) ---------------- */
void initLEDs(void) {
    SIM->SCGC5 |= (SIM_SCGC5_PORTD_MASK | SIM_SCGC5_PORTE_MASK);

    PORTE->PCR[RED_PIN] &= ~PORT_PCR_MUX_MASK;
    PORTE->PCR[RED_PIN] |= PORT_PCR_MUX(1);
    PORTE->PCR[BLUE_PIN] &= ~PORT_PCR_MUX_MASK;
    PORTE->PCR[BLUE_PIN] |= PORT_PCR_MUX(1);
    PORTD->PCR[GREEN_PIN] &= ~PORT_PCR_MUX_MASK;
    PORTD->PCR[GREEN_PIN] |= PORT_PCR_MUX(1);

    GPIOE->PDDR |= ((1 << RED_PIN) | (1 << BLUE_PIN));
    GPIOD->PDDR |= (1 << GREEN_PIN);
}

void offLED(TLED led) {             // LEDs are active LOW: set pin = off
    switch (led) {
    case RED:   GPIOE->PSOR = (1 << RED_PIN);   break;
    case GREEN: GPIOD->PSOR = (1 << GREEN_PIN); break;
    case BLUE:  GPIOE->PSOR = (1 << BLUE_PIN);  break;
    }
}

void toggleLED(TLED led) {
    switch (led) {
    case RED:   GPIOE->PTOR = (1 << RED_PIN);   break;
    case GREEN: GPIOD->PTOR = (1 << GREEN_PIN); break;
    case BLUE:  GPIOE->PTOR = (1 << BLUE_PIN);  break;
    }
}

/* ---------------- UART2 ---------------- */
void initUART2(uint32_t baud_rate) {
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
    UART2->BDH &= ~UART_BDH_SBR_MASK;              // BDH first!
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

#if DEBUG_UART
    PRINTF("[DBG] UART2 bus clock = %u Hz, SBR = %u, baud = %u\r\n",
           (unsigned)bus_clk, (unsigned)sbr, (unsigned)baud_rate);
#endif
}

void UART2_FLEXIO_IRQHandler(void) {
    static char recv_buffer[MAX_MSG_LEN];   // static: must survive between interrupts
    static int recv_ptr = 0;
    static int send_ptr = 0;
    BaseType_t hpw = pdFALSE;               // must start as pdFALSE

    /* ---- Transmit: only when we are actually sending (TIE on) ---- */
    if ((UART2->C2 & UART_C2_TIE_MASK) && (UART2->S1 & UART_S1_TDRE_MASK)) {
        if (send_buffer[send_ptr] == '\0') {
            send_ptr = 0;
            UART2->C2 &= ~UART_C2_TIE_MASK;  // done: stop TX interrupt
            UART2->C2 &= ~UART_C2_TE_MASK;   // and transmitter
        } else {
            UART2->D = send_buffer[send_ptr++];
        }
    }

    /* ---- Receive ---- */
    uint8_t s1 = UART2->S1;
    if (s1 & (UART_S1_OR_MASK | UART_S1_NF_MASK | UART_S1_FE_MASK)) {
        statUartErrors++;                    // cleared by the read of D below
    }

    if (s1 & (UART_S1_RDRF_MASK | UART_S1_OR_MASK)) {
        char rx = UART2->D;                  // reading D clears RDRF (and OR/NF/FE)

        if (rx == '\r') {
            // Serial.println sends "\r\n"; ignore the '\r'
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
            recv_ptr = 0;                    // line too long: discard it
        }
    }

    portYIELD_FROM_ISR(hpw);
}

// Start an interrupt-driven transmission. Waits if a previous one is still going.
void sendMessage(const char *message) {
    while (UART2->C2 & UART_C2_TIE_MASK) {
        vTaskDelay(pdMS_TO_TICKS(5));        // block (not busy-wait) until TX is idle
    }
    strncpy(send_buffer, message, MAX_MSG_LEN - 1);
    send_buffer[MAX_MSG_LEN - 1] = '\0';

    UART2->C2 |= UART_C2_TE_MASK;            // enable transmitter
    UART2->C2 |= UART_C2_TIE_MASK;           // TDRE is set, so ISR fires immediately
}

/* ---------------- Tasks ---------------- */

// HIGH priority (2) -> RED LED.
// Waits on the queue for new readings, but only until the next toggle is due,
// so it can update the blink speed AND keep blinking on time.
static void actuatorTask(void *p) {
    TMessage msg;
    TickType_t halfPeriod = pdMS_TO_TICKS(RED_SLOW_MS);
    TickType_t nextToggle = xTaskGetTickCount() + halfPeriod;
    const char *prevLevel = "NONE";

    while (1) {
        // Ticks left until the next toggle (signed maths handles tick wrap-around)
        int32_t remaining = (int32_t)(nextToggle - xTaskGetTickCount());
        TickType_t wait = (remaining > 0) ? (TickType_t)remaining : 0;

        if (xQueueReceive(rxQueue, &msg, wait) == pdTRUE) {
#if DEBUG_UART
            PRINTF("[RX] \"%s\"\r\n", msg.message);
#endif
            // New reading arrived before the toggle was due: update blink speed
            if (strncmp(msg.message, "LIGHT:", 6) != 0) {
                statBadMsgs++;
#if DEBUG_UART
                PRINTF("[RX] ignored: does not start with LIGHT:\r\n");
#endif
                continue;                    // not a light message, ignore
            }
            int reading = atoi(msg.message + 6);
            if (reading < 0 || reading > 4095) {
                statBadMsgs++;
#if DEBUG_UART
                PRINTF("[RX] ignored: value %d out of range\r\n", reading);
#endif
                continue;                    // corrupted value, ignore
            }

            const char *level;
            int blinkMs;
            if (reading > LIGHT_DARK_THRESHOLD) {
                halfPeriod = pdMS_TO_TICKS(RED_FAST_MS);
                level = "DARK";
                blinkMs = RED_FAST_MS;
            } else if (reading >= LIGHT_BRIGHT_THRESHOLD) {
                halfPeriod = pdMS_TO_TICKS(RED_MEDIUM_MS);
                level = "MEDIUM";
                blinkMs = RED_MEDIUM_MS;
            } else {
                halfPeriod = pdMS_TO_TICKS(RED_SLOW_MS);
                level = "BRIGHT";
                blinkMs = RED_SLOW_MS;
            }

#if DEBUG_UART
            if (level != prevLevel) {
                PRINTF("[LEVEL] %s -> %s (reading %d, red toggles every %d ms)\r\n",
                       prevLevel, level, reading, blinkMs);
            }
#endif
            prevLevel = level;
            (void)blinkMs;

            if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
                lastReading = reading;
                lastLevel = level;
                xSemaphoreGive(dataMutex);
            }
            continue;                        // go back and wait out the remaining time
        }

        // Timed out: toggle is due
        toggleLED(RED);
        nextToggle += halfPeriod;

        // If the period just got shorter and we're behind, resync instead of catching up
        if ((int32_t)(nextToggle - xTaskGetTickCount()) < 0) {
            nextToggle = xTaskGetTickCount() + halfPeriod;
        }
    }
}

// LOW priority (1) -> GREEN LED.
// Steady heartbeat + slow work (printing, replying to the ESP32).
static void statusTask(void *p) {
    char buffer[MAX_MSG_LEN];
    while (1) {
        int reading = -1;
        const char *level = "NONE";

        toggleLED(GREEN);

        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            reading = lastReading;
            level = lastLevel;
            xSemaphoreGive(dataMutex);
        }

        PRINTF("Light = %d -> %s\r\n", reading, level);
#if DEBUG_UART
        PRINTF("[STAT] lines=%u uartErrors=%u queueFull=%u badMsgs=%u\r\n",
               (unsigned)statLines, (unsigned)statUartErrors,
               (unsigned)statQueueFull, (unsigned)statBadMsgs);
#endif

        sprintf(buffer, "ACK LIGHT=%d LEVEL=%s\n", reading, level);
        sendMessage(buffer);                 // shows up on the ESP32 Serial Monitor

        vTaskDelay(pdMS_TO_TICKS(GREEN_PERIOD_MS));
    }
}

/* ---------------- main ---------------- */
int main(void) {
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    BOARD_InitDebugConsole();
#endif

    initLEDs();
    offLED(RED);
    offLED(GREEN);
    offLED(BLUE);

    rxQueue = xQueueCreate(QLEN, sizeof(TMessage));
    dataMutex = xSemaphoreCreateMutex();
    if (rxQueue == NULL || dataMutex == NULL) {
        PRINTF("Failed to create queue/mutex\r\n");
        while (1);
    }

    // Init UART only after the queue exists, so the ISR never uses a NULL queue
    initUART2(BAUD_RATE);
    PRINTF("CG2271 AS03 Demo: RED = high priority, GREEN = low priority\r\n");

    xTaskCreate(actuatorTask, "red_high",  configMINIMAL_STACK_SIZE + 250, NULL, 2, NULL);
    xTaskCreate(statusTask,   "green_low", configMINIMAL_STACK_SIZE + 250, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1) {
        __asm volatile ("nop");
    }
    return 0;
}
