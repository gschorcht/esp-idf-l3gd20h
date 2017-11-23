/*
 * Driver for L3GD20H 3-axes digital output gyroscope connected to I2C or SPI.
 * It can also be used with L3GD20 and L3G4200D.
 *
 * Part of esp-open-rtos [https://github.com/SuperHouse/esp-open-rtos]
 *
 * ---------------------------------------------------------------------------
 *
 * The BSD License (3-clause license)
 *
 * Copyright (c) 2017 Gunar Schorcht (https://github.com/gschorcht)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The information provided is believed to be accurate and reliable. The
 * copyright holder assumes no responsibility for the consequences of use
 * of such information nor for any infringement of patents or other rights
 * of third parties which may result from its use. No license is granted by
 * implication or otherwise under any patent or patent rights of the copyright
 * holder.
 */

#include <string.h>
#include <stdlib.h>

#if !defined(ESP_PLATFORM) && !defined(ESP_OPEN_RTOS) && !defined(__linux__)
#define ESP_OPEN_RTOS 1
#endif

#ifdef ESP_OPEN_RTOS  // ESP8266
#include "FreeRTOS.h"
#include "task.h"
#include "espressif/esp_common.h"
#include "espressif/sdk_private.h"
#include "esp/spi.h"
#include "i2c/i2c.h"

#elif ESP_PLATFORM  // ESP32 (ESP-IDF)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp8266_wrapper.h"
#include <errno.h>

#else  // __linux__
#include "esp8266_wrapper.h"
#include <errno.h>
#endif

#include "l3gd20h.h"

#if defined(L3GD20H_DEBUG_LEVEL_2)
#define debug(s, f, ...) printf("%s %s: " s "\n", "L3GD20H", f, ## __VA_ARGS__)
#define debug_dev(s, f, d, ...) printf("%s %s: bus %d, addr %02x - " s "\n", "L3GD20H", f, d->bus, d->addr, ## __VA_ARGS__)
#else
#define debug(s, f, ...)
#define debug_dev(s, f, d, ...)
#endif

#if defined(L3GD20H_DEBUG_LEVEL_1) || defined(L3GD20H_DEBUG_LEVEL_2)
#define error(s, f, ...) printf("%s %s: " s "\n", "L3GD20H", f, ## __VA_ARGS__)
#define error_dev(s, f, d, ...) printf("%s %s: bus %d, addr %02x - " s "\n", "L3GD20H", f, d->bus, d->addr, ## __VA_ARGS__)
#else
#define error(s, f, ...)
#define error_dev(s, f, d, ...)
#endif

// register addresses
#define L3GD20H_REG_WHO_AM_I     0x0f
#define L3GD20H_REG_CTRL1        0x20
#define L3GD20H_REG_CTRL2        0x21
#define L3GD20H_REG_CTRL3        0x22
#define L3GD20H_REG_CTRL4        0x23
#define L3GD20H_REG_CTRL5        0x24
#define L3GD20H_REG_REFERENCE    0x25
#define L3GD20H_REG_OUT_TEMP     0x26
#define L3GD20H_REG_STATUS       0x27
#define L3GD20H_REG_OUT_X_L      0x28
#define L3GD20H_REG_OUT_X_H      0x29
#define L3GD20H_REG_OUT_Y_L      0x2a
#define L3GD20H_REG_OUT_Y_H      0x2b
#define L3GD20H_REG_OUT_Z_L      0x2c
#define L3GD20H_REG_OUT_Z_H      0x2d
#define L3GD20H_REG_FIFO_CTRL    0x2e
#define L3GD20H_REG_FIFO_SRC     0x2f
#define L3GD20H_REG_IG_CFG       0x30
#define L3GD20H_REG_IG_SRC       0x31
#define L3GD20H_REG_IG_THS_XH    0x32
#define L3GD20H_REG_IG_THS_XL    0x33
#define L3GD20H_REG_IG_THS_YH    0x34
#define L3GD20H_REG_IG_THS_YL    0x35
#define L3GD20H_REG_IG_THS_ZH    0x36
#define L3GD20H_REG_IG_THS_ZL    0x37
#define L3GD20H_REG_IG_DURATION  0x38
#define L3GD20H_REG_LOW_ODR      0x39

// register structure definitions
#define L3GD20H_ZYXOR             0x80    // L3GD20H_REG_STATUS<7>
#define L3GD20H_ZOR               0x40    // L3GD20H_REG_STATUS<6>
#define L3GD20H_YOR               0x20    // L3GD20H_REG_STATUS<5>
#define L3GD20H_XOR               0x10    // L3GD20H_REG_STATUS<4>
#define L3GD20H_ZYXDA             0x08    // L3GD20H_REG_STATUS<3>
#define L3GD20H_ZDA               0x04    // L3GD20H_REG_STATUS<2>
#define L3GD20H_YDA               0x02    // L3GD20H_REG_STATUS<1>
#define L3GD20H_XDA               0x01    // L3GD20H_REG_STATUS<0>

#define L3GD20H_ANY_DATA_READY    0x0f    // L3GD20H_REG_STATUS<3:0>

#define L3GD20H_ODR               0xc0    // L3GD20H_REG_CTRL1<7:6>
#define L3GD20H_BW                0x30    // L3GD20H_REG_CTRL1<5:4>
#define L3GD20H_POWER_MODE        0x08    // L3GD20H_REG_CTRL1<3>
#define L3GD20H_Z_ENABLED         0x04    // L3GD20H_REG_CTRL1<2>
#define L3GD20H_Y_ENABLED         0x02    // L3GD20H_REG_CTRL1<1>
#define L3GD20H_X_ENABLED         0x01    // L3GD20H_REG_CTRL1<0>

