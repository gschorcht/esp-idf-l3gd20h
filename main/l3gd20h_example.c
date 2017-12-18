/**
 * Simple example with one sensor connected to I2C or SPI. It demonstrates the
 * different approaches to fetch the data. Either one of the interrupt signals
 * for axes movement wake up *INT1* and data ready interrupt *INT2* is used
 * or the new data are fetched periodically.
 *
 * Harware configuration:
 *
 *   I2C   +-------------------------+     +----------+
 *         | ESP8266  Bus 0          |     | L3GD20H  |
 *         |          GPIO 5 (SCL)   ------> SCL      |
 *         |          GPIO 4 (SDA)   ------- SDA      |
 *         |          GPIO 13        <------ INT1     |
 *         |          GPIO 12        <------ DRDY/INT2|
 *         +-------------------------+     +----------+
 *
 *         +-------------------------+     +----------+
 *         | ESP32    Bus 0          |     | L3GD20H  |
 *         |          GPIO 16 (SCL)  >-----> SCL      |
 *         |          GPIO 17 (SDA)  ------- SDA      |
 *         |          GPIO 22        <------ INT1     |
 *         |          GPIO 23        <------ DRDY/INT2|
 *         +-------------------------+     +----------+
 *
 *   SPI   +-------------------------+     +----------+
 *         | ESP8266  Bus 1          |     | L3GD20H  |
 *         |          GPIO 14 (SCK)  ------> SCK      |
 *         |          GPIO 13 (MOSI) ------> SDI      |
 *         |          GPIO 12 (MISO) <------ SDO      |
 *         |          GPIO 2  (CS)   ------> CS       |
 *         |          GPIO 5         <------ INT1     |
 *         |          GPIO 4         <------ DRDY/INT2|
 *         +-------------------------+     +----------+

 *         +-------------------------+     +----------+
 *         | ESP32    Bus 0          |     | L3GD20H  |
 *         |          GPIO 16 (SCK)  ------> SCK      |
 *         |          GPIO 17 (MOSI) ------> SDI      |
 *         |          GPIO 18 (MISO) <------ SDO      |
 *         |          GPIO 19 (CS)   ------> CS       |
 *         |          GPIO 22        <------ INT1     |
 *         |          GPIO 23        <------ DRDY/INT2|
 *         +-------------------------+     +----------+
 */

// use following constants to define the example mode
// #define SPI_USED    // if defined SPI is used, otherwise I2C
   #define INT1_USED   // axes movement / wake up interrupts
   #define INT2_USED   // data ready and FIFO status interrupts
   #define FIFO_MODE   // multiple sample read mode

#if defined(INT1_USED) || defined(INT2_USED)
#define INT_USED
#endif

#include <string.h>

/* -- platform dependent includes ----------------------------- */

#ifdef ESP_PLATFORM  // ESP32 (ESP-IDF)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp8266_wrapper.h"

#include "l3gd20h.h"

#else  // ESP8266 (esp-open-rtos)

#define TASK_STACK_DEPTH 256

#include <stdio.h>

#include "espressif/esp_common.h"
#include "espressif/sdk_private.h"

#include "esp/uart.h"
#include "i2c/i2c.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "l3gd20h/l3gd20h.h"

#endif  // ESP_PLATFORM

/** -- platform dependent definitions ------------------------------ */

#ifdef ESP_PLATFORM  // ESP32 (ESP-IDF)

// user task stack depth
#define TASK_STACK_DEPTH 2048

// define SPI interface for L3GD20H sensors
#define SPI_BUS       HSPI_HOST
#define SPI_SCK_GPIO  16
#define SPI_MOSI_GPIO 17
#define SPI_MISO_GPIO 18
#define SPI_CS_GPIO   19

// define I2C interfaces for L3GD20H sensors
#define I2C_BUS       0
#define I2C_SCL_PIN   16
#define I2C_SDA_PIN   17
#define I2C_FREQ      400000

// define GPIOs for interrupt
#define INT1_PIN      22
#define INT2_PIN      23

#else  // ESP8266 (esp-open-rtos)

// user task stack depth
#define TASK_STACK_DEPTH 256

// define SPI interface for L3GD20H sensors
#define SPI_BUS       1
#define SPI_CS_GPIO   2   // GPIO 15, the default CS of SPI bus 1, can't be used

// define I2C interfaces for L3GD20H sensors
#define I2C_BUS       0
#define I2C_SCL_PIN   5
#define I2C_SDA_PIN   4
#define I2C_FREQ      I2C_FREQ_100K

