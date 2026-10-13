// Seat Guardian, ESP32-S2 side.
//
// Reused from our Lab 3 demo: UART1 remapped to GPIO1/GPIO2 at 9600 baud,
// LDR on GPIO4 with 12-bit ADC, line-based messages to/from the MCXC444.
//
// FreeRTOS tasks (the Arduino core runs on FreeRTOS):
//   uartTask    receives lines from the MCXC444 and prints them
//   consoleTask forwards lines typed in the Serial Monitor to the MCXC444
//               (test the actuators by typing  LED,100,0,0  or  BUZZ,300)
//   ldrTask     polls the light sensor every 2 s
// Week 12 TODO: llmTask (SoCLaaS), firebaseTask, telegramTask.

#include "packet.h"

#if __has_include("secrets.h")
#include "secrets.h"            // your real keys, never committed (see .gitignore)
#else
#include "secrets.example.h"    // placeholders so the sketch always compiles
#endif

const int UART_TX_PIN = 1;      // -> MCXC444 PTE23 (RX)
const int UART_RX_PIN = 2;      // <- MCXC444 PTE22 (TX)
const int LIGHT_PIN   = 4;      // LDR signal (ADC1_CH3)
const int SERVO_PIN   = 5;      // Week 10 TODO: SG90 via ESP32Servo library

const uint32_t LINK_BAUD = 9600;
const uint32_t LDR_PERIOD_MS = 2000;

SemaphoreHandle_t linkTxMutex;  // consoleTask and future tasks share Serial1 TX
volatile int lastLight = -1;

void sendToMcxc(const String &line) {
  if (xSemaphoreTake(linkTxMutex, portMAX_DELAY) == pdTRUE) {
    Serial1.print(line);
    Serial1.print('\n');
    xSemaphoreGive(linkTxMutex);
  }
}

// Reads whole lines from a stream without blocking the task
static bool readLine(Stream &s, char *buf, int &ptr, int size) {
  while (s.available() > 0) {
    char c = s.read();
    if (c == '\n') {
      buf[ptr] = '\0';
      ptr = 0;
      return true;
    } else if (c != '\r' && ptr < size - 1) {
      buf[ptr++] = c;
    }
  }
  return false;
}

void uartTask(void *p) {
  char line[96];
  int ptr = 0;
  for (;;) {
    if (readLine(Serial1, line, ptr, sizeof(line))) {
      Serial.print("RX <- ");
      Serial.println(line);
      // Week 11 TODO: parse STATUS frames (packet.h) instead of text
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void consoleTask(void *p) {
  char line[96];
  int ptr = 0;
  for (;;) {
    if (readLine(Serial, line, ptr, sizeof(line))) {
      sendToMcxc(String(line));
      Serial.print("TX -> ");
      Serial.println(line);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void ldrTask(void *p) {
  for (;;) {
    long sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(LIGHT_PIN);   // average out noise
    lastLight = sum / 8;
    Serial.printf("LDR = %d\n", lastLight);
    vTaskDelay(pdMS_TO_TICKS(LDR_PERIOD_MS));
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(LINK_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  analogReadResolution(12);     // 0 - 4095
  delay(1000);
  Serial.println("Seat Guardian (ESP32) started");

  linkTxMutex = xSemaphoreCreateMutex();

  xTaskCreate(uartTask,    "uart",    4096, NULL, 3, NULL);
  xTaskCreate(consoleTask, "console", 4096, NULL, 2, NULL);
  xTaskCreate(ldrTask,     "ldr",     2048, NULL, 1, NULL);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));   // all work happens in the tasks
}
