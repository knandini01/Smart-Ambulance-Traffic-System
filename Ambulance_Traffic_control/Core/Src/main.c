#include "main.h"
#include <stdio.h>
#include <stdbool.h>

/* Traffic directions */
typedef enum
{
    NORTH = 0,
    EAST  = 1,
    SOUTH = 2,
    WEST  = 3
} Direction;

/* System states */
typedef enum
{
    STATE_NORMAL_GREEN = 0,
    STATE_NORMAL_YELLOW,
    STATE_ALL_RED,
    STATE_AMBULANCE_GREEN,
    STATE_AMBULANCE_YELLOW,
    STATE_AMBULANCE_ALL_RED
} TrafficState;

/* Timing & Fairness configurations (Configurable) */
#define TIME_NORMAL_GREEN          5000U  /* Normal green duration (5 seconds) */
#define TIME_NORMAL_YELLOW         2000U  /* Yellow duration (2 seconds) */
#define TIME_ALL_RED               1000U  /* All-red clearance interval (1 second) */
#define TIME_AMBULANCE_GREEN      10000U  /* Priority green duration (10 seconds) */
#define TIME_AMBULANCE_YELLOW      2000U  /* Ambulance yellow clearance (2 seconds) */
#define DEBOUNCE_INTERVAL_MS         50U  /* Per-button contact debounce (50 ms) */
#define MAX_CONSECUTIVE_AMBULANCES    2U  /* Anti-starvation consecutive ambulance limit */

/* State variables */
static TrafficState currentState = STATE_NORMAL_GREEN;
static Direction currentNormalDir = NORTH;
static Direction activeAmbulanceDir = NORTH;
static uint32_t stateStartTime = 0;

/* Anti-starvation and fairness variables */
static uint8_t consecutiveAmbulanceCount = 0;
static uint32_t lastNormalGreenTime[4] = {0, 0, 0, 0};
static bool antiStarvationForced = false;

/* EXTI shared variables */
static volatile uint8_t pendingRequests = 0;   /* Bitmask: bit 0: North, bit 1: East, bit 2: South, bit 3: West */
static volatile uint8_t unloggedRequests = 0;  /* Bitmask for UART event notification */
static volatile uint32_t lastPressTime[4] = {0, 0, 0, 0};

/* Function declarations */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_Init(void);

void AllLightsOff(void);
void SetAllRed(void);
void SetDirectionGreen(Direction dir);
void SetDirectionYellow(Direction dir);
void SetDirectionRed(Direction dir);
void SetTrafficState(TrafficState state, Direction dir);
void BuzzerOn(void);
void BuzzerOff(void);
const char* DirName(Direction dir);
int GetNextPendingAmbulance(Direction fromDir);
Direction GetLongestWaitingNormalDirection(Direction startFrom);
void ProcessTrafficStateMachine(void);
void ProcessUnloggedRequests(void);

/* ================= UART RETARGETING ================= */

int __io_putchar(int ch)
{
    if (ch == '\n')
    {
        while (!(USART2->ISR & USART_ISR_TXE_TXFNF));
        USART2->TDR = '\r';
    }
    while (!(USART2->ISR & USART_ISR_TXE_TXFNF));
    USART2->TDR = (uint8_t)ch;
    return ch;
}

/* ================= MAIN ================= */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    SystemCoreClockUpdate();
    MX_GPIO_Init();
    MX_USART2_Init();

    /* Initial state: North normal green, all others red */
    uint32_t now = HAL_GetTick();
    currentState = STATE_NORMAL_GREEN;
    currentNormalDir = NORTH;
    stateStartTime = now;
    lastNormalGreenTime[NORTH] = now;
    lastNormalGreenTime[EAST] = 0;
    lastNormalGreenTime[SOUTH] = 0;
    lastNormalGreenTime[WEST] = 0;
    consecutiveAmbulanceCount = 0;
    antiStarvationForced = false;

    SetTrafficState(STATE_NORMAL_GREEN, currentNormalDir);
    BuzzerOff();

    printf("\r\n========================================\r\n");
    printf("Smart Ambulance Traffic Control System\r\n");
    printf("Anti-Starvation Policy Active (Max %d Consecutive Ambulances)\r\n", MAX_CONSECUTIVE_AMBULANCES);
    printf("System initialized\r\n");
    printf("Normal traffic: %s GREEN\r\n", DirName(currentNormalDir));

    while (1)
    {
        ProcessUnloggedRequests();
        ProcessTrafficStateMachine();
    }
}