// define GPIOs for interrupt
#ifdef SPI_USED
#define INT1_PIN      5
#define INT2_PIN      4
#else
#define INT1_PIN      13
#define INT2_PIN      12
#endif  // SPI_USED

#endif  // ESP_PLATFORM

/* -- user tasks ---------------------------------------------- */

static l3gd20h_sensor_t* sensor;

/**
 * Common function used to get sensor data.
 */
void read_data (void)
{
    #ifdef FIFO_MODE
    
    l3gd20h_float_data_fifo_t  data;

    if (l3gd20h_new_data (sensor))
    {
        uint8_t num = l3gd20h_get_float_data_fifo (sensor, data);
        printf("%.3f L3GD20H num=%d\n", (double)sdk_system_get_time()*1e-3, num);
        for (int i = 0; i < num; i++)
            // max. full scale is +-2000 dps and max. sensitivity is 1 mdps, i.e. 7 digits
            printf("%.3f L3GD20H (xyz)[dps]: %+9.3f %+9.3f  %+9.3f\n",
                   (double)sdk_system_get_time()*1e-3, data[i].x, data[i].y, data[i].z);
    }
    
    #else
    
    l3gd20h_float_data_t  data;

    while (l3gd20h_new_data (sensor) &&
           l3gd20h_get_float_data (sensor, &data))
        // max. full scale is +-2000 dps and max. sensitivity is 1 mdps, i.e. 7 digits
        printf("%.3f L3GD20H (xyz)[dps]: %+9.3f %+9.3f  %+9.3f\n",
               (double)sdk_system_get_time()*1e-3, data.x, data.y, data.z);
               
    #endif // FIFO_MODE
}


#if defined(INT1_USED) || defined(INT2_USED)
/**
 * In this case, axes movement wake up interrupt *INT1*  and data ready
 * interrupt *INT2* are used. While data ready interrupt *INT2* is generated
 * every time new data are available or the FIFO status changes, the axes
 * movement wake up interrupt *INT1* is triggered when output data across
 * defined thresholds.
 *
 * When interrupts are used, the user has to define interrupt handlers that
 * either fetches the data directly or triggers a task which is waiting to
 * fetch the data. In this example, the interrupt handler sends an event to
 * a waiting task to trigger the data gathering.
 */

static QueueHandle_t gpio_evt_queue = NULL;

// User task that fetches the sensor values.

void user_task_interrupt (void *pvParameters)
{
    uint32_t gpio_num;

    while (1)
    {
        if (xQueueReceive(gpio_evt_queue, &gpio_num, portMAX_DELAY))
        {
            if (gpio_num == INT1_PIN)
            {
                l3gd20h_int1_source_t source;

                // get the source of the interrupt and reset INT1 signal
                l3gd20h_get_int1_source (sensor, &source);

                // if data ready interrupt, get the results and do something with them
                if (source.active)
                    read_data ();
            }
            else if (gpio_num == INT2_PIN)
            {
                l3gd20h_int2_source_t source;

                // get the source of the interrupt
                l3gd20h_get_int2_source (sensor, &source);

                // if data ready interrupt, get the results and do something with them
                read_data();
            }
        }
    }
}

// Interrupt handler which resumes sends an event to the waiting user_task_interrupt

