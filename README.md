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

## 🎮 How to Use the Simulator (Step-by-Step Guide)

You can run and test this simulation directly inside **VS Code** using the **Wokwi Simulator extension**.

### Step 1: Open the Project in VS Code
1. Clone or open this repository in VS Code:
   ```bash
   git clone https://github.com/knandini01/Smart-Ambulance-Traffic-System.git
   ```
2. Make sure the **Wokwi Simulator** extension is installed in VS Code:
   - Go to the Extensions panel (`Ctrl+Shift+X` or `Cmd+Shift+X`).
   - Search for **Wokwi Simulator** by Wokwi and click **Install**.

### Step 2: Start the Simulation
1. Open the file [`diagram.json`](diagram.json) in VS Code.
2. Click the green **Play (Start Simulation)** button in the top-right corner of the editor, or press `F1` and select **Wokwi: Start Simulator**.
3. The simulator canvas will launch showing the Nucleo-C031C6 board, 12 LEDs, 4 pushbuttons, and the buzzer.
4. The **Wokwi Serial Monitor** will automatically open at the bottom (configured at 115200 baud).

---

### Step 3: Interactive Controls & Keyboard Shortcuts

You can trigger ambulance arrivals either by clicking the red buttons on the diagram with your mouse or by pressing their keyboard shortcuts:

| Action | Control | How to Trigger |
| :--- | :---: | :--- |
| **North Ambulance** | `btn1` | Press key **`N`** on your keyboard, or click the **NORTH Ambulance** button |
| **East Ambulance** | `btn2` | Press key **`E`** on your keyboard, or click the **EAST Ambulance** button |
| **South Ambulance** | `btn3` | Press key **`S`** on your keyboard, or click the **SOUTH Ambulance** button |
| **West Ambulance** | `btn4` | Press key **`W`** on your keyboard, or click the **WEST Ambulance** button |

---

### Step 4: Step-by-Step Testing Walkthrough

Follow these 4 tests to verify all system behaviors:

#### 🟢 Test 1: Normal Traffic Cycle (Hands-Free)
1. Start the simulation and do not press any buttons.
2. Watch the traffic signals cycle naturally:
   - **North Green** turns on for 5 seconds.
   - North switches to **Yellow** for 2 seconds.
   - **All 4 Red LEDs** turn on for a 1-second intersection clearance.
   - **East Green** turns on for 5 seconds, followed by Yellow (2s) and All-Red (1s).
   - Sequence repeats through **South**, **West**, and returns to **North**.
3. Observe the Serial Monitor output:
   ```text
   System initialized
   Normal traffic: NORTH GREEN
   Transition: NORTH YELLOW
   Transition: ALL RED
   Normal traffic: EAST GREEN
   ```

#### 🚨 Test 2: Emergency Ambulance Priority
1. While North has a green light, press **`E`** (East Ambulance).
2. Observe the safe transition:
   - North immediately transitions to **Yellow** for 2 seconds (it never abruptly cuts off).
   - All 4 directions show **Red** for 1 second.
   - **East Green** turns ON for a full **10 seconds**.
   - The **Warning Buzzer** turns ON.
   - Serial Monitor prints: `Ambulance request received: EAST` and `Ambulance priority granted: EAST`.
3. When the 10 seconds expire, East transitions to Yellow (2s) and All-Red (1s), the buzzer turns OFF, and normal traffic resumes safely.

#### 🔄 Test 3: Multiple Ambulances & Queueing
1. While any light is green, press **`S`** (South Ambulance) and immediately press **`W`** (West Ambulance).
2. Observe:
   - Traffic safely clears and grants green to the **South Ambulance** first for 10 seconds (buzzer sounds).
   - The West request is **not lost**—it is held in the queue.
   - Once South finishes its yellow and all-red clearance, **West Ambulance** is immediately granted its 10-second priority green.
   - The Serial Monitor confirms both requests were queued and serviced consecutively.

#### 🛡️ Test 4: Anti-Starvation Protection (Fairness Rule)
1. Simulate a heavy emergency flood by pressing **`N`**, then **`E`**, then **`S`** in rapid succession.
2. **First Emergency**: North is granted priority (Consecutive: 1/2).
3. **Second Emergency**: East is granted priority (Consecutive: 2/2).
4. **Anti-Starvation Trigger**:
   - Because 2 ambulances were served in a row, the controller detects that civilian lanes are at risk of being trapped.
   - Instead of immediately granting green to the South ambulance, the controller safely pauses emergencies and grants a **full 5-second normal green** to whichever civilian direction has waited the longest!
   - Serial Monitor displays:
     ```text
     Anti-starvation: Max consecutive ambulances (2) reached!
     Anti-starvation: Granting guaranteed normal green to longest-waiting direction: SOUTH
     ```
5. Once the civilian phase completes safely (5s Green -> 2s Yellow -> 1s All-Red), the system automatically resumes the queue and serves the South ambulance.

---

## 📁 Repository Structure

```
├── diagram.json                 # Wokwi circuit diagram & wiring
├── wokwi.toml                   # Wokwi simulation configuration
├── firmware.elf                 # Pre-compiled executable ELF binary
├── README.md                    # Project documentation & simulator guide
└── Ambulance_Traffic_control/   # Complete STM32CubeIDE project
    ├── Core/
    │   ├── Inc/                 # Header files (main.h, stm32c0xx_it.h)
    │   ├── Src/
    │   │   ├── main.c           # State machine, GPIO, UART, and scheduler
    │   │   └── stm32c0xx_it.c   # EXTI interrupt service routines
    │   └── Startup/             # Vector table & startup code
    └── Drivers/                 # STM32C0 HAL & CMSIS driver libraries
```