/* ================= TRAFFIC LIGHT CONTROL HELPERS ================= */

const char* DirName(Direction dir)
{
    switch (dir)
    {
        case NORTH: return "NORTH";
        case EAST:  return "EAST";
        case SOUTH: return "SOUTH";
        case WEST:  return "WEST";
        default:    return "UNKNOWN";
    }
}

/* All LEDs OFF */
void AllLightsOff(void)
{
    HAL_GPIO_WritePin(GPIOB,
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
        GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 |
        GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9,
        GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOC,
        GPIO_PIN_6 | GPIO_PIN_7,
        GPIO_PIN_RESET);
}

/* All 4 directions RED ON; all yellow and green LEDs OFF */
void SetAllRed(void)
{
    /* Red LEDs: PB0 (North), PB3 (East), PB6 (South), PB9 (West) */
    HAL_GPIO_WritePin(GPIOB,
        GPIO_PIN_0 | GPIO_PIN_3 | GPIO_PIN_6 | GPIO_PIN_9,
        GPIO_PIN_SET);

    /* Yellow and green LEDs */
    HAL_GPIO_WritePin(GPIOB,
        GPIO_PIN_1 | GPIO_PIN_2 |  /* North Y, G */
        GPIO_PIN_4 | GPIO_PIN_5 |  /* East Y, G */
        GPIO_PIN_7 | GPIO_PIN_8,   /* South Y, G */
        GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOC,
        GPIO_PIN_6 | GPIO_PIN_7,   /* West Y, G */
        GPIO_PIN_RESET);
}

/* Give Green to selected direction; ensure other three directions are RED */
void SetDirectionGreen(Direction dir)
{
    SetAllRed();

    switch (dir)
    {
        case NORTH:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); /* North Red OFF */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);   /* North Green ON */
            break;

        case EAST:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET); /* East Red OFF */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);   /* East Green ON */
            break;

        case SOUTH:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET); /* South Red OFF */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);   /* South Green ON */
            break;

        case WEST:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET); /* West Red OFF */
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_SET);   /* West Green ON */
            break;

        default:
            break;
    }
}

/* Give Yellow to selected direction; other three directions remain RED */
void SetDirectionYellow(Direction dir)
{
    SetAllRed();

    switch (dir)
    {
        case NORTH:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); /* North Red OFF */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);   /* North Yellow ON */
            break;

        case EAST:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET); /* East Red OFF */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);   /* East Yellow ON */
            break;

        case SOUTH:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET); /* South Red OFF */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);   /* South Yellow ON */
            break;

        case WEST:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET); /* West Red OFF */
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_SET);   /* West Yellow ON */
            break;

        default:
            break;
    }
}

/* Explicitly set a specific direction to RED (turns off its green and yellow) */
void SetDirectionRed(Direction dir)
{
    switch (dir)
    {
        case NORTH:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1 | GPIO_PIN_2, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
            break;

        case EAST:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4 | GPIO_PIN_5, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
            break;

        case SOUTH:
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7 | GPIO_PIN_8, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
            break;

        case WEST:
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
            break;

        default:
            break;
    }
}

void SetTrafficState(TrafficState state, Direction dir)
{
    switch (state)
    {
        case STATE_NORMAL_GREEN:
        case STATE_AMBULANCE_GREEN:
            SetDirectionGreen(dir);
            break;

        case STATE_NORMAL_YELLOW:
        case STATE_AMBULANCE_YELLOW:
            SetDirectionYellow(dir);
            break;

        case STATE_ALL_RED:
        case STATE_AMBULANCE_ALL_RED:
            SetAllRed();
            break;

        default:
            break;
    }
}