#ifdef ESP_PLATFORM  // ESP32 (ESP-IDF)
static void IRAM_ATTR int_signal_handler(void* arg)
{
    uint32_t gpio = (uint32_t) arg;

#else  // ESP8266 (esp-open-rtos)
void int_signal_handler (uint8_t gpio)
{

#endif
    // send an event with GPIO to the interrupt user task
    xQueueSendFromISR(gpio_evt_queue, &gpio, NULL);
}

#else

/*
 * In this example, user task fetches the sensor values every seconds.
 */

void user_task_periodic(void *pvParameters)
{
    vTaskDelay (100/portTICK_PERIOD_MS);
    
    while (1)
    {
        // read sensor data
        read_data ();
        
        // passive waiting until 1 second is over
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}

#endif

/* -- main program ---------------------------------------------- */

#ifdef ESP_PLATFORM  // ESP32 (ESP-IDF)
void app_main()
#else  // ESP8266 (esp-open-rtos)
void user_init(void)
#endif
{
    #ifdef ESP_OPEN_RTOS  // ESP8266
    // Set UART Parameter.
    uart_set_baud(0, 115200);
    #endif

    vTaskDelay(1);

    /** -- MANDATORY PART -- */

    #ifdef SPI_USED

    // init the sensor connnected to SPI
    #ifdef ESP_PLATFORM
    spi_bus_init (SPI_BUS, SPI_SCK_GPIO, SPI_MISO_GPIO, SPI_MOSI_GPIO);
    #endif

    // init the sensor connected to SPI_BUS with SPI_CS_GPIO as chip select.
    sensor = l3gd20h_init_sensor (SPI_BUS, 0, SPI_CS_GPIO);

    #else  // I2C

    // init all I2C bus interfaces at which L3GD20H sensors are connected
    i2c_init (I2C_BUS, I2C_SCL_PIN, I2C_SDA_PIN, I2C_FREQ);
    
    // init the sensor with slave address L3GD20H_I2C_ADDRESS_2 connected to I2C_BUS.
    sensor = l3gd20h_init_sensor (I2C_BUS, L3GD20H_I2C_ADDRESS_2, 0);

    #endif  // SPI_USED
    
    if (sensor)
    {
        // --- SYSTEM CONFIGURATION PART ----
        
        #if !defined(INT1_USED) && !defined(INT2_USED)

        // create a user task that fetches data from sensor periodically
        xTaskCreate(user_task_periodic, "user_task_periodic", TASK_STACK_DEPTH, NULL, 2, NULL);

        #else // INT1_USED || INT2_USED

        // create a task that is triggered only in case of interrupts to fetch the data
        xTaskCreate(user_task_interrupt, "user_task_interrupt", TASK_STACK_DEPTH, NULL, 2, NULL);

        // create event queue
        gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

        // configure interupt pins for *INT1* and *INT2* signals and set the interrupt handler
        gpio_set_interrupt(INT1_PIN, GPIO_INTTYPE_EDGE_POS, int_signal_handler);
        gpio_set_interrupt(INT2_PIN, GPIO_INTTYPE_EDGE_POS, int_signal_handler);

        #endif  // !defined(INT1_USED) && !defined(INT2_USED)
        
        // -- SENSOR CONFIGURATION PART ---

        // Interrupt configuration has to be done before the sensor is set
        // into measurement mode

        // set polarity of INT signals if necessary
        // l3gd20h_config_int_signals (dev, l3gd20h_high_active, l3gd20h_push_pull);

        #ifdef INT1_USED
        // enable event interrupts
        l3gd20h_int1_config_t int1_config;
    
        l3gd20h_get_int1_config (sensor, &int1_config);
    
        int1_config.x_high_enabled = true;
        int1_config.y_high_enabled = true;
        int1_config.z_high_enabled = true;
        int1_config.x_low_enabled  = false;
        int1_config.y_low_enabled  = false;
        int1_config.z_low_enabled  = false;
        int1_config.x_threshold = 1000;
        int1_config.y_threshold = 1000;
        int1_config.z_threshold = 1000;
    
        int1_config.filter = l3gd20h_hpf_only;
        int1_config.and_or = false;
        int1_config.duration = 0;
        int1_config.latch = true;
    
        l3gd20h_set_int1_config (sensor, &int1_config);
        #endif // INT1_USED
        
        #ifdef INT2_USED
        // enable data ready (DRDY) and FIFO interrupt signal *INT2*
        // NOTE: DRDY and FIFO interrupts must not be enabled at the same time
        #ifdef FIFO_MODE
        l3gd20h_enable_int2 (sensor, l3gd20h_fifo_overrun, true);
        l3gd20h_enable_int2 (sensor, l3gd20h_fifo_threshold, true);
        #else
        l3gd20h_enable_int2 (sensor, l3gd20h_data_ready, true);
        #endif
        
        #endif // INT2_USED

        #ifdef FIFO_MODE
        // clear FIFO and activate FIFO mode if needed
        l3gd20h_set_fifo_mode (sensor, l3gd20h_bypass, 0);
        l3gd20h_set_fifo_mode (sensor, l3gd20h_stream, 10);
        #endif
        
        // select LPF/HPF, configure HPF and reset the reference by dummy read
        l3gd20h_select_output_filter (sensor, l3gd20h_hpf_only);
        l3gd20h_config_hpf (sensor, l3gd20h_hpf_normal, 0);
        l3gd20h_get_hpf_ref (sensor);

        // LAST STEP: Finally set scale and sensor mode to start measurements
        l3gd20h_set_scale(sensor, l3gd20h_scale_245dps);
        l3gd20h_set_mode (sensor, l3gd20h_normal_odr_12_5, 3, true, true, true);

        // -- SENSOR CONFIGURATION PART ---
    }
}

