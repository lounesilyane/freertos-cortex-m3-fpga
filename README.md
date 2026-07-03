# FPGA-Based PID DC Motor Speed Control with Cortex-M3 SoC
### Tang Nano 4K (GW1NSR-4C) | VHDL | Cortex-M3 Soft-Core | FreeRTOS (in progress)

![Platform](https://img.shields.io/badge/Platform-Tang%20Nano%204K-blue)
![FPGA](https://img.shields.io/badge/FPGA-GW1NSR--4C-orange)
![Core](https://img.shields.io/badge/Core-ARM%20Cortex--M3-red)
![Language](https://img.shields.io/badge/VHDL-IEEE%201076--1993-green)
![License](https://img.shields.io/badge/License-MIT-lightgrey)
![Status](https://img.shields.io/badge/Status-Active%20Research-brightgreen)

---

## Overview

This project implements a **closed-loop PID DC motor speed controller** entirely on an FPGA,
combining a **VHDL-based PID engine** with an **ARM Cortex-M3 soft-core SoC** for telemetry,
display, and supervisory control.

Designed and validated as a Master's thesis (PFE) in Electronics of Embedded Systems,
University Akli Mohand Oulhadj — Bouira, Algeria. **Final grade: 18.5/20 — Mention Excellent.**

The project is now being extended toward **FreeRTOS integration on the Cortex-M3 soft-core**
as part of doctoral research preparation in real-time embedded systems.

---

## Hardware Architecture

```
                    ┌─────────────────────────────────────────┐
                    │         Tang Nano 4K (GW1NSR-4C)        │
                    │                                          │
  Quadrature ──────►│  ┌──────────┐      ┌──────────────────┐ │
  Encoder           │  │  Speed   │      │   Cortex-M3      │ │
                    │  │ Measure  │─────►│   Soft-Core      │─┼──► UART (115200)
  PWM ─────────────►│  │  (Stro.) │      │   @ 54 MHz       │ │
  → L298N H-Bridge  │  └──────────┘      └────────┬─────────┘ │
  → JGA25-370 Motor │                             │           │
                    │  ┌──────────────────────────┘           │
                    │  │  PID Controller (VHDL)               │
                    │  │  KP=13  KI=24  KD=0  SCALE=16        │
                    │  │  TE_TICKS=270000 (10ms @ 27MHz)      │
                    │  │  PERIOD_TICKS=2700 (10kHz PWM)       │
                    │  └──────────────────────────────────────┘
                    └─────────────────────────────────────────┘
                              │ I2C (via Arduino Uno relay)
                              ▼
                         OLED SSD1306
```

---

## Key Specifications

| Parameter | Value |
|---|---|
| FPGA | Gowin GW1NSR-4C (Tang Nano 4K) |
| Soft-core | ARM Cortex-M3 (Gowin EMPU) @ 54 MHz |
| PID sampling period | 10 ms (TE_TICKS = 270,000 @ 27 MHz) |
| PWM frequency | 10 kHz (PERIOD_TICKS = 2,700 @ 27 MHz) |
| UART baud rate | 115,200 baud (BAUDDIV = 469 @ 54 MHz) |
| UART frame | 23-byte ASCII telemetry frame |
| Motor | JGA25-370 DC motor with quadrature encoder |
| H-Bridge | L298N |
| PID parameters | KP=13, KI=24, KD=0, SCALE=16 (>> 4) |
| Speed measurement | Stroboscopic method — accuracy 99.6% |
| Steady-state error | 0% (validated) |

---

## System Identification

Open-loop step response identified via MATLAB (Nelder-Mead / fminsearch):

```
         12.77
G(s) = ─────────────    R² = 0.947
        0.01915s + 1
```

- Time constant τ = 19.15 ms
- Static gain K = 12.77
- Normalization: `duty_est = mean(pwm) × PERIOD_TICKS / 2700`

---

## Repository Structure

```
📁 freertos-cortex-m3-fpga/
│
├── 📁 vhdl/
│   ├── pid_controller.vhd       ← Fixed-point PID (Q15, ieee.numeric_std)
│   ├── phase5_top.vhd           ← Top-level SoC integration
│   ├── speed_measure.vhd        ← Stroboscopic encoder measurement
│   ├── uart_tx.vhd              ← UART transmitter (BAUDDIV=469)
│   └── irq_latency_demo.vhd    ← Interrupt latency demo (Step 4)
│
├── 📁 cortex-m3/
│   ├── main.c                   ← Cortex-M3 supervisory code
│   ├── uart_handler.c           ← 23-byte frame parser
│   ├── FreeRTOSConfig.h         ← RTOS configuration (in progress)
│   └── latency_demo_cm3.c      ← RTOS latency measurement demo
│
├── 📁 identification/
│   ├── identify_motor.py        ← scipy Nelder-Mead identification
│   ├── step_response.py         ← Step response plotting
│   └── data/
│       └── open_loop_data.csv   ← Raw UART telemetry data
│
├── 📁 docs/
│   ├── memoir_pfe.pdf           ← Master's thesis (French)
│   └── conference_article.pdf  ← Conference paper (in progress)
│
└── README.md
```

---

## VHDL Implementation Notes

All VHDL strictly targets **GowinEDA** synthesis constraints:

- No VHDL-2008 `wait` constructs
- No multi-driver conflicts (CV0013)
- Single synchronous process per output signal
- Fixed-point arithmetic: `ieee.numeric_std` signed types (Q15)
- Internal UART signal routing to avoid multi-driver on SoC integration

---

## FreeRTOS Extension (In Progress)

Current research extends this bare-metal SoC toward a **FreeRTOS-based
multi-task architecture** on the Cortex-M3 soft-core:

```
Planned task architecture:
├── vPidMonitorTask   (HIGH priority)  ← reads FPGA telemetry via UART
├── vDisplayTask      (MED  priority)  ← updates OLED via I2C
├── vSetpointTask     (LOW  priority)  ← handles user input (buttons)
└── Idle Task                          ← low-power mode hook
```

FreeRTOS port: `GCC/ARM_CM3` — same port as STM32F103,
directly compatible with Gowin EMPU Cortex-M3 soft-core.

**Measured latencies @ 54 MHz:**
| Type | Cycles | Time |
|---|---|---|
| VHDL hardware IRQ response | 2 cycles | 74 ns |
| Cortex-M3 interrupt latency | ~12 cycles | ~222 ns |
| FreeRTOS scheduling latency | ~20-50 cycles | ~370-925 ns |

---

## Results

- ✅ Zero steady-state error validated across full speed range
- ✅ Stroboscopic speed measurement accuracy: **99.6%**
- ✅ System identification R² = **0.947**
- ✅ Stable closed-loop response, no oscillation
- ✅ Live UART telemetry at 115,200 baud (23-byte ASCII frame)
- ✅ OLED real-time display via Arduino Uno I2C relay

---

## Tools & Environment

| Tool | Purpose |
|---|---|
| GowinEDA | FPGA synthesis, place & route |
| GCC ARM Toolchain | Cortex-M3 C compilation |
| VSCode + Cortex-Debug | Development & debugging |
| MATLAB / Python scipy | System identification |
| Logic Analyzer | UART & signal debugging |
| Arduino IDE | OLED I2C relay (SSD1306/U8g2) |

---

## Author

**Lounes**
M.Sc. Electronics of Embedded Systems
University Akli Mohand Oulhadj — Bouira, Algeria
*Doctoral research candidate — Real-Time Embedded Systems*

---

## License

MIT License — free to use for academic and research purposes.
If you use this work in a publication, please cite accordingly.