/* ================= BUZZER CONTROL ================= */

void BuzzerOn(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);
}

void BuzzerOff(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
}

/* ================= REQUEST SCHEDULING (FAIRNESS) ================= */

/* Selects the next pending ambulance direction in round-robin sequence starting from fromDir */
int GetNextPendingAmbulance(Direction fromDir)
{
    for (int i = 0; i < 4; i++)
    {
        Direction d = (Direction)((fromDir + i) % 4);
        if (pendingRequests & (1U << d))
        {
            return (int)d;
        }
    }
    return -1;
}

/* Finds the direction that has waited the longest since receiving a normal green phase.
   Tie-breaking proceeds symmetrically in rotational sequence starting from startFrom. */
Direction GetLongestWaitingNormalDirection(Direction startFrom)
{
    Direction longestDir = startFrom;
    uint32_t oldestTime = lastNormalGreenTime[startFrom];

    for (int i = 1; i < 4; i++)
    {
        Direction d = (Direction)((startFrom + i) % 4);
        if (lastNormalGreenTime[d] < oldestTime)
        {
            oldestTime = lastNormalGreenTime[d];
            longestDir = d;
        }
    }
    return longestDir;
}

/* Emits UART announcement for any newly detected ambulance button press */
void ProcessUnloggedRequests(void)
{
    if (unloggedRequests != 0)
    {
        for (int i = 0; i < 4; i++)
        {
            if (unloggedRequests & (1U << i))
            {
                __disable_irq();
                unloggedRequests &= ~(1U << i);
                __enable_irq();
                printf("Ambulance request received: %s\r\n", DirName((Direction)i));
            }
        }
    }
}

/* ================= TRAFFIC STATE MACHINE ================= */

