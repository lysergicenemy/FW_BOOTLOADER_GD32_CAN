/**
 * CAN bus based STM32 USART bootloader protocol for GD32 MCU
 * Compatible with STM32CubeProgrammer
 *
 * Supports: Get, Get Version, Get ID, Read Memory, Write Memory, Erase Memory, Go
 * Default CAN bus config: 250kbps, PA11-CAN_RX, PA12-CAN_TX
 */

#include "gd32f30x.h"
#include "bootloader.h"

can_trasnmit_message_struct tx_msg;
can_receive_message_struct rx_msg;

/* Simple inline functions */
static inline void send_byte(uint8_t data)
{
    while (can_flag_get(CAN0, CAN_FLAG_TS) == SET)
        ;
    tx_msg.tx_sfid = CAN_TARGET_ID;
    tx_msg.tx_ff = CAN_FF_STANDARD;
    tx_msg.tx_ft = CAN_FT_DATA;
    tx_msg.tx_dlen = 1U;
    tx_msg.tx_data[0] = data;
    can_message_transmit(CAN0, &tx_msg);
}

static inline uint8_t receive_byte(void)
{
    while ((uint8_t)(CAN_RFIFO1(CAN0) & CAN_RFIF_RFL_MASK) == 0)
        ;
    can_message_receive(CAN0, CAN_FIFO1, &rx_msg);

    /* reload watchdog if any read */
    FWDGT_CTL = FWDGT_KEY_RELOAD;

    return rx_msg.rx_data[0];
}

static inline uint8_t xor_checksum(const uint8_t *data, uint32_t len)
{
    uint8_t sum = 0;
    while (len--)
        sum ^= *data++;
    return sum;
}

static void flash_erase_page(uint32_t page_addr)
{
    FMC_REG_VAL(FMC_FLAG_BANK0_END) |= BIT(FMC_BIT_POS(FMC_FLAG_BANK0_END));
    FMC_REG_VAL(FMC_FLAG_BANK0_WPERR) |= BIT(FMC_BIT_POS(FMC_FLAG_BANK0_WPERR));
    FMC_REG_VAL(FMC_FLAG_BANK0_PGERR) |= BIT(FMC_BIT_POS(FMC_FLAG_BANK0_PGERR));

    while (FMC_STAT0 & FMC_STAT0_BUSY)
        ;

    FMC_CTL0 |= FMC_CTL0_PER;
    FMC_ADDR0 = page_addr;
    FMC_CTL0 |= FMC_CTL0_START;

    while (FMC_STAT0 & FMC_STAT0_BUSY)
        ;
    /* reset the PER bit */
    FMC_CTL0 &= ~FMC_CTL0_PER;
}

/*!
    \brief      initialize CAN and filter
    \param[in]  none
    \param[out] none
    \retval     none
*/
static inline void can_networking_init(can_baud_t can_baud)
{
    can_parameter_struct can_parameter;
    can_filter_parameter_struct can_filter;

    can_struct_para_init(CAN_INIT_STRUCT, &can_parameter);
    can_struct_para_init(CAN_FILTER_STRUCT, &can_filter);

    /* configure CAN0 GPIO */
    RCU_REG_VAL(RCU_GPIOA) |= BIT(RCU_BIT_POS(RCU_GPIOA));
    RCU_REG_VAL(RCU_AF) |= BIT(RCU_BIT_POS(RCU_AF));
    RCU_REG_VAL(RCU_CAN0) |= BIT(RCU_BIT_POS(RCU_CAN0));

    gpio_init(GPIOA, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_11);   // CAN0 RX
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12); // CAN0 TX

    /* initialize CAN register */
    can_deinit(CAN0);

    /* initialize CAN */
    can_parameter.time_triggered = DISABLE;
    can_parameter.auto_bus_off_recovery = ENABLE;
    can_parameter.auto_wake_up = DISABLE;
    can_parameter.no_auto_retrans = ENABLE;
    can_parameter.rec_fifo_overwrite = ENABLE; // DISABLE;
    can_parameter.trans_fifo_order = DISABLE;
    can_parameter.working_mode = CAN_NORMAL_MODE;
    can_parameter.resync_jump_width = CAN_BT_SJW_1TQ;
    can_parameter.time_segment_1 = CAN_BT_BS1_5TQ;
    can_parameter.time_segment_2 = CAN_BT_BS2_2TQ;
    can_parameter.prescaler = can_baud;
    can_init(CAN0, &can_parameter);

    /* initialize filter */
    /* CAN0 filter number */

    can_filter.filter_number = 0;
    /* initialize filter */
    can_filter.filter_mode = CAN_FILTERMODE_LIST;
    can_filter.filter_bits = CAN_FILTERBITS_16BIT;

    can_filter.filter_list_high = CAN_HOST_ID << 5;
    can_filter.filter_list_low = 0;
    can_filter.filter_mask_high = 0;
    can_filter.filter_mask_low = 0;

    can_filter.filter_fifo_number = CAN_FIFO1;
    can_filter.filter_enable = ENABLE;
    can_filter_init(&can_filter);

    /* init tx/rx messages */
    // TX MSG 1
    can_struct_para_init(CAN_TX_MESSAGE_STRUCT, &tx_msg); // clear struct
    // RX MSG 1
    can_struct_para_init(CAN_RX_MESSAGE_STRUCT, &rx_msg);
}

