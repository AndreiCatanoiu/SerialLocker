#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char* TAG = "LOCKER";

#define LOCK_TX_GPIO 17
#define LOCK_RX_GPIO 16
#define ESP_TX_GPIO 1
#define ESP_RX_GPIO 3 
#define UART_NO 1
#define BUF_SIZE 512

typedef struct
{
    int baud_rate;
    int tx_gpio;
    int rx_gpio;
} CoreUart_t;

typedef struct 
{
    uint16_t header;
    uint8_t len;
    uint8_t sourceId;
    uint8_t targetId;
    uint8_t command;
    uint8_t address;
} OmniMessage_t;

static const CoreUart_t uarts[] = 
{
    { .baud_rate = 115200, .tx_gpio = ESP_TX_GPIO, .rx_gpio = ESP_RX_GPIO },
    { .baud_rate = 9600, .tx_gpio = LOCK_TX_GPIO, .rx_gpio = LOCK_RX_GPIO },
};

static OmniMessage_t messages[] =
{
    { .header = 0x55AA, .len = 0x01, .sourceId = 0x10, .targetId = 0x30, .command = 0x01, .address = 0x10 },
    { .header = 0x55AA, .len = 0x01, .sourceId = 0x10, .targetId = 0x30, .command = 0x01, .address = 0x20 },
    { .header = 0x55AA, .len = 0x01, .sourceId = 0x10, .targetId = 0x30, .command = 0x02, .address = 0x21 },
    { .header = 0x55AA, .len = 0x01, .sourceId = 0x10, .targetId = 0x30, .command = 0x02, .address = 0x22 },
};

void uart_init(int uart_number)
{
    ESP_LOGI(TAG, "Initialization start");
    if (!uart_is_driver_installed(uart_number))
    {
        uart_config_t uart_config = 
        {
            .baud_rate = uarts[uart_number].baud_rate,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_APB,
        };
        int intr_alloc_flags = ESP_INTR_FLAG_SHARED | ESP_INTR_FLAG_LOWMED;

        ESP_ERROR_CHECK(uart_driver_install(uart_number, BUF_SIZE, 0, 0, NULL, intr_alloc_flags));
        ESP_ERROR_CHECK(uart_param_config(uart_number, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(uart_number, uarts[uart_number].tx_gpio, uarts[uart_number].rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    }
    else
    {
        ESP_LOGW(TAG, "Driver already installed");
    }
    ESP_LOGI(TAG, "Initialization end");
}

int uart_read(int uart_number, uint8_t *buff, size_t buff_len)
{
    if (!uart_is_driver_installed(uart_number))
        return 0;
    int len = uart_read_bytes(uart_number, buff, buff_len - 1, 20 / portTICK_PERIOD_MS);
    buff[len] = '\0'; 
    return len;
}

int uart_write(int uart_number, uint8_t *buff, size_t buff_len)
{
    if (!uart_is_driver_installed(uart_number))
        return 0;
    return uart_write_bytes(uart_number, (const char *) buff, buff_len);
}

uint16_t check_sum(uint8_t* pdata, uint32_t len)
{
    uint16_t checksum = 0;
    for (uint32_t i = 0; i < len; i++)
    {
        checksum += pdata[i];
    }
    checksum = checksum ^ 0xFFFF;
    return checksum;
}

void send_commands(OmniMessage_t message)
{
    size_t total_len = message.len + 7;
    uint8_t send[total_len + 2];

    send[0] = (uint8_t)((message.header & 0xFF00) >> 8);
    send[1] = (uint8_t)(message.header & 0x00FF);
    send[2] = message.len;
    send[3] = message.sourceId;
    send[4] = message.targetId;
    send[5] = message.command;
    send[6] = message.address;

    if (message.len > 0)
    {
        if (message.address == 0x10)
            send[7] = 0x07;
        else
        {
            for (int i = 7; i < message.len + 7; i++)
                send[i] = 0x01;
        }
    }
    else
        return;

    uint16_t checksum = check_sum(send + 2, total_len - 2);
    send[total_len] = (uint8_t)(checksum & 0x00FF);
    send[total_len + 1] = (uint8_t)((checksum & 0xFF00) >> 8);

    uart_write(UART_NO, send, sizeof(send));
}

void check_lock_status(uint8_t expected_status, int retries)
{
    for (int i = 0; i < retries; i++)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        uint8_t response[BUF_SIZE];
        int len = uart_read(UART_NO, response, BUF_SIZE);

        if (len > 0)
        {
            if (response[6] == 0x20)
            {
                if (response[7] == expected_status)
                {
                    ESP_LOGI(TAG, "Locker status is correct (%d).", expected_status);
                    return;
                }
                else
                {
                    ESP_LOGW(TAG, "Locker status is different from desired. Trying again (%d/%d).", i + 1, retries);
                }
            }
        }
        else
        {
            ESP_LOGW(TAG, "No response received during locker status check.");
        }
    }
    ESP_LOGE(TAG, "Error: Locker status did not reach the desired state after %d attempts.", retries);
}

void receive_commands()
{
    uint8_t response[BUF_SIZE];
    int len; 
    
    int number;
    len = uart_read(UART_NO, response, BUF_SIZE);

    switch (response[6])
    {
        case 0x10:
            number = 0;
            break;
        case 0x20:
            number = 1;
            break;
        case 0x21:
            number = 2;
            break;
        case 0x22:
            number = 3;
            break;
        default:
            number = -1;
            break;
    }
    
    if (len > 0)
    {
        switch (number)
        {
            case 0:
                ESP_LOGI(TAG, "Firmware version: %02x%02x", response[7], response[8]);
                break;
            case 1:
                if (response[7] == 0) 
                    ESP_LOGI(TAG, "LOCKED!");
                else if (response[7] == 1)
                    ESP_LOGI(TAG, "UNLOCKED!");
                else
                    ESP_LOGE(TAG, "ERROR");
                break;
            case 2:
                send_commands(messages[1]);
                check_lock_status(1, 5);    
                break;
            case 3:
                send_commands(messages[1]);
                check_lock_status(0, 5);
                break;
            default:
                ESP_LOGW(TAG, "Unknown command");
        }
    }
    else
    {
        ESP_LOGD(TAG, "No response received from device.");
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void locker_task()
{
    uart_init(0);
    uart_init(1);
    send_commands(messages[0]);

    while (1)
    {
        receive_commands();
        uint8_t command[BUF_SIZE];
        int len = uart_read(0, command, BUF_SIZE); 

        if (len > 0)
        {
            switch (command[0])
            {
                case '1':
                    ESP_LOGI(TAG, "Command: Status");
                    send_commands(messages[1]);
                    break;
                case '2': 
                    ESP_LOGI(TAG, "Command: Unlock");
                    send_commands(messages[2]);
                    break;
                case '3':  
                    ESP_LOGI(TAG, "Command: Lock");
                    send_commands(messages[3]);
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown command");
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
