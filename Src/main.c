/**
 * CAN bus bootloader for GD32F30x
 * Size target: 1 flash page (2048 bytes)
 * Uses backup registers for magic word or GPIO state
 * Used FWDG (reset at any receive msg) for prevent
 * firmware stacking
 */

#include "gd32f30x.h"
#include "bootloader.h"

#define MAGIC_WORD 0x424C454EUL /* ASCII: 'BLEN' */

/* Minimal clock initialization */
static inline void clock_init(void)
{
    /* reset the RCU clock configuration to the default reset state */
    /* Set IRC8MEN bit */
    RCU_CTL |= RCU_CTL_IRC8MEN;

    volatile uint32_t i;
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV2;
    for (i = 0; i < 20000; i++)
        ;

    RCU_CFG0 &= ~RCU_CFG0_SCS;

    /* reset HXTALEN, CKMEN and PLLEN bits */
    RCU_CTL &= ~(RCU_CTL_PLLEN | RCU_CTL_CKMEN | RCU_CTL_HXTALEN);
    /* disable all interrupts */
    RCU_INT = 0x009f0000U;

    /* reset HXTALBPS bit */
    RCU_CTL &= ~(RCU_CTL_HXTALBPS);

    /* Reset CFG0 and CFG1 registers */
    RCU_CFG0 = 0x00000000U;
    RCU_CFG1 = 0x00000000U;

    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* enable IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;

    /* wait until IRC8M is stable or the startup time is longer than IRC8M_STARTUP_TIMEOUT */
    do
    {
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC8MSTB);
    } while ((0U == stab_flag) && (IRC8M_STARTUP_TIMEOUT != timeout));

    /* if fail */
    if (0U == (RCU_CTL & RCU_CTL_IRC8MSTB))
    {
        while (1)
        {
        }
    }

    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

    /* select IRC8M as system clock */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_IRC8M;

    /* wait until IRC8M is selected as system clock */
    while (0U != (RCU_CFG0 & RCU_SCSS_IRC8M))
    {
    }

    /* Enable backup domain */
    RCU_REG_VAL(RCU_PMU) |= BIT(RCU_BIT_POS(RCU_PMU));
    RCU_REG_VAL(RCU_BKPI) |= BIT(RCU_BIT_POS(RCU_BKPI));
    PMU_CTL |= PMU_CTL_BKPWEN;
    BKP_TPCS |= (uint16_t)(BKP_FLAG_TAMPER >> ((uint8_t)8U));
}

/* Write magic word to RTC backup register */
static inline void write_magic_word(uint32_t value)
{
    /* Write to backup register */
    BKP_DATA0 = (uint16_t)value & 0x0000ffff;
    BKP_DATA1 = (uint16_t)(value >> 16);
}

/* Main bootloader logic */
int main(void)
{
    /* Initialize clocks */
    clock_init();

#ifdef BL_USE_MAGICWORD
#ifdef BL_CHECK_EEPROM
    if (((BKP_DATA_GET(BKP_DATA0) | BKP_DATA_GET(BKP_DATA1) << 16) != MAGIC_WORD) && (*(volatile uint16_t *)EEPROM_START_ADDR == EEPROM_MAGIC))
    /* Magic word is not correct AND flash is initialized - jump to application */
#else
    if ((BKP_DATA_GET(BKP_DATA0) | BKP_DATA_GET(BKP_DATA1) << 16) != MAGIC_WORD)
    /* Magic word is not correct - jump to application */
#endif
#else
    if ((!GPIO_ISTAT(BL_GPIO_PORT) & (BL_GPIO_PIN)))
    /* GPIO is LOW state - jump to application */
#endif
    {
        jump2app(APP_START_ADDR);
    }

    /* Magic word is set or flash is NOT initialized - stay in bootloader */
    /* Clear magic word to indicate we're in bootloader */
    write_magic_word(0U);

    /* Run STM32-compatible bootloader protocol */
    bootloader_run();

    /* Should never reach here */
    while (1)
        ;
}