/**
 * @brief confiure FWDGT counter clock: 40KHz(IRC40K) / 64 = 156.25 Hz
 * Max timeout 26.2s
 * 
 * @param cval 0...0xfff - timeout value
 */
static inline void fwdg_init(uint32_t cval)
{
    /* enable write access to FWDGT_PSC,and FWDGT_RLD */
    FWDGT_CTL = FWDGT_WRITEACCESS_ENABLE;
    while (RESET != (FWDGT_STAT & FWDGT_STAT_PUD))
        ;
    /* configure FWDGT */
    FWDGT_PSC = (uint32_t)FWDGT_PSC_DIV256;
    while (RESET != (FWDGT_STAT & FWDGT_STAT_RUD))
        ;
    FWDGT_RLD = (BITS(0, 11) & ((uint32_t)(0xfff) << 0));
    /* reload the counter */
    FWDGT_CTL = FWDGT_KEY_RELOAD;
    /* After xx seconds to generate a reset */
    FWDGT_CTL = FWDGT_KEY_ENABLE;
}

/**
 * Handle Get command (0x00) - minimal response
 */
static void handle_get(void)
{
    send_byte(6); /* 7 bytes follow (version + 6 commands, N = bytes - 1) */
    send_byte(BOOTLOADER_VERSION);
    send_byte(CMD_GET);
    send_byte(CMD_GET_ID);
    send_byte(CMD_READ_MEMORY);
    send_byte(CMD_WRITE_MEMORY);
    send_byte(CMD_ERASE_MEMORY);
    send_byte(CMD_GO);
    send_byte(ACK_BYTE);
}

/**
 * Handle Get ID command (0x02) - returns GD32E230 chip ID (0x0440)
 */
static void handle_get_id(void)
{
    send_byte(1); /* N = 1 (2 bytes of ID follow) */
    send_byte(PID_MSB);
    send_byte(PID_LSB);
    send_byte(ACK_BYTE);
}

/**
 * Handle Read Memory command (0x11) - reads memory content
 */
static void handle_read_memory(void)
{
    uint16_t n, i;
    uint8_t *ptr;
    addr_t addr;

    /* Get address */
    for (i = 0; i < 4; i++)
        addr.byte[3U - i] = receive_byte();
    if (xor_checksum(addr.byte, 4) != receive_byte())
    {
        send_byte(NACK_BYTE);
        return;
    }

    send_byte(ACK_BYTE);

    /* Get number of bytes to read */
    n = receive_byte();
    if ((n ^ receive_byte()) != 0xFF)
    {
        send_byte(NACK_BYTE);
        return;
    }

    send_byte(ACK_BYTE);

    /* Read and send data */
    ptr = (uint8_t *)addr.word;
    for (i = 0; i <= n; i++) // n+1 bytes, i is uint16_t, no overflow
    {
        send_byte(ptr[i]);
    }
}

/**
 * Handle Write Memory command (0x31) - minimal
 */