#define L3GD20H_HPF_MODE          0x30    // L3GD20H_REG_CTRL3<5:4>
#define L3GD20H_HPF_CUTOFF        0x0f    // L3GD20H_REG_CTRL3<3:0>

#define L3GD20H_INT1_IG           0x80    // L3GD20H_REG_CTRL3<7>
#define L3GD20H_INT1_BOOT         0x40    // L3GD20H_REG_CTRL3<6>
#define L3GD20H_HL_ACTIVE         0x20    // L3GD20H_REG_CTRL3<5>
#define L3GD20H_PP_OD             0x10    // L3GD20H_REG_CTRL3<4>
#define L3GD20H_INT2_DRDY         0x08    // L3GD20H_REG_CTRL3<3>
#define L3GD20H_INT2_FTH          0x04    // L3GD20H_REG_CTRL3<2>
#define L3GD20H_INT2_ORUN         0x02    // L3GD20H_REG_CTRL3<1>
#define L3GD20H_INT2_EMPTY        0x01    // L3GD20H_REG_CTRL3<0>

#define L3GD20H_BLOCK_DATA_UPDATE 0x80    // L3GD20H_REG_CTRL4<7>
#define L3GD20H_BIG_LITTLE_ENDIAN 0x40    // L3GD20H_REG_CTRL4<6>
#define L3GD20H_FULL_SCALE        0x30    // L3GD20H_REG_CTRL4<5:4>

#define L3GD20H_BOOT              0x80    // L3GD20H_REG_CTRL5<7>
#define L3GD20H_FIFO_EN           0x40    // L3GD20H_REG_CTRL5<6>
#define L3GD20H_STOP_ON_FTH       0x20    // L3GD20H_REG_CTRL5<5>
#define L3GD20H_HP_ENABLED        0x10    // L3GD20H_REG_CTRL5<4>
#define L3GD20H_IG_SEL            0x0c    // L3GD20H_REG_CTRL5<3:2>
#define L3GD20H_OUT_SEL           0x03    // L3GD20H_REG_CTRL5<1:0>

#define L3GD20H_FIFO_MODE         0xe0    // L3GD20H_REG_FIFO_CTRL<7:5>
#define L3GD20H_FIFO_THRESH       0x1f    // L3GD20H_REG_FIFO_CTRL<4:0>

#define L3GD20H_FIFO_THS          0x80    // L3GD20H_REG_FIFO_SRC<7>
#define L3GD20H_FIFO_OVR          0x40    // L3GD20H_REG_FIFO_SRC<6>
#define L3GD20H_FIFO_EMPTY        0x20    // L3GD20H_REG_FIFO_SRC<5>
#define L3GD20H_FIFO_FFS          0x1f    // L3GD20H_REG_FIFO_SRC<4:0>

#define L3GD20H_INT1_AND_OR       0x80    // L3GD20H_REG_IG_CFG<7>
#define L3GD20H_INT1_LATCH        0x40    // L3GD20H_REG_IG_CFG<6>
#define L3GD20H_INT1_Z_HIGH       0x20    // L3GD20H_REG_IG_CFG<5>, L3GD20H_REG_IG_SRC<5>
#define L3GD20H_INT1_Z_LOW        0x10    // L3GD20H_REG_IG_CFG<4>, L3GD20H_REG_IG_SRC<4>
#define L3GD20H_INT1_Y_HIGH       0x08    // L3GD20H_REG_IG_CFG<3>, L3GD20H_REG_IG_SRC<3>
#define L3GD20H_INT1_Y_LOW        0x04    // L3GD20H_REG_IG_CFG<2>, L3GD20H_REG_IG_SRC<2>
#define L3GD20H_INT1_X_HIGH       0x02    // L3GD20H_REG_IG_CFG<1>, L3GD20H_REG_IG_SRC<1>
#define L3GD20H_INT1_X_LOW        0x01    // L3GD20H_REG_IG_CFG<0>, L3GD20H_REG_IG_SRC<0>

#define L3GD20H_INT1_ACTIVE       0x40    // L3GD20H_REG_IG_SRC<7>

#define L3GD20H_INT1_WAIT         0x80    // L3GD20H_REG_IG_DURATION<7>
#define L3GD20H_INT1_DURATION     0x3f    // L3GD20H_REG_IG_DURATION<6:0>

#define L3GD20H_DRDY_HL           0x20    // L3GD20H_REG_LOW_ODR<5>
#define L3GD20H_SW_RESET          0x04    // L3GD20H_REG_LOW_ODR<2>
#define L3GD20H_LOW_ODR           0x01    // L3GD20H_REG_LOW_ODR<0>

/** Forward declaration of functions for internal use */

static bool    l3gd20h_reset       (l3gd20h_sensor_t* dev);
static bool    l3gd20h_is_available(l3gd20h_sensor_t* dev);

static bool    l3gd20h_read_reg    (l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len);
static bool    l3gd20h_write_reg   (l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len);
static bool    l3gd20h_update_reg  (l3gd20h_sensor_t* dev, uint8_t reg, uint8_t mask, uint8_t  val);

static uint8_t l3gd20h_get_reg_bit (uint8_t  byte, uint8_t mask);
static void    l3gd20h_set_reg_bit (uint8_t* byte, uint8_t mask, uint8_t bit);

static bool    l3gd20h_i2c_read    (l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len);
static bool    l3gd20h_i2c_write   (l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len);
static bool    l3gd20h_spi_read    (l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len);
static bool    l3gd20h_spi_write   (l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len);

// platform dependent SPI interface functions
#ifdef ESP_OPEN_RTOS
static const spi_settings_t bus_settings = {
    .mode         = SPI_MODE3,
    .freq_divider = SPI_FREQ_DIV_1M,
    .msb          = true,
    .minimal_pins = false,
    .endianness   = SPI_LITTLE_ENDIAN
};

