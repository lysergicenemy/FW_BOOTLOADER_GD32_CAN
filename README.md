# GD32F30x CAN Bootloader

Minimal CAN bootloader for GD32F30x (Cortex-M4F) microcontrollers.  
Compatible with the STM32 USART bootloader protocol — adapted for CAN transport.  
Fits in a single 2KB flash page.

## Features

- **Communication**: CAN 2.0 (standard IDs), 250 kbit/s (default)
- **Protocol**: STM32 CubeProgrammer-compatible command set over CAN
- **Supported Commands**:
  | Command | Code | Description |
  |---------|------|-------------|
  | Get     | 0x00 | List supported commands |
  | Get ID  | 0x02 | Return chip PID (0x0442) |
  | Read Memory  | 0x11 | Read flash/RAM (polling) |
  | Write Memory | 0x31 | Write flash (half-word aligned) |
  | Erase Memory | 0x43 | Global erase (all pages except bootloader) |
  | Go     | 0x21 | Jump to application |
- **Boot Modes**:
  1. **Magic word** — checks RTC backup registers for `0x424C454E` ("BLEN")
  2. **GPIO pin state** — configurable via `BL_GPIO_PORT` / `BL_GPIO_PIN`
- **Clock**: Internal 8MHz RC oscillator (IRC8M), no external crystal needed
- **Minimal Footprint**: ~1.5KB code, ~500B RAM (including 1KB stack)
- **No interrupt dependencies**: CAN operates in polling mode
- **No standard library**: Built with `-nostartfiles -nostdlib -ffreestanding`

## Memory Layout

| Region | Address | Size | Content |
|--------|---------|------|---------|
| FLASH  | `0x08000000` | 2 KB | Bootloader (page 0) |
| FLASH  | `0x08000800` | 126 KB | Application (pages 1–63) |
| RAM    | `0x20000000` | ~1 KB | .data, .bss, stack, heap |


## Hardware Configuration

### CAN Bus

| Signal | Port | Function |
|--------|------|----------|
| CAN0 RX | PA11 | CAN receive |
| CAN0 TX | PA12 | CAN transmit |

### CAN Identifiers

| Role | ID |
|------|----|
| Bootloader (host) | `0x03` |
| Tool/Tester (target) | `0x04` |

### Boot Mode Selection

Define `BL_USE_MAGICWORD` in `bootloader.h` to enable magic-word mode:
```c
#define BL_USE_MAGICWORD          // Use RTC backup register check
// #undef BL_USE_MAGICWORD       // Use GPIO pin check instead
```

- **Magic word mode**: Application writes `0x424C454E` to `BKP_DATA0:1` before reset
- **GPIO mode**: Bootloader check GPIO state after any reset. LOW - jump to app, HIGH - stay in bootloader.

## Building

### Prerequisites

- ARM GCC toolchain (`arm-none-eabi-gcc`)
- `make`

### Build

```bash
make
```

### Flash

```bash
make flash          # requires J-Link + JLinkExe in PATH
```

Or manually with:

```
JLinkGDBServer -if SWD -device GD32F303CB -speed 4000
```

## Limitations

### Known Constraints

1. **Global variable initialization**
   - The startup code does **not** copy `.data` or zero `.bss`
   - Global/static variables with initializers will contain garbage
   - All state must be initialized explicitly at runtime

2. **No peripheral interrupts**
   - Only the Cortex-M4 core exceptions are mapped
   - CAN, timers, DMA etc. **cannot** use interrupt handlers

3. **Erase command**
   - Global erase only (N = `0xFF` or any value — always erases pages 1–63)
   - XOR checksum for page numbers is parsed but ignored

4. **No standard library**
   - `memcpy`, `memset`, `sprintf`, `malloc` etc. are **not available**

5. **Watchdog**
   - FWDGT is used for prevent firmware stacking (reset via `Reset_Handler`)

## Project Structure

```
.
├── GD32F303_FLASH.ld      # Linker script
├── startup_minimal.s      # Minimal Cortex-M4 startup (16 vectors only)
├── Makefile               # Build system
├── Inc/
│   ├── bootloader.h       # Bootloader config, CAN IDs, command codes
│   ├── main.h             # (future) main declarations
│   └── gd32f30x_libopt.h  # Peripheral library config
├── Src/
│   ├── main.c             # Entry point, clock init, boot decision
│   └── bootloader.c       # CAN bootloader protocol implementation
├── Drivers/
│   ├── CMSIS/             # CMSIS core and device headers
│   └── GD32F30x_standard_peripheral/  # StdPerip drivers (CAN, GPIO, RCU)
├── JLink/                 # J-Link scripts for erase/flash/reset
└── build/                 # Build artefacts
```