static void handle_write_memory(void)
{
    uint32_t i, data_len;
    uint32_t xor_sum;
    addr_t addr;
    uint8_t data[MAX_WRITE_SIZE];

    /* Get address (big-endian) */
    for (i = 0; i < 4; i++)
        addr.byte[3U - i] = receive_byte();

    if (xor_checksum(addr.byte, 4) != receive_byte())
    {
        send_byte(NACK_BYTE);
        return;
    }

    /* if xor ok - ack */
    send_byte(ACK_BYTE);

    /* Get N (number of bytes - 1) */
    xor_sum = receive_byte();

    data_len = xor_sum + 1; // actual number of bytes

    /* Receive data and compute checksum */
    for (i = 0; i < data_len; i++)
    {
        data[i] = receive_byte();
        xor_sum ^= data[i];
    }

    if (xor_sum != receive_byte())
    {
        send_byte(NACK_BYTE);
        return;
    }

    /* Program flash */
    flash_unlock();

    uint32_t *word_ptr = (uint32_t *)data;
    uint32_t word_count = data_len / 4;

    /* Wait BSY flag is reset and set PG */
    while (FMC_STAT0 & FMC_STAT0_BUSY)
        ;
    FMC_CTL0 |= FMC_CTL0_PG;

    /* Write pages */
    for (i = 0; i < word_count; i++)
    {
        REG32(addr.word + i * 4) = word_ptr[i];
        while (FMC_STAT0 & FMC_STAT0_BUSY)
            ;
    }

    /* Clear PG */
    FMC_CTL0 &= ~FMC_CTL0_PG;

    /* clear flags */
    FMC_REG_VAL(FMC_FLAG_BANK0_END) |= BIT(FMC_BIT_POS(FMC_FLAG_BANK0_END));
    FMC_REG_VAL(FMC_FLAG_BANK0_WPERR) |= BIT(FMC_BIT_POS(FMC_FLAG_BANK0_WPERR));
    FMC_REG_VAL(FMC_FLAG_BANK0_PGERR) |= BIT(FMC_BIT_POS(FMC_FLAG_BANK0_PGERR));

    flash_lock();

    send_byte(ACK_BYTE);
}

/**
 * Handle Erase Memory command (0x43) - global erase only
 */
static void handle_erase_memory(void)
{
    uint8_t n_pages_minus1, i;

    /* Receive N (number of pages - 1) or 0xFF for global erase */
    n_pages_minus1 = receive_byte();

    if (n_pages_minus1 != 0xFF)
    {
        for (i = 0; i <= n_pages_minus1; i++)
        {
            receive_byte();
        }
    }

    /* xor */
    uint8_t xor = receive_byte();

    /* check if global erase */
    if ((n_pages_minus1 ^ xor) == 0xFF)
    {
        /* Unlock flash and erase all application pages */
        flash_unlock();

        for (uint32_t page = 1; page < FLASH_TOTAL_PAGES; page++) /* Skip page 0 (bootloader) */
        {
            uint32_t page_address = FLASH_BASE + (page * FLASH_PAGE_SIZE);
            flash_erase_page(page_address);
        }

        flash_lock();

        send_byte(ACK_BYTE);
    }
}

void jump2app(void)
{
    __disable_irq();
    SCB->VTOR = APP_START_ADDR;
    uint32_t sp = *(volatile uint32_t *)APP_START_ADDR;
    uint32_t pc = *(volatile uint32_t *)(APP_START_ADDR + 4);
    __set_MSP(sp);
    ((void (*)(void))pc)();
}

/**
 * Handle Go command (0x21) - SIMPLE VERSION. always jumps to APP_START_ADDR
 */
static void handle_go(void)
{
    uint32_t i;
    /* 4x address + xor */
    for (i = 0; i < 5; i++)
    {
        receive_byte();
    }
    /* send ACK */
    send_byte(ACK_BYTE);
    /* delay for jump2app */
    for (i = 255U; i != 0U; i--)
    {
        __NOP();
    }
    /* Jump to application */
    jump2app();
}

/**
 * Main bootloader protocol
 */
void bootloader_run(void)
{
    uint8_t cmd, comp;

    fwdg_init(0xfff);
    can_networking_init(CAN_BAUD_250K);

    /* Wait for sync */
    while (receive_byte() != SYNC_BYTE)
        ;
    send_byte(ACK_BYTE);

    /* Command loop */
    while (1)
    {
        cmd = receive_byte();
        comp = receive_byte();

        if ((cmd ^ comp) != 0xFF)
        {
            send_byte(NACK_BYTE);
        }
        else
        {
            /* ack for command */
            send_byte(ACK_BYTE);

            switch (cmd)
            {
            case CMD_GET:
                handle_get();
                break;
            case CMD_GET_ID:
                handle_get_id();
                break;
            case CMD_READ_MEMORY:
                handle_read_memory();
                break;
            case CMD_WRITE_MEMORY:
                handle_write_memory();
                break;
            case CMD_ERASE_MEMORY:
                handle_erase_memory();
                break;
            case CMD_GO:
                handle_go();
                break;
            default:
                send_byte(NACK_BYTE);
                break;
            }
        }
    }
}