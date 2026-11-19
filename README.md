# Wallship (Battle Ship) — Embedded System & Heuristic Search AI

A modular, HAL-independent C implementation of an 8×7 Battleship-style tabletop game designed for the **STM32 Nucleo-144 (STM32F767ZI)** microcontroller, complete with a custom Heuristic Search AI engine and a Windows CLI desktop simulator.

## Architecture & Key Features
- **HAL-Independent Core Engine:** Decouples core game state, rule validation, and AI decision-making from hardware-specific drivers (`wallship_types.h`, `wallship_game_rules.h`), allowing the exact same C codebase to run on both embedded hardware and a desktop PC simulator.
- **Heuristic Search AI (`wallship_heuristic.h`, `wallship_ai.h`):**
  - Supports three difficulty profiles: `AI_RANDOM`, `AI_BALANCED`, and `AI_AGGRESSIVE`.
  - Uses probability density scoring (**Hunt Mode**) combined with directional adjacency weighting (**Target Mode**) to prioritize cells adjacent to or aligned with damaged ships, paired with an unbiased `Xorshift32` PRNG.
- **Hardware Integration (STM32F767ZI):**
  - **224-Button Switch Matrix:** Scanned in real time using Timer Interrupts (`TIM6` at 200 µs intervals) with software debouncing.
  - **224 WS2812B Addressable RGB LEDs:** Driven via `TIM1` PWM + DMA channels for smooth visual feedback with minimal CPU overhead.
  - **Display & UI:** Status and score feedback via SPI TFT (`ILI9341`) and I2C LCD (`1602A`).
- **Desktop CLI Simulator (`wallship_pve.c`, `wallship_pvp.c`):** Enables rapid logic and AI stress-testing via terminal before flashing to the microcontroller.

## Tech Stack
- **Language:** Embedded C (C99 / C11)
- **Microcontroller & Peripherals:** STM32 Nucleo-144 (`STM32F767ZI`), Timer Interrupts (`TIM1`, `TIM6`), DMA, GPIO Matrix, SPI, I2C
- **Tools:** STM32CubeIDE, GCC / MinGW, Windows PowerShell

## Running the Desktop Simulator
1. Clone this repository and open **Windows PowerShell** in the project directory.
2. Run the PowerShell build-and-play script (choose AI profile: `random`, `balanced`, or `aggressive`, followed by an optional RNG seed):
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\play_wallship.ps1 balanced 123
   ```
   *(Optional: Append `--reveal` to inspect AI ship placements during testing.)*

---
*Note: Developed as a collaborative team project for Microcontroller Application and Development at King Mongkut's Institute of Technology Ladkrabang (KMITL).*
