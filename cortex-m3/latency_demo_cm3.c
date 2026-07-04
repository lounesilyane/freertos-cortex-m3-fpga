/* ============================================================
 * FILE    : latency_demo_cm3.c
 * TARGET  : Cortex-M3 soft-core on GW1NSR-4C (Tang Nano 4K)
 * CLOCK   : 54 MHz (PLL output, from your PFE phase5_top.vhd)
 * PURPOSE : Illustrate the 3 latency types from RTOS theory
 *           using your exact PFE hardware context
 *
 * LATENCY TYPES DEMONSTRATED:
 *   1. Interrupt Latency    (latence d'interruption)
 *      = IRQ fires → ISR first instruction executes
 *      On Cortex-M3: minimum 12 clock cycles (hardware stacking)
 *
 *   2. Scheduling Latency   (latence d'ordonnancement)
 *      = ISR exits → high-priority task resumes execution
 *      FreeRTOS: handled via PendSV (lowest-priority exception)
 *
 *   3. Task Switching Latency (latence de commutation de tâche)
 *      = External event → task handling that event actually runs
 *      = Interrupt Latency + ISR execution + Scheduling Latency
 *
 * CONNECTION TO YOUR PFE:
 *   • Your UART ISR receiving 23-byte telemetry frames is a
 *     real example of interrupt + scheduling latency in action
 *   • Your SysTick at 54 MHz drives the FreeRTOS scheduler tick
 *   • Your BAUDDIV=469 UART is the peripheral being serviced
 * ============================================================ */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* ============================================================
 * HARDWARE CONSTANTS — from your PFE phase5_top.vhd
 * ============================================================ */
#define CPU_FREQ_HZ         54000000UL   /* PLL output: 54 MHz      */
#define SYSTICK_FREQ_HZ     1000UL       /* FreeRTOS tick: 1 ms     */
#define UART_BAUDDIV        469U         /* 115200 baud @ 54 MHz    */
#define UART_FRAME_BYTES    23U          /* your 23-byte ASCII frame */

/* Cortex-M3 SysTick reload value for 1 ms tick */
#define SYSTICK_RELOAD      ((CPU_FREQ_HZ / SYSTICK_FREQ_HZ) - 1U)
/* = 53999 cycles per tick                                          */

/* ============================================================
 * LATENCY MEASUREMENT
 * Uses DWT (Data Watchpoint and Trace) cycle counter
 * Available on Cortex-M3 — lets you measure cycles precisely
 * ============================================================ */
#define DWT_CYCCNT          (*(volatile uint32_t *)0xE0001004)
#define DWT_CTRL            (*(volatile uint32_t *)0xE0001000)
#define CoreDebug_DEMCR     (*(volatile uint32_t *)0xE000EDFC)

/* Enable DWT cycle counter */
static inline void dwt_enable(void)
{
    CoreDebug_DEMCR |= (1UL << 24);  /* enable trace             */
    DWT_CYCCNT       = 0U;           /* reset counter            */
    DWT_CTRL        |= 1U;           /* enable cycle counter     */
}

/* Snapshot the current cycle count */
static inline uint32_t dwt_get(void)
{
    return DWT_CYCCNT;
}

/* ============================================================
 * SHARED RESOURCES
 * ============================================================ */

/* Queue: UART ISR → PID task (mirrors your PFE UART → MCU flow) */
static QueueHandle_t  xUartQueue;

/* Semaphore: signals PID task that new encoder data is ready     */
static SemaphoreHandle_t xPidSemaphore;

/* Cycle timestamps for latency measurement */
static volatile uint32_t irq_fired_at   = 0U;  /* T0: IRQ fires      */
static volatile uint32_t isr_entered_at = 0U;  /* T1: ISR starts     */
static volatile uint32_t isr_exited_at  = 0U;  /* T2: ISR ends       */
static volatile uint32_t task_ran_at    = 0U;  /* T3: task resumes   */