static bool spi_device_init (uint8_t bus, uint8_t cs)
{
    gpio_enable(cs, GPIO_OUTPUT);
    gpio_write (cs, true);
    return true;
}

static size_t spi_transfer_pf(uint8_t bus, uint8_t cs, const uint8_t *mosi, uint8_t *miso, uint16_t len)
{
    spi_settings_t old_settings;

    spi_get_settings(bus, &old_settings);
    spi_set_settings(bus, &bus_settings);
    gpio_write(cs, false);

    size_t transfered = spi_transfer (bus, (const void*)mosi, (void*)miso, len, SPI_8BIT);

    gpio_write(cs, true);
    spi_set_settings(bus, &old_settings);
    
    return transfered;
}

#endif

#define msb_lsb_to_type(t,b,o) (t)(((t)b[o] << 8) | b[o+1])
#define lsb_msb_to_type(t,b,o) (t)(((t)b[o+1] << 8) | b[o])
#define lsb_to_type(t,b,o)     (t)(b[o])

l3gd20h_sensor_t* l3gd20h_init_sensor (uint8_t bus, uint8_t addr, uint8_t cs)
{
    l3gd20h_sensor_t* dev;

    if ((dev = malloc (sizeof(l3gd20h_sensor_t))) == NULL)
        return NULL;

    // init sensor data structure
    dev->bus    = bus;
    dev->addr   = addr;
    dev->cs     = cs;

    dev->error_code = L3GD20H_OK;
    dev->scale      = l3gd20h_scale_245dps;
    dev->fifo_mode  = l3gd20h_bypass;
    
    // if addr==0 then SPI is used and has to be initialized
    if (!addr && !spi_device_init (bus, cs))
    {
        error_dev ("Could not initialize SPI interface.", __FUNCTION__, dev);
        free (dev);
        return NULL;
    }
        
    // check availability of the sensor
    if (!l3gd20h_is_available (dev))
    {
        error_dev ("Sensor is not available.", __FUNCTION__, dev);
        free (dev);
        return NULL;
    }

    // reset the sensor
    if (!l3gd20h_reset(dev))
    {
        error_dev ("Could not reset the sensor device.", __FUNCTION__, dev);
        free (dev);
        return NULL;
    }
    
    l3gd20h_update_reg (dev, L3GD20H_REG_CTRL4, L3GD20H_FULL_SCALE, l3gd20h_scale_245dps);
    l3gd20h_update_reg (dev, L3GD20H_REG_CTRL4, L3GD20H_BLOCK_DATA_UPDATE, 1);

    return dev;
}


bool l3gd20h_set_mode (l3gd20h_sensor_t* dev, l3gd20h_mode_t mode, uint8_t bw,
                       bool x, bool y, bool z)
{
    if (!dev) return false;

    if (bw > 3)
    {
        error_dev ("Bandwidth value %d not in range 0 ... 3", __FUNCTION__, dev, bw);
        dev->error_code = L3GD20H_WRONG_BANDWIDTH;
        return false;
    }
    
    dev->error_code = L3GD20H_OK;
    
    uint8_t reg1 = 0;
    uint8_t reg2 = 0;

    if (mode != l3gd20h_power_down)
    {
        // read current register values
        if (!l3gd20h_read_reg (dev, L3GD20H_REG_CTRL1, &reg1, 1) ||
            !l3gd20h_read_reg (dev, L3GD20H_REG_LOW_ODR, &reg2, 1))
            return false;
   
        // if sensor is in power mode it takes at least 100 ms to start in another mode
        if (!l3gd20h_get_reg_bit (reg1, L3GD20H_POWER_MODE))
            vTaskDelay (20);
            
        if (mode >= l3gd20h_normal_odr_100)
        {
            // high output data rate
            l3gd20h_set_reg_bit (&reg2, L3GD20H_LOW_ODR, 0);
            l3gd20h_set_reg_bit (&reg1, L3GD20H_ODR, mode - l3gd20h_normal_odr_100);
        }
        else
        {
            // low output data rate
            l3gd20h_set_reg_bit (&reg2, L3GD20H_LOW_ODR, 1);
            l3gd20h_set_reg_bit (&reg1, L3GD20H_ODR, mode - l3gd20h_normal_odr_12_5);
        }
    
        l3gd20h_set_reg_bit (&reg1, L3GD20H_POWER_MODE, 1);
        l3gd20h_set_reg_bit (&reg1, L3GD20H_BW, bw);
        l3gd20h_set_reg_bit (&reg1, L3GD20H_X_ENABLED, x);
        l3gd20h_set_reg_bit (&reg1, L3GD20H_Y_ENABLED, y);
        l3gd20h_set_reg_bit (&reg1, L3GD20H_Z_ENABLED, z);
        
        if (!l3gd20h_write_reg (dev, L3GD20H_REG_LOW_ODR, &reg2, 1))
            return false;
    } 
    else
        l3gd20h_set_reg_bit (&reg1, L3GD20H_POWER_MODE, 0);

    if (!l3gd20h_write_reg (dev, L3GD20H_REG_CTRL1, &reg1, 1))
        return false;
        
    return true;
}


bool l3gd20h_set_scale (l3gd20h_sensor_t* dev, l3gd20h_scale_t scale)
{
    if (!dev) return false;
    
    dev->error_code = L3GD20H_OK;
    dev->scale = scale;
    
    // read CTRL4 register and write scale
    return l3gd20h_update_reg (dev, L3GD20H_REG_CTRL4, L3GD20H_FULL_SCALE, scale);
}


