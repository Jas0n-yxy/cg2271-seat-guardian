# Seat Guardian

CG2271 AS4 mini project, group B01. A smart library seat monitor: the MCXC444 senses the seat and gives local feedback under FreeRTOS, and the ESP32 connects it to an LLM (NUS SoCLaaS), a Telegram assistant and a Firebase log.

Wiring guide: [`docs/WIRING.md`](docs/WIRING.md). Architecture diagram: [`docs/Seat_Guardian_Architecture.html`](docs/Seat_Guardian_Architecture.html) (download and open in a browser). Interim report: [`docs/B01_YY_Interim.pdf`](docs/B01_YY_Interim.pdf).

## Repository layout

| Path | What it is |
|---|---|
| `mcxc444/SeatGuardian/` | Complete MCUXpresso FreeRTOS project (heap 4, time slicing on). Import it; our code is in `source/`. |
| `esp32/seat_guardian/` | ESP32-S2 Arduino sketch (open the `.ino` in Arduino IDE). |
| `shared/packet.h` | Board-to-board frame format. Copies sit in both board folders; keep all three identical. |
| `reference/lab3/` | Our Lab 3 code, kept unchanged for reference. |
| `docs/` | Report and architecture diagram. |

## What is reused from Lab 3 / Lab 2

| Project file | Reused from |
|---|---|
| `mcxc444/SeatGuardian/source/uart_link.c` | Lab 3 demo: interrupt-driven UART2 (PTE22/PTE23, 9600 baud), RX lines → queue, TX through the ISR, error counters. Added a TX mutex. |
| `mcxc444/SeatGuardian/source/actuators.c` | Lab 2 `as02_2.c`: RGB LED PWM on TPM0. |
| `mcxc444/SeatGuardian/source/sensors.c` | Lab 3 Part 6 pattern: PORT interrupt → clear ISFR → `xSemaphoreGiveFromISR`. |
| `esp32/seat_guardian/seat_guardian.ino` | Lab 3 ESP32 sketch: UART1 on GPIO1/GPIO2, LDR on GPIO4 (12-bit ADC). |

## Current status

| Part | Status |
|---|---|
| UART link, RGB LED PWM, LDR | Reused from labs; compiles. Re-test on hardware. |
| Task / queue / mutex / semaphore skeleton | Done; imports and builds with 0 errors / 0 warnings (~39 KB flash, ~15 KB RAM). |
| Ultrasonic driver | **TODO meet-up #1** (`sensors.c`, `ultrasonic_read_cm`) |
| Sound + touch interrupts | Wired in code; **check polarity, add debounce at meet-up #2** |
| Buzzer, servo | Buzzer GPIO stub; servo **TODO week 10** |
| Packet protocol, full state machine | **Week 11** |
| SoCLaaS, Telegram, Firebase | **Week 12** |

## Parts checklist (tick when found)

| ✓ | Part | Board | Mode | Where to get it | Notes |
|---|---|---|---|---|---|
| ☐ | HC-SR04 ultrasonic | MCXC444 | polled | sensor kit / TA | Needs **5 V**; ECHO through a 1 kΩ / 2 kΩ divider |
| ☐ | Sound sensor with digital output (big sound, KY-037/KY-038) | MCXC444 | interrupt | sensor kit | Tune the potentiometer so normal room noise stays LOW |
| ☐ | Touch sensor (KY-036 metal touch, or TTP223) | MCXC444 | interrupt | sensor kit | Check whether DO goes HIGH or LOW on touch |
| ☐ | Photoresistor module (KY-018) | ESP32 | polled | sensor kit | Power from **3.3 V** only |
| ☐ | Onboard RGB LED | MCXC444 | actuator | on the board | Counts as our one onboard actuator |
| ☐ | Buzzer (active KY-012 or passive KY-006) | MCXC444 | actuator | sensor kit | Active = on/off; passive needs a PWM tone |
| ☐ | Servo SG90 | ESP32 | actuator | TA | 5 V supply; signal on GPIO5 |
| ☐ | 1 kΩ + 2 kΩ resistors (or 1 kΩ + 1 kΩ + 1 kΩ) | – | – | lab | HC-SR04 echo divider |
| ☐ | Jumper wires, 2 breadboards | – | – | kit | |

## Pin table

Pins marked *proposed* are untested; confirm them in the MCXC444 reference manual before wiring.

| Signal | MCXC444 pin | ESP32 pin | Status |
|---|---|---|---|
| UART MCXC → ESP32 | PTE22 (UART2_TX, ALT4) | GPIO2 (RX) | tested in Lab 3 |
| UART ESP32 → MCXC | PTE23 (UART2_RX, ALT4) | GPIO1 (TX) | tested in Lab 3 |
| GND | GND | GND | required |
| RGB LED | PTE31 / PTD5 / PTE29 (TPM0 CH4/5/2) | – | onboard |
| Sound DO | PTA12 (D3), PORTA IRQ | – | proposed |
| Touch DO | PTA13 (D8), PORTA IRQ | – | proposed |
| HC-SR04 TRIG | PTE21, GPIO out | – | proposed |
| HC-SR04 ECHO | PTE20, TPM1_CH0 input capture (via divider) | – | proposed |
| Buzzer | PTB2 (A2) | – | proposed |
| LDR | – | GPIO4 | tested in Lab 3 |
| Servo signal | – | GPIO5 | proposed |

## Build: MCXC444

The repo contains the whole MCUXpresso project (FreeRTOS, heap 4, pre-emption and time slicing already enabled), so there is nothing to set up.

1. MCUXpresso → **File → Import… → General → Existing Projects into Workspace** → Next.
2. *Select root directory*: the cloned `cg2271-seat-guardian\mcxc444` folder. Tick **SeatGuardian**.
3. **Leave "Copy projects into workspace" unticked**, so you edit the files inside the repo and `git` sees your changes. Finish.
4. Select the project → **Build**. Expect `Build Finished. 0 errors`.
5. **Debug** to flash. Debug console: MCU-Link COM port at **115200**.

If *Debug* hangs at "Target Discovery", flash without the debugger:
```
C:\nxp\LinkServer_25.6.131\LinkServer.exe flash MCXC444:FRDM-MCXC444 load mcxc444\SeatGuardian\Debug\SeatGuardian.axf
```

## Build: ESP32

1. Arduino IDE with **esp32 by Espressif Systems**; board **ESP32S2 Dev Module**.
2. Copy `esp32/seat_guardian/secrets.example.h` to `secrets.h` in the same folder and fill in your values. `secrets.h` is git-ignored, so **never paste keys into any other file**.
3. Upload, open Serial Monitor at **115200**.

## Quick hardware test (weeks 9–10)

With both boards wired, type in the ESP32 Serial Monitor:

- `LED,100,0,0` → MCXC444 LED turns red
- `BUZZ,300` → buzzer beeps for 300 ms

The ESP32 Serial Monitor should also print `RX <- STATUS,FREE,...` once a second from the MCXC444.