/* Latency results (in CPU cycles @ 54 MHz) */
static volatile uint32_t interrupt_latency   = 0U; /* T1 - T0 */
static volatile uint32_t scheduling_latency  = 0U; /* T3 - T2 */
static volatile uint32_t switching_latency   = 0U; /* T3 - T0 */

/* ============================================================
 * UART FRAME TYPE
 * Mirrors your 23-byte ASCII telemetry frame from PFE
 * ============================================================ */
typedef struct {
    int16_t  consigne;      /* setpoint RPM                     */
    int16_t  vitesse;       /* measured speed (stroboscopic)    */
    int16_t  erreur;        /* error = consigne - vitesse       */
    int16_t  commande;      /* PID output → PWM duty cycle      */
    uint32_t timestamp_ms;  /* FreeRTOS tick count (ms)         */
} UartFrame_t;

/* ============================================================
 * ISR: UART Receive Interrupt
 *
 * LATENCY 1 — INTERRUPT LATENCY demonstrated here:
 *   • irq_fired_at   = cycle when UART data register full flag
 *                      set (approximate, set by peripheral HW)
 *   • isr_entered_at = very first instruction of this ISR
 *   • Difference     = Cortex-M3 exception entry overhead
 *                      (push 8 regs + vector fetch ≈ 12 cycles)
 *
 * In your PFE: this ISR would receive the 23-byte frame
 * sent by the FPGA PID over the UART TX line.
 * ============================================================ */
void UART0_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    UartFrame_t frame;

    /* ---- MEASURE: interrupt latency ---- */
    isr_entered_at  = dwt_get();
    interrupt_latency = isr_entered_at - irq_fired_at;
    /* Result: ~12 cycles @ 54 MHz ≈ 222 ns minimum            */
    /* (Cortex-M3 TRM: 12-cycle exception entry, no FPU)        */

    /* Read incoming UART data (your 23-byte frame) */
    /* In real code: read UART->DR register byte by byte        */
    frame.consigne     = (int16_t) 300;   /* placeholder read   */
    frame.vitesse      = (int16_t) 298;
    frame.erreur       = (int16_t)   2;
    frame.commande     = (int16_t) 150;
    frame.timestamp_ms = xTaskGetTickCountFromISR();

    /* Send frame to PID task via queue (ISR-safe API)          */
    xQueueSendFromISR(xUartQueue, &frame, &xHigherPriorityTaskWoken);

    /* Signal PID task that new data is ready                   */
    xSemaphoreGiveFromISR(xPidSemaphore, &xHigherPriorityTaskWoken);

    /* ---- MEASURE: ISR exit timestamp ---- */
    isr_exited_at = dwt_get();

    /* If a higher-priority task was woken, request PendSV now  */
    /* PendSV = the Cortex-M3 exception FreeRTOS uses for       */
    /* context switching — configured at LOWEST priority so it  */
    /* runs after all other ISRs complete                        */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

    /* After portYIELD_FROM_ISR:
     * • PendSV is pended
     * • When this ISR returns, processor checks pending exceptions
     * • PendSV fires → FreeRTOS saves current task context
     *   (push remaining registers to TCB stack)
     *   then restores PID task context → task_ran_at is set
     * • scheduling_latency = task_ran_at - isr_exited_at       */
}

/* ============================================================
 * TASK 1: PID Controller Task (HIGH PRIORITY)
 *
 * LATENCY 2 — SCHEDULING LATENCY demonstrated here:
 *   The gap between ISR exit and this task's first instruction
 *   is the scheduling latency.
 *   FreeRTOS: ~20-50 cycles on Cortex-M3 (PendSV overhead +
 *   context restore: pop 8 regs from TCB stack)
 *
 * LATENCY 3 — TASK SWITCHING LATENCY = full chain:
 *   IRQ fires → ISR enters → ISR runs → PendSV → task runs
 *   = interrupt_latency + isr_execution + scheduling_latency
 * ============================================================ */