bool l3gd20h_set_fifo_mode (l3gd20h_sensor_t* dev, l3gd20h_fifo_mode_t mode)
{
    if (!dev) return false;
    
    dev->error_code = L3GD20H_OK;
    dev->fifo_mode = mode;
    
    // read CTRL5 register and write FIFO_EN flag
    if (!l3gd20h_update_reg (dev, L3GD20H_REG_CTRL5, L3GD20H_FIFO_EN, (mode != l3gd20h_bypass)))
        return false;

    // read FIFO_CTRL register and write FIFO mode
    if (!l3gd20h_update_reg (dev, L3GD20H_REG_FIFO_CTRL, L3GD20H_FIFO_MODE, mode))
        return false;

    return true;
}


bool l3gd20h_select_output_filter (l3gd20h_sensor_t* dev,
                                   l3gd20h_filter_t filter)
{
    if (!dev) return 0;

    dev->error_code = L3GD20H_OK;

    if (!l3gd20h_update_reg (dev, L3GD20H_REG_CTRL5, L3GD20H_OUT_SEL, filter) ||
        (filter == l3gd20h_hpf_and_lpf2 &&
        !l3gd20h_update_reg (dev, L3GD20H_REG_CTRL5, L3GD20H_HP_ENABLED, 1)))
    {   
        error_dev ("Could not select filters for output data", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_SEL_OUT_FILTER_FAILED;
        return false;
    }

    return true;
}


bool l3gd20h_new_data (l3gd20h_sensor_t* dev)
{
    if (!dev) return false;

    dev->error_code = L3GD20H_OK;

    uint8_t reg;

    if (dev->fifo_mode == l3gd20h_bypass)
    {
        if (!l3gd20h_read_reg (dev, L3GD20H_REG_STATUS, &reg, 1))
        {
            error_dev ("Could not get sensor status", __FUNCTION__, dev);
            return false;
        }
        return l3gd20h_get_reg_bit (reg, L3GD20H_ZYXDA);
    }
    else
    {
        if (!l3gd20h_read_reg (dev, L3GD20H_REG_FIFO_SRC, &reg, 1))
        {
            error_dev ("Could not get fifo source register data", __FUNCTION__, dev);
            return false;
        }
        return l3gd20h_get_reg_bit (reg, L3GD20H_FIFO_FFS);
    }
}

// scale factors for conversion of raw sensor data to degree for possible
// sensitivities according to mechanical characteristics in datasheet
const static float L3GD20H_SCALES[3] = { (0.00875F), (0.0175F), (0.070F) };


bool l3gd20h_get_float_data (l3gd20h_sensor_t* dev, l3gd20h_float_data_t* data)
{
    if (!dev || !data) return false;

    l3gd20h_raw_data_t raw;
    
    if (!l3gd20h_get_raw_data (dev, &raw))
        return false;
        
    data->x = raw.x * L3GD20H_SCALES[dev->scale];
    data->y = raw.y * L3GD20H_SCALES[dev->scale];
    data->z = raw.z * L3GD20H_SCALES[dev->scale];
    
    return true;
}


uint8_t l3gd20h_get_float_data_fifo (l3gd20h_sensor_t* dev, l3gd20h_float_data_fifo_t data)
{
    if (!dev) return 0;

    l3gd20h_raw_data_fifo_t raw;
    
    uint8_t num = l3gd20h_get_raw_data_fifo (dev, raw);
    
    for (int i = 0; i < num; i++)
    {
        data[i].x = raw[i].x * L3GD20H_SCALES[dev->scale];
        data[i].y = raw[i].y * L3GD20H_SCALES[dev->scale];
        data[i].z = raw[i].z * L3GD20H_SCALES[dev->scale];
    }

    return num;
}

bool l3gd20h_get_raw_data (l3gd20h_sensor_t* dev, l3gd20h_raw_data_t* raw)
{
    if (!dev || !raw) return false;

    dev->error_code = L3GD20H_OK;

    uint8_t data[6];
    uint8_t reg;

    if (!l3gd20h_read_reg (dev, L3GD20H_REG_OUT_X_L, data, 6))
    {
        error_dev ("Could not get raw data", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_GET_RAW_DATA_FAILED;
        return false;
    }

    raw->x = lsb_msb_to_type ( int16_t, data, 0);
    raw->y = lsb_msb_to_type ( int16_t, data, 2);
    raw->z = lsb_msb_to_type ( int16_t, data, 4);
    
    if (dev->fifo_mode != l3gd20h_bypass)
    {
        if (!l3gd20h_read_reg (dev, L3GD20H_REG_FIFO_SRC, &reg, 1))
            return false;
            
        // in FIFO mode test whether it was last sample
        if (l3gd20h_get_reg_bit (reg, L3GD20H_FIFO_FFS))
            return true;
        
        // if so, clean FIFO
        if (!l3gd20h_update_reg (dev, L3GD20H_REG_FIFO_CTRL, L3GD20H_FIFO_MODE, l3gd20h_bypass) ||
            !l3gd20h_update_reg (dev, L3GD20H_REG_FIFO_CTRL, L3GD20H_FIFO_MODE, dev->fifo_mode))
            return false;
    }
    
    return true;
}


uint8_t l3gd20h_get_raw_data_fifo (l3gd20h_sensor_t* dev, l3gd20h_raw_data_fifo_t raw)
{
    if (!dev) return 0;

    dev->error_code = L3GD20H_OK;

    uint8_t reg;

    if (!l3gd20h_read_reg (dev, L3GD20H_REG_FIFO_SRC, &reg, 1))
    {
        error_dev ("Could not get fifo source register data", __FUNCTION__, dev);
        return 0;
    }

    uint8_t level = l3gd20h_get_reg_bit (reg, L3GD20H_FIFO_FFS) + (reg & L3GD20H_FIFO_OVR ? 1 : 0);

    for (int i = 0; i < level; i++)
        if (!l3gd20h_get_raw_data(dev, raw+i))
        {
            error_dev ("Could not get raw data", __FUNCTION__, dev);
            dev->error_code |= L3GD20H_GET_RAW_DATA_FIFO_FAILED;
            return 0;
        }

    // clean FIFO (see app note)
    if (!l3gd20h_update_reg (dev, L3GD20H_REG_FIFO_CTRL, L3GD20H_FIFO_MODE, l3gd20h_bypass) ||
        !l3gd20h_update_reg (dev, L3GD20H_REG_FIFO_CTRL, L3GD20H_FIFO_MODE, dev->fifo_mode))
        return 0;
    
    return level;
}

bool l3gd20h_set_int1_config (l3gd20h_sensor_t* dev, 
                              l3gd20h_int1_config_t* config)
{
    if (!dev || !config) return false;

    dev->error_code = L3GD20H_OK;

    uint8_t ig_cfg = 0;
    uint8_t ig_dur = 0;
    uint8_t ig_ths[6] = { 0 };
    
    l3gd20h_set_reg_bit (&ig_cfg, L3GD20H_INT1_X_LOW , config->x_low_enabled);
    l3gd20h_set_reg_bit (&ig_cfg, L3GD20H_INT1_X_HIGH, config->x_high_enabled);
    
    l3gd20h_set_reg_bit (&ig_cfg, L3GD20H_INT1_Y_LOW , config->y_low_enabled);
    l3gd20h_set_reg_bit (&ig_cfg, L3GD20H_INT1_Y_HIGH, config->y_high_enabled);

    l3gd20h_set_reg_bit (&ig_cfg, L3GD20H_INT1_Z_LOW , config->z_low_enabled);
    l3gd20h_set_reg_bit (&ig_cfg, L3GD20H_INT1_Z_HIGH, config->z_high_enabled);
    
    l3gd20h_set_reg_bit (&ig_cfg, L3GD20H_INT1_LATCH , config->latch_interrupt);
    l3gd20h_set_reg_bit (&ig_cfg, L3GD20H_INT1_AND_OR, config->and_combination);

    l3gd20h_set_reg_bit (&ig_dur, L3GD20H_INT1_WAIT    , config->wait_enabled);
    l3gd20h_set_reg_bit (&ig_dur, L3GD20H_INT1_DURATION, config->duration);

    ig_ths[0] = (config->x_threshold >> 8) & 0x7f;
    ig_ths[1] = (config->x_threshold & 0xff);
    ig_ths[2] = (config->y_threshold >> 8) & 0x7f;
    ig_ths[3] = (config->y_threshold & 0xff);
    ig_ths[4] = (config->z_threshold >> 8) & 0x7f;
    ig_ths[5] = (config->z_threshold & 0xff);

    if (// ouput value selection used for threshold comparison for INT1 generation
        !l3gd20h_update_reg (dev, L3GD20H_REG_CTRL5, L3GD20H_IG_SEL, config->filter) ||

        // write the thresholds to registers IG_THS_*
        !l3gd20h_write_reg (dev, L3GD20H_REG_IG_THS_XH, ig_ths, 6) ||
        
        // write duration configuration to IG_DURATION 
        !l3gd20h_write_reg (dev, L3GD20H_REG_IG_DURATION, &ig_dur, 1) ||
        
        // write INT1 configuration  to IG_CFG
        !l3gd20h_write_reg (dev, L3GD20H_REG_IG_CFG, &ig_cfg, 1) ||
        
        // enable or disable the INT1 signal in register CTRL3
        !l3gd20h_update_reg (dev, L3GD20H_REG_CTRL3, L3GD20H_INT1_IG, (ig_cfg & 0x3f) ? 1 : 0))
    {   
        error_dev ("Could not configure interrupt INT1", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_CONFIG_INT1_FAILED;
        return false;
    }

    if (config->filter == l3gd20h_hpf_and_lpf2 &&
        !l3gd20h_update_reg (dev, L3GD20H_REG_CTRL5, L3GD20H_HP_ENABLED, 1))
    {   
        error_dev ("Could not configure interrupt INT1", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_CONFIG_INT1_FAILED;
        return false;
    }

    return true;
}


bool l3gd20h_get_int1_config (l3gd20h_sensor_t* dev, 
                              l3gd20h_int1_config_t* config)
{
    if (!dev || !config) return false;

    dev->error_code = L3GD20H_OK;

    uint8_t ig_cfg;
    uint8_t ig_dur;
    uint8_t ig_ths[6];
    uint8_t ctrl3;
    uint8_t ctrl5;

    if (!l3gd20h_read_reg (dev, L3GD20H_REG_IG_THS_XH, ig_ths, 6) ||
        !l3gd20h_read_reg (dev, L3GD20H_REG_IG_CFG, &ig_cfg, 1) ||
        !l3gd20h_read_reg (dev, L3GD20H_REG_IG_DURATION, &ig_dur, 1) ||
        !l3gd20h_read_reg (dev, L3GD20H_REG_CTRL3, &ctrl3, 1) ||
        !l3gd20h_read_reg (dev, L3GD20H_REG_CTRL5, &ctrl5, 1))
    {   
        error_dev ("Could not read configuration for interrupt INT1 from sensor", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_CONFIG_INT1_FAILED;
        return false;
    }
    
    config->x_low_enabled   = l3gd20h_get_reg_bit (ig_cfg, L3GD20H_INT1_X_LOW);
    config->x_high_enabled  = l3gd20h_get_reg_bit (ig_cfg, L3GD20H_INT1_X_HIGH);
    
    config->y_low_enabled   = l3gd20h_get_reg_bit (ig_cfg, L3GD20H_INT1_Y_LOW);
    config->y_high_enabled  = l3gd20h_get_reg_bit (ig_cfg, L3GD20H_INT1_Y_HIGH);

    config->z_low_enabled   = l3gd20h_get_reg_bit (ig_cfg, L3GD20H_INT1_Z_LOW);
    config->z_high_enabled  = l3gd20h_get_reg_bit (ig_cfg, L3GD20H_INT1_Z_HIGH);
    
    config->x_threshold     = msb_lsb_to_type(uint16_t, ig_ths, 0);
    config->y_threshold     = msb_lsb_to_type(uint16_t, ig_ths, 2);
    config->z_threshold     = msb_lsb_to_type(uint16_t, ig_ths, 4);
    
    config->filter          = l3gd20h_get_reg_bit (ctrl5, L3GD20H_IG_SEL);
    
    config->and_combination = l3gd20h_get_reg_bit (ig_cfg, L3GD20H_INT1_AND_OR);
    config->latch_interrupt = l3gd20h_get_reg_bit (ig_cfg, L3GD20H_INT1_LATCH);
    
    config->wait_enabled    = l3gd20h_get_reg_bit (ig_dur, L3GD20H_INT1_WAIT);
    config->duration        = l3gd20h_get_reg_bit (ig_dur, L3GD20H_INT1_DURATION);
    
    config->counter_mode    = 0;

    return true;
}


bool l3gd20h_get_int1_source (l3gd20h_sensor_t* dev, l3gd20h_int1_source_t* source)
{
    if (!dev || !source) return false;

    dev->error_code = L3GD20H_OK;

    uint8_t ig_src;

    if (!l3gd20h_read_reg (dev, L3GD20H_REG_IG_SRC, &ig_src, 1))
    {   
        error_dev ("Could not read source of interrupt INT1 from sensor", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_INT1_SOURCE_FAILED;
        return false;
    }

    source->active = l3gd20h_get_reg_bit (ig_src, L3GD20H_INT1_ACTIVE);
    source->x_low  = l3gd20h_get_reg_bit (ig_src, L3GD20H_INT1_X_LOW);
    source->x_high = l3gd20h_get_reg_bit (ig_src, L3GD20H_INT1_X_HIGH);
    source->y_low  = l3gd20h_get_reg_bit (ig_src, L3GD20H_INT1_Y_LOW);
    source->y_high = l3gd20h_get_reg_bit (ig_src, L3GD20H_INT1_Y_HIGH);
    source->z_low  = l3gd20h_get_reg_bit (ig_src, L3GD20H_INT1_Z_LOW);
    source->z_high = l3gd20h_get_reg_bit (ig_src, L3GD20H_INT1_Z_HIGH);
    
    return true;
}


bool l3gd20h_enable_int2 (l3gd20h_sensor_t* dev,
                          l3gd20h_int2_types_t type, bool value)
{
    if (!dev) return false;

    dev->error_code = L3GD20H_OK;

    uint8_t mask;
    
    switch (type)
    {
        case l3gd20h_data_ready:     mask  = L3GD20H_INT2_DRDY;  break;
        case l3gd20h_fifo_threshold: mask  = L3GD20H_INT2_FTH;   break;
        case l3gd20h_fifo_overrun:   mask  = L3GD20H_INT2_ORUN;  break;
        case l3gd20h_fifo_empty:     mask  = L3GD20H_INT2_EMPTY; break;
        default: dev->error_code = L3GD20H_WRONG_INT_TYPE; 
                 error_dev ("Wrong interrupt type", __FUNCTION__, dev);
                 return false;
    }        
        
    if (!l3gd20h_update_reg (dev, L3GD20H_REG_CTRL3, mask, value))
    {
        error_dev ("Could not %s interrupt INT2", __FUNCTION__, dev, value ? "enable" : "disable");
        dev->error_code |= L3GD20H_CONFIG_INT2_FAILED;
        return false;
    }
    
    return true;
}


bool l3gd20h_get_int2_source (l3gd20h_sensor_t* dev, l3gd20h_int2_source_t* source)
{
    if (!dev || !source) return false;

    dev->error_code = L3GD20H_OK;

    uint8_t fifo_src;
    uint8_t status;

    if (!l3gd20h_read_reg (dev, L3GD20H_REG_STATUS, &status, 1) ||
        !l3gd20h_read_reg (dev, L3GD20H_REG_FIFO_SRC, &fifo_src, 1))
    {   
        error_dev ("Could not read source of interrupt INT2 from sensor", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_INT2_SOURCE_FAILED;
        return false;
    }

    source->data_ready     = l3gd20h_get_reg_bit (status, L3GD20H_ANY_DATA_READY);
    source->fifo_threshold = l3gd20h_get_reg_bit (fifo_src, L3GD20H_FIFO_THS);
    source->fifo_overrun   = l3gd20h_get_reg_bit (fifo_src, L3GD20H_FIFO_OVR);
    source->fifo_empty     = l3gd20h_get_reg_bit (fifo_src, L3GD20H_FIFO_EMPTY);

    return true;
}


bool l3gd20h_config_int_signals (l3gd20h_sensor_t* dev,
                                 l3gd20h_signal_level_t level,
                                 l3gd20h_signal_type_t type)
{
    if (!dev) return false;

    dev->error_code = L3GD20H_OK;

    if (!l3gd20h_update_reg (dev, L3GD20H_REG_CTRL3, L3GD20H_HL_ACTIVE, level) ||
        !l3gd20h_update_reg (dev, L3GD20H_REG_CTRL3, L3GD20H_PP_OD, type) ||
        !l3gd20h_update_reg (dev, L3GD20H_REG_LOW_ODR, L3GD20H_DRDY_HL, level))
    {   
        error_dev ("Could not configure interrupt signals", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_CONFIG_INT_SIGNALS_FAILED;
        return false;
    }

    return true;
}


bool l3gd20h_config_hpf (l3gd20h_sensor_t* dev, l3gd20h_hpf_mode_t mode, 
                         uint8_t cutoff)
{
    if (!dev) return false;

    dev->error_code = L3GD20H_OK;

    if (!l3gd20h_update_reg (dev, L3GD20H_REG_CTRL2, L3GD20H_HPF_MODE, mode) ||
        !l3gd20h_update_reg (dev, L3GD20H_REG_CTRL2, L3GD20H_HPF_CUTOFF, cutoff))
    {   
        error_dev ("Could not configure high pass filter", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_CONFIG_HPF_FAILED;
        return false;
    }

    return true;
}


bool l3gd20h_set_hpf_ref (l3gd20h_sensor_t* dev, int8_t ref)
{
    if (!dev) return false;

    dev->error_code = L3GD20H_OK;

    if (!l3gd20h_write_reg (dev, L3GD20H_REG_REFERENCE, (uint8_t*)&ref, 1))
    {   
        error_dev ("Could not set high pass filter reference", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_CONFIG_HPF_FAILED;
        return false;
    }

    return true;
}

int8_t l3gd20h_get_hpf_ref (l3gd20h_sensor_t* dev)
{
    if (!dev) return 0;

    dev->error_code = L3GD20H_OK;

    int8_t ref = 0;
    
    if (!l3gd20h_read_reg (dev, L3GD20H_REG_REFERENCE, (uint8_t*)&ref, 1))
    {   
        error_dev ("Could not get high pass filter reference", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_CONFIG_HPF_FAILED;
        return 0;
    }

    return ref;
}


int8_t l3gd20h_get_temperature (l3gd20h_sensor_t* dev)
{
    if (!dev) return 0;

    dev->error_code = L3GD20H_OK;

    int8_t reg;
    
    if (!l3gd20h_read_reg (dev, L3GD20H_REG_OUT_TEMP, (uint8_t*)(&reg), 1))
    {   
        error_dev ("Could not get temperature", __FUNCTION__, dev);
        return false;
    }
    
    return reg;
}


/** Functions for internal use only */

/**
 * @brief   Check the chip ID to test whether sensor is available
 */
static bool l3gd20h_is_available (l3gd20h_sensor_t* dev)
{
    uint8_t chip_id;

    if (!dev) return false;

    dev->error_code = L3GD20H_OK;

    if (!l3gd20h_read_reg (dev, L3GD20H_REG_WHO_AM_I, &chip_id, 1))
        return false;

    if (chip_id != L3GD20H_CHIP_ID &&
        chip_id != L3GD20_CHIP_ID &&
        chip_id != L3G4200D_CHIP_ID)
    {
        error_dev ("Chip id %02x is wrong, should be %02x.",
                    __FUNCTION__, dev, chip_id, L3GD20H_CHIP_ID);
        dev->error_code = L3GD20H_WRONG_CHIP_ID;
        return false;
    }

    return true;
}


static bool l3gd20h_reset (l3gd20h_sensor_t* dev)
{
    if (!dev) return false;

    dev->error_code = L3GD20H_OK;

    if (!l3gd20h_update_reg (dev, L3GD20H_REG_LOW_ODR, L3GD20H_SW_RESET, 1))
        return false;
        
    vTaskDelay(10);

    uint8_t reg[6] = { 0 };
    
    // initialize sensor completely including setting in power down mode
    l3gd20h_write_reg (dev, L3GD20H_REG_CTRL1    , reg, 6);
    l3gd20h_write_reg (dev, L3GD20H_REG_FIFO_CTRL, reg, 1);
    l3gd20h_write_reg (dev, L3GD20H_REG_IG_CFG   , reg, 1);
    l3gd20h_write_reg (dev, L3GD20H_REG_IG_THS_XH, reg, 6);
    
    return true;
}


static bool l3gd20h_update_reg(l3gd20h_sensor_t* dev, uint8_t reg, uint8_t mask, uint8_t val)
{
    if (!dev) return false;

    uint8_t reg_val;
    uint8_t shift = 0;
    
    while (!((mask >> shift) & 0x01)) shift++;

    // read current register value
    if (!l3gd20h_read_reg (dev, reg, &reg_val, 1))
        return false;

    // set masked bits to the given value 
    reg_val = (reg_val & ~mask) | ((val << shift) & mask);

    // write back new register value
    if (!l3gd20h_write_reg (dev, reg, &reg_val, 1))
        return false;
        
    return true;
}

static bool l3gd20h_read_reg(l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len)
{
    if (!dev || !data) return false;

    return (dev->addr) ? l3gd20h_i2c_read (dev, reg, data, len)
                       : l3gd20h_spi_read (dev, reg, data, len);
}


static bool l3gd20h_write_reg(l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len)
{
    if (!dev || !data) return false;

    return (dev->addr) ? l3gd20h_i2c_write (dev, reg, data, len)
                       : l3gd20h_spi_write (dev, reg, data, len);
}


static void l3gd20h_set_reg_bit (uint8_t* byte, uint8_t mask, uint8_t bit)
{ 
    if (byte)
    {
        uint8_t shift = 0;
        while (!((mask >> shift) & 0x01)) shift++;
        *byte = ((*byte & ~mask) | ((bit << shift) & mask));
    }
}


static uint8_t l3gd20h_get_reg_bit(uint8_t byte, uint8_t mask)
{ 
    uint8_t shift = 0;
    while (!((mask >> shift) & 0x01)) shift++;
    return (byte & mask) >> shift;
}


#define L3GD20H_SPI_BUF_SIZE 64      // SPI register data buffer size of ESP866

#define L3GD20H_SPI_READ_FLAG      0x80
#define L3GD20H_SPI_WRITE_FLAG     0x00
#define L3GD20H_SPI_AUTO_INC_FLAG  0x40

static bool l3gd20h_spi_read(l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len)
{
    if (!dev || !data) return false;

    if (len >= L3GD20H_SPI_BUF_SIZE)
    {
        dev->error_code |= L3GD20H_SPI_BUFFER_OVERFLOW;
        error_dev ("Error on read from SPI slave on bus 1. Tried to transfer "
                   "more than %d byte in one read operation.",
                   __FUNCTION__, dev, L3GD20H_SPI_BUF_SIZE);
        return false;
    }

    uint8_t addr = (reg & 0x3f) | L3GD20H_SPI_READ_FLAG | L3GD20H_SPI_AUTO_INC_FLAG;
    
    static uint8_t mosi[L3GD20H_SPI_BUF_SIZE];
    static uint8_t miso[L3GD20H_SPI_BUF_SIZE];

    memset (mosi, 0xff, L3GD20H_SPI_BUF_SIZE);
    memset (miso, 0xff, L3GD20H_SPI_BUF_SIZE);

    mosi[0] = addr;
    
    if (!spi_transfer_pf (dev->bus, dev->cs, mosi, miso, len+1))
    {
        error_dev ("Could not read data from SPI", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_SPI_READ_FAILED;
        return false;
    }
    
    // shift data one by left, first byte received while sending register address is invalid
    for (int i=0; i < len; i++)
      data[i] = miso[i+1];

    #ifdef L3GD20H_DEBUG_LEVEL_2
    printf("L3GD20H %s: read the following bytes from reg %02x: ", __FUNCTION__, reg);
    for (int i=0; i < len; i++)
        printf("%02x ", data[i]);
    printf("\n");
    #endif

    return true;
}


static bool l3gd20h_spi_write(l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len)
{
    if (!dev || !data) return false;

    uint8_t addr = (reg & 0x3f) | L3GD20H_SPI_WRITE_FLAG | L3GD20H_SPI_AUTO_INC_FLAG;

    static uint8_t mosi[L3GD20H_SPI_BUF_SIZE];

    if (len >= L3GD20H_SPI_BUF_SIZE)
    {
        dev->error_code |= L3GD20H_SPI_BUFFER_OVERFLOW;
        error_dev ("Error on write to SPI slave on bus 1. Tried to transfer more"
                   "than %d byte in one write operation.", __FUNCTION__, dev, L3GD20H_SPI_BUF_SIZE);

        return false;
    }

    reg &= 0x7f;

    // first byte in output is the register address
    mosi[0] = addr;

    // shift data one byte right, first byte in output is the register address
    for (int i = 0; i < len; i++)
        mosi[i+1] = data[i];

    #ifdef L3GD20H_DEBUG_LEVEL_2
    printf("L3GD20H %s: Write the following bytes to reg %02x: ", __FUNCTION__, reg);
    for (int i = 1; i < len+1; i++)
        printf("%02x ", mosi[i]);
    printf("\n");
    #endif

    if (!spi_transfer_pf (dev->bus, dev->cs, mosi, NULL, len+1))
    {
        error_dev ("Could not write data to SPI.", __FUNCTION__, dev);
        dev->error_code |= L3GD20H_SPI_WRITE_FAILED;
        return false;
    }

    return true;
}


#define I2C_AUTO_INCREMENT	(0x80)

static bool l3gd20h_i2c_read(l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len)
{
    if (!dev || !data) return false;

    debug_dev ("Read %d byte from i2c slave register %02x.", __FUNCTION__, dev, len, reg);

    if (len > 1)
        reg |= I2C_AUTO_INCREMENT;
    
    int result = i2c_slave_read(dev->bus, dev->addr, &reg, data, len);

    if (result)
    {
        dev->error_code |= (result == -EBUSY) ? L3GD20H_I2C_BUSY : L3GD20H_I2C_READ_FAILED;
        error_dev ("Error %d on read %d byte from I2C slave register %02x.",
                    __FUNCTION__, dev, result, len, reg);
        return false;
    }

#   ifdef L3GD20H_DEBUG_LEVEL_2
    printf("L3GD20H %s: Read following bytes: ", __FUNCTION__);
    printf("%02x: ", reg & 0x7f);
    for (int i=0; i < len; i++)
        printf("%02x ", data[i]);
    printf("\n");
#   endif

    return true;
}


static bool l3gd20h_i2c_write(l3gd20h_sensor_t* dev, uint8_t reg, uint8_t *data, uint16_t len)
{
    if (!dev || !data) return false;

    debug_dev ("Write %d byte to i2c slave register %02x.", __FUNCTION__, dev, len, reg);

    int result = i2c_slave_write(dev->bus, dev->addr, &reg, data, len);

    if (result)
    {
        dev->error_code |= (result == -EBUSY) ? L3GD20H_I2C_BUSY : L3GD20H_I2C_WRITE_FAILED;
        error_dev ("Error %d on write %d byte to i2c slave register %02x.",
                    __FUNCTION__, dev, result, len, reg);
        return false;
    }

#   ifdef L3GD20H_DEBUG_LEVEL_2
    printf("L3GD20H %s: Wrote the following bytes: ", __FUNCTION__);
    printf("%02x: ", reg);
    for (int i=0; i < len; i++)
        printf("%02x ", data[i]);
    printf("\n");
#   endif

    return true;
}