void ProcessTrafficStateMachine(void)
{
    uint32_t now = HAL_GetTick();

    switch (currentState)
    {
        case STATE_NORMAL_GREEN:
        {
            /* If this normal green phase was forced by the anti-starvation policy,
               allow it to complete its full duration without early preemption. */
            if (!antiStarvationForced)
            {
                int req = GetNextPendingAmbulance(currentNormalDir);
                if (req != -1)
                {
                    if (req == (int)currentNormalDir)
                    {
                        /* Same direction requested: elevate directly to ambulance green */
                        activeAmbulanceDir = (Direction)req;
                        __disable_irq();
                        pendingRequests &= ~(1U << activeAmbulanceDir);
                        __enable_irq();

                        consecutiveAmbulanceCount = 1;
                        currentState = STATE_AMBULANCE_GREEN;
                        stateStartTime = now;
                        BuzzerOn();
                        printf("Ambulance priority granted: %s (Consecutive: %d/%d)\r\n",
                               DirName(activeAmbulanceDir), consecutiveAmbulanceCount, MAX_CONSECUTIVE_AMBULANCES);
                    }
                    else
                    {
                        /* Conflicting direction requested: initiate safe transition to yellow */
                        currentState = STATE_NORMAL_YELLOW;
                        stateStartTime = now;
                        SetTrafficState(STATE_NORMAL_YELLOW, currentNormalDir);
                        printf("Transition: %s YELLOW\r\n", DirName(currentNormalDir));
                    }
                    break;
                }
            }

            /* Regular timer expiration */
            if (now - stateStartTime >= TIME_NORMAL_GREEN)
            {
                currentState = STATE_NORMAL_YELLOW;
                stateStartTime = now;
                SetTrafficState(STATE_NORMAL_YELLOW, currentNormalDir);
                printf("Transition: %s YELLOW\r\n", DirName(currentNormalDir));
            }
            break;
        }

        case STATE_NORMAL_YELLOW:
        {
            if (now - stateStartTime >= TIME_NORMAL_YELLOW)
            {
                currentState = STATE_ALL_RED;
                stateStartTime = now;
                SetTrafficState(STATE_ALL_RED, currentNormalDir);
                printf("Transition: ALL RED\r\n");
            }
            break;
        }

        case STATE_ALL_RED:
        {
            if (now - stateStartTime >= TIME_ALL_RED)
            {
                if (antiStarvationForced)
                {
                    /* Forced anti-starvation normal cycle has completed safely */
                    antiStarvationForced = false;
                    printf("Anti-starvation normal phase completed; resuming request evaluation\r\n");
                }

                /* Check for pending ambulance requests */
                int req = GetNextPendingAmbulance((Direction)((currentNormalDir + 1) % 4));
                if (req != -1)
                {
                    /* Serve pending ambulance */
                    activeAmbulanceDir = (Direction)req;
                    __disable_irq();
                    pendingRequests &= ~(1U << activeAmbulanceDir);
                    __enable_irq();

                    consecutiveAmbulanceCount = 1;
                    currentState = STATE_AMBULANCE_GREEN;
                    stateStartTime = now;
                    SetTrafficState(STATE_AMBULANCE_GREEN, activeAmbulanceDir);
                    BuzzerOn();
                    printf("Ambulance priority granted: %s (Consecutive: %d/%d)\r\n",
                           DirName(activeAmbulanceDir), consecutiveAmbulanceCount, MAX_CONSECUTIVE_AMBULANCES);
                }
                else
                {
                    /* No ambulance requests: advance normal rotation North -> East -> South -> West */
                    consecutiveAmbulanceCount = 0;
                    currentNormalDir = (Direction)((currentNormalDir + 1) % 4);
                    currentState = STATE_NORMAL_GREEN;
                    stateStartTime = now;
                    lastNormalGreenTime[currentNormalDir] = now;
                    SetTrafficState(STATE_NORMAL_GREEN, currentNormalDir);
                    printf("Normal traffic: %s GREEN\r\n", DirName(currentNormalDir));
                }
            }
            break;
        }

        case STATE_AMBULANCE_GREEN:
        {
            if (now - stateStartTime >= TIME_AMBULANCE_GREEN)
            {
                currentState = STATE_AMBULANCE_YELLOW;
                stateStartTime = now;
                SetTrafficState(STATE_AMBULANCE_YELLOW, activeAmbulanceDir);
                printf("Transition: %s YELLOW\r\n", DirName(activeAmbulanceDir));
            }
            break;
        }

        case STATE_AMBULANCE_YELLOW:
        {
            if (now - stateStartTime >= TIME_AMBULANCE_YELLOW)
            {
                currentState = STATE_AMBULANCE_ALL_RED;
                stateStartTime = now;
                SetTrafficState(STATE_AMBULANCE_ALL_RED, activeAmbulanceDir);
                BuzzerOff();
                printf("Transition: ALL RED\r\n");
                printf("Ambulance priority ended: %s\r\n", DirName(activeAmbulanceDir));
            }
            break;
        }

        case STATE_AMBULANCE_ALL_RED:
        {
            if (now - stateStartTime >= TIME_ALL_RED)
            {
                /* Check if further ambulance requests are pending */
                int nextReq = GetNextPendingAmbulance((Direction)((activeAmbulanceDir + 1) % 4));

                if (nextReq != -1)
                {
                    /* Pending ambulances exist. Check anti-starvation limit */
                    if (consecutiveAmbulanceCount >= MAX_CONSECUTIVE_AMBULANCES)
                    {
                        /* Max consecutive limit reached: force a normal green for the longest-waiting approach */
                        antiStarvationForced = true;
                        consecutiveAmbulanceCount = 0;

                        Direction longestWaiting = GetLongestWaitingNormalDirection((Direction)((currentNormalDir + 1) % 4));
                        currentNormalDir = longestWaiting;

                        currentState = STATE_NORMAL_GREEN;
                        stateStartTime = now;
                        lastNormalGreenTime[currentNormalDir] = now;
                        SetTrafficState(STATE_NORMAL_GREEN, currentNormalDir);

                        printf("Anti-starvation: Max consecutive ambulances (%d) reached!\r\n", MAX_CONSECUTIVE_AMBULANCES);
                        printf("Anti-starvation: Granting guaranteed normal green to longest-waiting direction: %s\r\n",
                               DirName(currentNormalDir));
                    }
                    else
                    {
                        /* Serve next pending ambulance */
                        activeAmbulanceDir = (Direction)nextReq;
                        __disable_irq();
                        pendingRequests &= ~(1U << activeAmbulanceDir);
                        __enable_irq();

                        consecutiveAmbulanceCount++;
                        currentState = STATE_AMBULANCE_GREEN;
                        stateStartTime = now;
                        SetTrafficState(STATE_AMBULANCE_GREEN, activeAmbulanceDir);
                        BuzzerOn();
                        printf("Ambulance priority granted: %s (Consecutive: %d/%d)\r\n",
                               DirName(activeAmbulanceDir), consecutiveAmbulanceCount, MAX_CONSECUTIVE_AMBULANCES);
                    }
                }
                else
                {
                    /* All ambulance requests resolved: return to normal traffic rotation */
                    consecutiveAmbulanceCount = 0;
                    antiStarvationForced = false;
                    currentNormalDir = (Direction)((activeAmbulanceDir + 1) % 4);
                    currentState = STATE_NORMAL_GREEN;
                    stateStartTime = now;
                    lastNormalGreenTime[currentNormalDir] = now;
                    SetTrafficState(STATE_NORMAL_GREEN, currentNormalDir);
                    printf("All ambulance requests completed\r\n");
                    printf("Returning to normal traffic: %s GREEN\r\n", DirName(currentNormalDir));
                }
            }
            break;
        }

        default:
            currentState = STATE_NORMAL_GREEN;
            break;
    }
}

