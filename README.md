# Smart Ambulance Traffic Priority & Junction Management System

An intelligent, non-blocking four-way traffic junction controller with ambulance priority and anti-starvation management for the **STM32C031C6** (ARM Cortex-M0+) simulated in **Wokwi**.

---

## 🚦 System Overview

This project simulates a complete four-way traffic intersection (North, East, South, West) with dedicated emergency vehicle priority. When an ambulance approach button is pressed, the junction safely stops conflicting traffic through standardized yellow and all-red clearance intervals, grants dedicated green priority with an audible warning buzzer, and prevents civilian traffic starvation through a round-robin fairness policy.

---

## 🛠️ Hardware & Pin Mapping

### Traffic-Light LEDs (Active-High, Push-Pull)
| Approach | Red Pin | Yellow Pin | Green Pin | Logic |
| :--- | :---: | :---: | :---: | :--- |
| **North** | `PB0` | `PB1` | `PB2` | High = ON, Low = OFF |
| **East** | `PB3` | `PB4` | `PB5` | High = ON, Low = OFF |
| **South** | `PB6` | `PB7` | `PB8` | High = ON, Low = OFF |
| **West** | `PB9` | `PC6` | `PC7` | High = ON, Low = OFF |

### Ambulance Detection Pushbuttons (EXTI Falling Edge with Internal Pull-Ups)
| Button | Pin | Associated Approach | Shortcut Key in Wokwi |
| :--- | :---: | :---: | :---: |
| **btn1** | `PA0` | North Ambulance | `N` |
| **btn2** | `PA1` | East Ambulance | `E` |
| **btn3** | `PA4` | South Ambulance | `S` |
| **btn4** | `PA8` | West Ambulance | `W` |

### Alert & Telemetry
| Peripheral | Pin | Description |
| :--- | :---: | :--- |
| **Warning Buzzer** | `PA7` | Sounds during ambulance green and yellow priority phases |
| **UART TX** | `PA2` | USART2 TX @ 115200 baud (8N1) streamed to Wokwi Serial Monitor |
| **UART RX** | `PA3` | USART2 RX |

---

## ⚡ Key Features

1. **Non-Blocking Architecture**:
   - The entire state machine runs on `HAL_GetTick()` delta timing.
   - Zero blocking `HAL_Delay()` calls in the control loop, allowing instantaneous response to inputs.

2. **Interrupt-Driven Request Detection with Debounce**:
   - Button presses trigger hardware EXTI interrupts (`EXTI0_1_IRQHandler` and `EXTI4_15_IRQHandler`).
   - Uses the STM32C0 HAL callback `HAL_GPIO_EXTI_Falling_Callback()`.
   - Each button is protected by a 50 ms software contact debounce filter.
   - Requests are non-destructively latched into an atomic bitmask.

3. **Safe Transition Interlocks (Zero Conflicting Greens)**:
   - Green-to-green direct switching is physically prohibited.
   - Every approach transition passes through:
     - **Yellow Phase**: 2 seconds.
     - **All-Red Clearance Phase**: 1 second.
   - All helper functions start by setting all 4 directions to RED before illuminating target signals.

4. **Queueing & Round-Robin Fairness**:
   - Simultaneous or concurrent ambulance requests arriving during an active emergency are retained in the bitmask.
   - Served in circular round-robin sequence without dropping requests.

5. **Anti-Starvation Mechanism**:
   - Enforces a configurable limit of **2 consecutive ambulance services** (`MAX_CONSECUTIVE_AMBULANCES`).
   - Tracks the timestamp of when each lane last received a normal green (`lastNormalGreenTime`).
   - If emergency calls persist beyond the limit, the controller temporarily pauses priority to grant a full, uninterrupted normal green phase (5 s) to whichever lane has waited the longest.
   - Once the forced civilian phase completes safely, the system resumes serving the remaining ambulance queue.

6. **Real-Time Serial Telemetry**:
   - Outputs live event messages to the Wokwi Serial Monitor at 115200 baud.

---

## ⏱️ Timing Configuration

All timing parameters are configurable in `Core/Src/main.c`:

| Parameter | Macro | Default Value | Description |
| :--- | :--- | :---: | :--- |
| Normal Green | `TIME_NORMAL_GREEN` | 5000 ms | Duration of regular traffic green |
| Yellow Interval | `TIME_NORMAL_YELLOW` | 2000 ms | Caution clearance interval |
| All-Red Clearance | `TIME_ALL_RED` | 1000 ms | Intersection clear time |
| Ambulance Priority | `TIME_AMBULANCE_GREEN` | 10000 ms | Emergency vehicle green signal |
| Ambulance Yellow | `TIME_AMBULANCE_YELLOW` | 2000 ms | Emergency caution transition |
| Debounce Filter | `DEBOUNCE_INTERVAL_MS` | 50 ms | Switch bounce suppression |
| Consecutive Limit | `MAX_CONSECUTIVE_AMBULANCES` | 2 | Anti-starvation service threshold |

---

## 🖥️ State Machine Flow

```
+-------------------------------------------------------------+
|                     STATE_NORMAL_GREEN                      |
|                  (North -> East -> South -> West)           |
+------------------------------+------------------------------+
                               | Ambulance detected / Timer (5s)
                               v
+-------------------------------------------------------------+
|                     STATE_NORMAL_YELLOW                     |
|                            (2s)                             |
+------------------------------+------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|                       STATE_ALL_RED                         |
|                            (1s)                             |
+------------------------------+------------------------------+
                               |
       +-----------------------+-----------------------+
       | Ambulance Pending                             | No Emergency
       v                                               v
+------------------------------+         +----------------------------+
|    STATE_AMBULANCE_GREEN     |         |     Next Normal Green      |
|     (10s + Buzzer ON)        |         +----------------------------+
+--------------+---------------+
               | Timer (10s)
               v
+------------------------------+
|    STATE_AMBULANCE_YELLOW    |
|             (2s)             |
+--------------+---------------+
               | Timer (2s)
               v
+------------------------------+
|   STATE_AMBULANCE_ALL_RED    |
|      (1s + Buzzer OFF)       |
+--------------+---------------+
               |
               +---> If Consecutive < 2 & More Ambulances -> Next Ambulance Green
               |
               +---> If Consecutive >= 2 & More Ambulances -> Longest-Waiting Normal Green (Anti-Starvation)
               |
               +---> If No More Ambulances -> Return to Normal Rotation
```

---

## 🚀 How to Run in Wokwi Simulation

1. **Simulation Files**:
   - `diagram.json`: Circuit schematic with Nucleo-C031C6, 12 LEDs, 4 pushbuttons, and buzzer.
   - `wokwi.toml`: Points to `firmware.elf`.
   - `firmware.elf`: Compiled binary ready for simulation.

2. **Starting the Simulation**:
   - Open this workspace in VS Code with the Wokwi extension installed.
   - Press `F1` and select **Wokwi: Start Simulator**, or open `diagram.json` and click the play button.
   - Open the **Wokwi Serial Monitor** (115200 baud) to monitor live junction telemetry.

3. **Testing Ambulance Priority**:
   - Press **`N`**, **`E`**, **`S`**, or **`W`** on your keyboard to trigger an ambulance on North, East, South, or West.
   - Observe the safe transition from green to yellow, then all-red, followed by 10 seconds of green for the requested approach with the buzzer sounding.
   - Press multiple keys to test round-robin queuing and anti-starvation.

---

## 📁 Repository Structure

```
├── diagram.json                 # Wokwi circuit diagram & wiring
├── wokwi.toml                   # Wokwi simulation configuration
├── firmware.elf                 # Pre-compiled executable ELF binary
├── README.md                    # Project documentation
└── Ambulance_Traffic_control/   # Complete STM32CubeIDE project
    ├── Core/
    │   ├── Inc/                 # Header files (main.h, stm32c0xx_it.h)
    │   ├── Src/
    │   │   ├── main.c           # State machine, GPIO, UART, and scheduler
    │   │   └── stm32c0xx_it.c   # EXTI interrupt service routines
    │   └── Startup/             # Vector table & startup code
    └── Drivers/                 # STM32C0 HAL & CMSIS driver libraries
```
