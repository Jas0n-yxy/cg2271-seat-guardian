# Seat Guardian: wiring guide

Wire one group at a time, in the order below, and run its test before adding the next group. Pins marked *proposed* have not been tried on hardware yet; confirm them against the board schematic (`SPF-93440_A-1.pdf`, page 4, "ARDUINO HEADERS") before wiring. If a pin is inconvenient, change the matching line in `mcxc444/SeatGuardian/source/config.h`.

## Safety rules

1. **Unplug both USB cables while wiring.** Check every wire, then power up.
2. **All grounds must be connected together**: MCXC444 GND, ESP32 GND and the "−" pin of every module.
3. **Only the HC-SR04 and the servo get 5 V.** Power every other module from **3.3 V**, so their output signals are also 3.3 V and cannot damage the board pins.
4. **The HC-SR04 Echo pin outputs 5 V.** It must go through the voltage divider before it reaches the MCXC444.

## Power rails

On the breadboard, make one rail from the MCXC444 **3V3** pin and one **GND** rail. Run a separate wire from the MCXC444 **5V** header pin to the HC-SR04 only.

## Group 1: board-to-board link (test level L1)

| From | To |
|---|---|
| MCXC444 **PTE22** (UART2 TX) | ESP32 **GPIO2** (RX) |
| MCXC444 **PTE23** (UART2 RX) | ESP32 **GPIO1** (TX) |
| MCXC444 **GND** | ESP32 **GND** |

TX always goes to the other board's RX. Without the GND wire the boards receive nothing or garbage.

**Test:** the ESP32 Serial Monitor (115200) shows `RX <- STATUS,FREE,...` every second. Type `LED,100,0,0`; the MCXC444 LED turns red.

## Group 2: ESP32 devices

| Module pin | Connect to |
|---|---|
| Light sensor **S** | ESP32 **GPIO4** |
| Light sensor **+** (middle pin) | ESP32 **3V3** |
| Light sensor **−** | GND |
| Servo **orange** (signal) | ESP32 **GPIO5** *(proposed)* |
| Servo **red** (power) | ESP32 **5V** |
| Servo **brown** (ground) | GND |

**Test:** `LDR = xxxx` in the ESP32 Serial Monitor changes clearly when you cover the sensor or shine a torch on it.

## Group 3: MCXC444 devices

| Module pin | MCXC444 pin | Board label |
|---|---|---|
| Sound sensor **DO** | **PTA12** *(proposed)* | **D3** |
| Touch sensor **DO** (or S) | **PTA13** *(proposed)* | **D8** |
| Buzzer **S** | **PTB2** *(proposed)* | **A2** |
| **+ / VCC** of all three modules | **3V3** | |
| **−** of all three modules | GND | |

Before testing, turn the sound sensor's potentiometer until normal room noise does not light its DO LED.

**Test:** clapping prints `[EVT] noise` and touching prints `[EVT] touch` on the MCXC444 terminal (115200), **once per event**. Typing `BUZZ,300` in the ESP32 Serial Monitor beeps the buzzer for 0.3 s.

## Group 4: HC-SR04 ultrasonic (meet-up #1)

| HC-SR04 pin | Connect to |
|---|---|
| **VCC** | MCXC444 **5V** |
| **GND** | GND |
| **Trig** | MCXC444 **PTE21** *(proposed)* |
| **Echo** | the voltage divider below, then MCXC444 **PTE20** *(proposed)* |

```
HC-SR04 Echo ──[ 1 kΩ ]──┬──► MCXC444 PTE20
                         │
                      [ 2 kΩ ]
                         │
                        GND

5 V × 2 / (1 + 2) ≈ 3.3 V, which is safe for the MCXC444.
No 2 kΩ resistor? Use two 1 kΩ resistors in series instead.
```

The rule is: **the bottom resistor should be about twice the top one.** Other combinations:

| Top (Echo → PTE20) | Bottom (PTE20 → GND) | PTE20 sees | OK? |
|---|---|---|---|
| 1 kΩ | 2 kΩ | 3.33 V | ✅ |
| 2 kΩ | 2 kΩ + 2 kΩ in series | 3.33 V | ✅ |
| 2 kΩ | 2 kΩ | 2.5 V | ⚠️ marginal: the pin needs about 0.7 × 3.3 V ≈ 2.3 V to read HIGH, so a weak 5 V supply can make readings drop out |
| 1 kΩ | 2 kΩ + 2 kΩ in series | 4.0 V | ❌ too high, can damage the pin |

If the module is an **HC-SR04P** (3–5.5 V version), power it from **3.3 V** instead and connect Echo straight to PTE20 with no divider.

**Test:** the MCXC444 terminal prints `[US] <n> cm` twice a second. With a hand or a book at 10 / 30 / 50 cm, the distance is within ±2 cm. Closer than 60 cm turns the LED blue. `-1` means no echo was received; `-2` means Echo is stuck high.

## Finding the pins

- **MCXC444:** the headers are printed with Arduino-style labels (D3, D8, A2, ...). PTE20 to PTE23 are on the analog / differential header. When unsure, check the schematic, page 4, "ARDUINO HEADERS".
- **ESP32 DevKitM-1:** the numbers printed on the board are **GPIO numbers** ("1", "2", "4", "5"), **not** physical pin positions.