/* ================= EXTI CALLBACK ================= */

/* Called by the STM32C0 HAL when a configured falling-edge EXTI pin triggers */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    uint32_t now = HAL_GetTick();
    Direction dir;

    /* Map button pins to traffic directions according to Wokwi diagram.json */
    switch (GPIO_Pin)
    {
        case GPIO_PIN_0: /* btn1: North */
            dir = NORTH;
            break;
        case GPIO_PIN_1: /* btn2: East */
            dir = EAST;
            break;
        case GPIO_PIN_4: /* btn3: South */
            dir = SOUTH;
            break;
        case GPIO_PIN_8: /* btn4: West */
            dir = WEST;
            break;
        default:
            return;
    }

    /* 50ms software debounce per button */
    if ((now - lastPressTime[dir]) >= DEBOUNCE_INTERVAL_MS)
    {
        lastPressTime[dir] = now;
        pendingRequests |= (1U << dir);
        unloggedRequests |= (1U << dir);
    }
}

/* ================= CLOCK CONFIGURATION ================= */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_0);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV4;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1;

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ================= USART2 CONFIGURATION ================= */

static void MX_USART2_Init(void)
{
    /* Enable peripheral clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    /* Configure PA2 as USART2_TX, PA3 as USART2_RX */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 115200 baud with 12MHz PCLK */
    uint32_t pclk = HAL_RCC_GetPCLK1Freq();
    USART2->BRR = (pclk + (115200 / 2)) / 115200;
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;
}

/* ================= GPIO CONFIGURATION ================= */

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable GPIO clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Start outputs in a known safe state (all off) */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOB,
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
        GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 |
        GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9,
        GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOC,
        GPIO_PIN_6 | GPIO_PIN_7,
        GPIO_PIN_RESET);

    /* Configure ambulance request buttons: PA0, PA1, PA4, PA8 */
    GPIO_InitStruct.Pin =
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_8;

    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Configure buzzer output: PA7 */
    GPIO_InitStruct.Pin = GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Configure traffic LEDs on PB0-PB9 */
    GPIO_InitStruct.Pin =
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
        GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 |
        GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Configure West yellow and green LEDs: PC6 and PC7 */
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* Enable EXTI interrupt groups */
    HAL_NVIC_SetPriority(EXTI0_1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);

    HAL_NVIC_SetPriority(EXTI4_15_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}
