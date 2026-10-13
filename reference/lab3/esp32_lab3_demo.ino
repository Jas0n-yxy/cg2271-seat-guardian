// ESP32 side of the AS03 demo, with debug output.
// Reads the light sensor every 200 ms, sends "LIGHT:<value>" to the MCXC444,
// and prints exactly what was sent (TX ->) and each full line received (RX <-).

const int LIGHT_PIN = 4;
const int UART_TX_PIN = 1;   // -> MCXC444 PTE23 (RX)
const int UART_RX_PIN = 2;   // <- MCXC444 PTE22 (TX)

char rxLine[96];
int rxPtr = 0;
unsigned long sentCount = 0;
unsigned long recvCount = 0;

void setup() {
  Serial.begin(9600);  // Computer Serial Monitor
  Serial1.begin(9600, SERIAL_8N1,
                UART_RX_PIN, UART_TX_PIN);  // MCXC444

  analogReadResolution(12);
  delay(1000);
  Serial.println("ESP32 light demo (debug) started");
}

void loop() {
  int lightReading = analogRead(LIGHT_PIN);

  // Send a newline-terminated message to the MCXC444
  Serial1.print("LIGHT:");
  Serial1.println(lightReading);
  sentCount++;

  Serial.print("TX -> LIGHT:");
  Serial.print(lightReading);
  Serial.print("   (sent ");
  Serial.print(sentCount);
  Serial.print(", received ");
  Serial.print(recvCount);
  Serial.println(")");

  // Collect replies from the MCXC444 into whole lines, without waiting
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    if (c == '\n') {
      rxLine[rxPtr] = '\0';
      recvCount++;
      Serial.print("RX <- ");
      Serial.println(rxLine);
      rxPtr = 0;
    } else if (c != '\r' && rxPtr < (int)sizeof(rxLine) - 1) {
      rxLine[rxPtr++] = c;
    }
  }

  delay(200);
}
