/**
 * STM32 USART bootloader protocol implementation for GD32E230
 * Compatible with STM32CubeProgrammer
 *
 * Minimal implementation for <= 1KB flash
 * Supports: Get, Get ID, Write Memory, Erase Memory, Go
 * Fixed baud rate: 57600
 * RS485 DE control via PA1 (hardware)
 */

#ifndef STM32_BOOTLOADER_H
#define STM32_BOOTLOADER_H


/* Bootloader version */
#define BOOTLOADER_VERSION 0x20 /* Version 2.0 */

/* Bootloader config if use gpio comment BL_USE_MAGICWORD */
#define BL_USE_MAGICWORD
#define BL_GPIO_PORT GPIOA
#define BL_GPIO_PIN GPIO_PIN_0

/* Memory configuration */
#define APP_START_ADDR 0x08000800UL /* Application starts after 2KB bootloader */
#define FLASH_PAGE_SIZE 2048        /* GD32F303 flash page size */
#define FLASH_TOTAL_PAGES 64        /* 128KB total flash = 64 pages */
#define MAX_WRITE_SIZE 256          /* Maximum data block size (protocol allows up to 256) */

/* CAN constants */
#define CAN_HOST_ID 0x03
#define CAN_TARGET_ID 0x04

/* Protocol constants */
#define SYNC_BYTE 0x7F
#define ACK_BYTE 0x79
#define NACK_BYTE 0x1F

/* Device PID */
#define PID_MSB 0x04
#define PID_LSB 0x42

/* Command codes */
#define CMD_GET 0x00
#define CMD_GET_VERSION 0x01
#define CMD_GET_ID 0x02
#define CMD_READ_MEMORY 0x11
#define CMD_WRITE_MEMORY 0x31
#define CMD_ERASE_MEMORY 0x43
#define CMD_GO 0x21

union addr_u
{
    uint8_t byte[4];
    uint32_t word;
};
typedef union addr_u addr_t;

enum can_baud_e
{
    CAN_BAUD_500K = 1,
    CAN_BAUD_250K = 2,
    CAN_BAUD_125K = 4,
};
typedef enum can_baud_e can_baud_t;

/**
 * Initialize and run STM32 bootloader protocol
 * This function will not return until a Go command is received
 * or a system reset is triggered
 */
void bootloader_run(void);

void jump2app(void);

static inline void flash_lock(void)
{
    FMC_CTL0 |= FMC_CTL0_LK;
}

static inline void flash_unlock(void)
{
    FMC_KEY0 = UNLOCK_KEY0;
    FMC_KEY0 = UNLOCK_KEY1;
}

#endif /* STM32_BOOTLOADER_H */