static void vPidTask(void *pvParameters)
{
    UartFrame_t frame;
    (void)pvParameters;

    for (;;)
    {
        /* Block until UART ISR signals new data                */
        /* Task is in BLOCKED state here — consuming 0% CPU     */
        if (xSemaphoreTake(xPidSemaphore, portMAX_DELAY) == pdTRUE)
        {
            /* ---- MEASURE: scheduling latency ---- */
            task_ran_at        = dwt_get();
            scheduling_latency = task_ran_at - isr_exited_at;
            switching_latency  = task_ran_at - irq_fired_at;
            /* scheduling_latency ≈ 20-50 cycles @ 54 MHz       */
            /* switching_latency  = full end-to-end delay        */

            /* Receive the frame from queue                      */
            if (xQueueReceive(xUartQueue, &frame, 0) == pdTRUE)
            {
                /* Process PID data — mirrors your PFE MCU code */
                /* In your PFE: MCU read vitesse from FPGA via  */
                /* APB bus and updated OLED display              */
                (void)frame.vitesse;
                (void)frame.erreur;
                (void)frame.commande;
            }

            /* Send latency telemetry back over UART (optional) */
            /* Format: same 23-byte ASCII frame from PFE        */
        }
    }
}

/* ============================================================
 * TASK 2: Monitoring Task (LOW PRIORITY)
 * Periodically prints latency measurements
 * Equivalent to your OLED display refresh task
 * ============================================================ */
static void vMonitorTask(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        /* Print every 500 ms                                   */
        vTaskDelay(pdMS_TO_TICKS(500));

        /*
         * Expected values @ 54 MHz:
         * ┌────────────────────────┬──────────┬──────────────┐
         * │ Latency type           │ Cycles   │ Time (ns)    │
         * ├────────────────────────┼──────────┼──────────────┤
         * │ Interrupt latency      │ ~12      │ ~222 ns      │
         * │ Scheduling latency     │ ~20-50   │ ~370-925 ns  │
         * │ Task switching latency │ ~32-62+  │ ~593-1148 ns │
         * └────────────────────────┴──────────┴──────────────┘
         * Compare: your FPGA VHDL pipeline = 2 cycles = 74 ns
         * → FPGA hardware response is 3-15x faster than RTOS
         *   software path — this is why your PID ran in VHDL!
         */

        /* In real code: send via UART or display on OLED       */
        /* uart_printf("IRQ_LAT=%lu SCH_LAT=%lu SW_LAT=%lu\r\n",
                        interrupt_latency,
                        scheduling_latency,
                        switching_latency); */
    }
}

/* ============================================================
 * MAIN: System initialisation
 * ============================================================ */
int main(void)
{
    /* Enable DWT cycle counter for latency measurement         */
    dwt_enable();

    /* Create communication primitives                          */
    xUartQueue    = xQueueCreate(8, sizeof(UartFrame_t));
    xPidSemaphore = xSemaphoreCreateBinary();

    /* Create tasks
     * PID task: HIGH priority  (= your real-time critical task)
     * Monitor : LOW  priority  (= your OLED display task)     */
    xTaskCreate(vPidTask,     "PID",     256, NULL, 3, NULL);
    xTaskCreate(vMonitorTask, "Monitor", 128, NULL, 1, NULL);

    /* Configure SysTick for FreeRTOS tick = 1 ms
     * SysTick reload = 53999 cycles @ 54 MHz
     * FreeRTOS does this internally via port.c on Cortex-M3   */

    /* Configure UART IRQ in NVIC
     * Priority must be >= configMAX_SYSCALL_INTERRUPT_PRIORITY
     * so ISR-safe FreeRTOS APIs (xQueueSendFromISR) work      */

    /* Start the FreeRTOS scheduler
     * After this: SysTick and PendSV drive everything         */
    vTaskStartScheduler();

    /* Should never reach here                                  */
    for (;;) {}
    return 0;
}
