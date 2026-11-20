#include <cmsis_os.h>
#include "stm32f4xx_hal.h"
#include "u8g2.h"
#include "soft_i2c.h"

#define TX_TIMEOUT        100

#define USE_I2C_INTERFACE
//#define USE_SPI_INTERFACE


/* SPI Interface */
SPI_HandleTypeDef *U8G2_SPI_HANDLE;

/* I2C Interface */
I2C_HandleTypeDef *U8G2_I2C_HANDLE;
extern I2C_HandleTypeDef hi2c2;

static uint8_t oled_device_address = 0x3C;
static uint8_t oled_bus_probed = 0U;

static uint8_t oled_probe_bus(void)
{
    const uint8_t addresses[] = {0x3C, 0x3D};

    for (uint32_t i = 0; i < sizeof(addresses); ++i)
    {
        const uint8_t address = addresses[i];
        if (U8G2_I2C_HANDLE->Instance == I2C_SOFT)
        {
            if (SOFT_I2C_IsDeviceReady((uint8_t)(address << 1)))
            {
                oled_device_address = address;
                return 1U;
            }
        }
        else if (HAL_I2C_IsDeviceReady(U8G2_I2C_HANDLE, (uint16_t)(address << 1), 2, TX_TIMEOUT) == HAL_OK)
        {
            oled_device_address = address;
            return 1U;
        }
    }

    if (U8G2_I2C_HANDLE->Instance == I2C_SOFT)
    {
        for (uint32_t i = 0; i < sizeof(addresses); ++i)
        {
            const uint8_t address = addresses[i];
            if (HAL_I2C_IsDeviceReady(&hi2c2, (uint16_t)(address << 1), 2, TX_TIMEOUT) == HAL_OK)
            {
                U8G2_I2C_HANDLE = &hi2c2;
                oled_device_address = address;
                return 1U;
            }
        }
    }

    return 0U;
}


uint8_t u8x8_stm32_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    /* STM32 supports HW SPI, Remove unused cases like U8X8_MSG_DELAY_XXX & U8X8_MSG_GPIO_XXX */
    switch (msg)
    {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            /* Insert codes for initialization */
            break;
        case U8X8_MSG_DELAY_MILLI:
            /* ms Delay */
            osDelay(arg_int);
            break;

#ifdef USE_SPI_INTERFACE
            /* SPI Interface */
        case U8X8_MSG_GPIO_CS:
            /* Insert codes for SS pin control */
            //HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, arg_int);
            break;
        case U8X8_MSG_GPIO_DC:
            /* Insert codes for DC pin control */
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, arg_int);
            break;
        case U8X8_MSG_GPIO_RESET:
            /* Insert codes for RST pin control */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, arg_int);
            break;
#endif
    }
    return 1;
}

#ifdef USE_SPI_INTERFACE
uint8_t u8x8_byte_stm32_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg)
    {
        case U8X8_MSG_BYTE_SEND:
            /* Insert codes to transmit data */
            if (HAL_SPI_Transmit(&SPI_HANDLE, arg_ptr, arg_int, TX_TIMEOUT) != HAL_OK) return 0;
            break;
        case U8X8_MSG_BYTE_INIT:
            /* Insert codes to begin SPI transmission */
            break;
        case U8X8_MSG_BYTE_SET_DC:
            /* Control DC pin, U8X8_MSG_GPIO_DC will be called */
            u8x8_gpio_SetDC(u8x8, arg_int);
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            /* Select slave, U8X8_MSG_GPIO_CS will be called */
            u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level);
            osDelay(1);
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            osDelay(1);
            /* Insert codes to end SPI transmission */
            u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
            break;
        default:
            return 0;
    }
    return 1;
}
#endif

#ifdef USE_I2C_INTERFACE

uint8_t u8x8_byte_stm32_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    /* u8g2/u8x8 will never send more than 32 bytes between START_TRANSFER and END_TRANSFER */
    static uint8_t buffer[32];
    static uint8_t buf_idx;
    uint8_t *data;

    switch (msg)
    {
        case U8X8_MSG_BYTE_SEND:
            data = (uint8_t *) arg_ptr;
            while (arg_int > 0)
            {
                buffer[buf_idx++] = *data;
                data++;
                arg_int--;
            }
            break;
        case U8X8_MSG_BYTE_INIT:
            /* add your custom code to init i2c subsystem */
            break;
        case U8X8_MSG_BYTE_SET_DC:
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            if (!oled_bus_probed)
            {
                oled_bus_probed = 1U;
                if (!oled_probe_bus())
                    return 0;
            }
            if (U8G2_I2C_HANDLE->Instance == I2C_SOFT)
                return SOFT_I2C_Master_Transmit((uint8_t)(oled_device_address << 1), buffer, buf_idx);
            return HAL_I2C_Master_Transmit(U8G2_I2C_HANDLE, (uint16_t)(oled_device_address << 1), buffer, buf_idx, TX_TIMEOUT) == HAL_OK;
        default:
            return 0;
    }
    return 1;
}

#endif
