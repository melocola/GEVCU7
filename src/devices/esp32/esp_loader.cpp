/* Copyright 2020 Espressif Systems (Shanghai) PTE LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "serial_comm_prv.h"
#include "serial_comm.h"
#include "serial_io.h"
#include "esp_loader.h"
#include "esp_targets.h"
#include "md5_hash.h"
#include <string.h>
#include <assert.h>
#include "../../Logger.h"
#include "gevcu_port.h"

extern SdFs sdCard;

#ifndef MAX
#define MAX(a, b) ((a) > (b)) ? (a) : (b)
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b)) ? (a) : (b)
#endif

static const uint32_t DEFAULT_TIMEOUT = 1000;
static const uint32_t DEFAULT_FLASH_TIMEOUT = 3000;       // timeout for most flash operations
static const uint32_t ERASE_REGION_TIMEOUT_PER_MB = 10000; // timeout (per megabyte) for erasing a region
static const uint8_t  PADDING_PATTERN = 0xFF;

typedef enum {
    SPI_FLASH_READ_ID = 0x9F
} spi_flash_cmd_t;

static uint32_t s_flash_write_size = 0;
static const target_registers_t *s_reg = NULL;
static target_chip_t s_target = ESP_UNKNOWN_CHIP;

#if MD5_ENABLED

static const uint32_t MD5_TIMEOUT_PER_MB = 800;
static struct MD5Context s_md5_context;
static uint32_t s_start_address;
static uint32_t s_image_size;

static inline void init_md5(uint32_t address, uint32_t size)
{
    s_start_address = address;
    s_image_size = size;
    MD5Init(&s_md5_context);
}

static inline void md5_update(const uint8_t *data, uint32_t size)
{
    MD5Update(&s_md5_context, data, size);
}

static inline void md5_final(uint8_t digets[16])
{
    MD5Final(digets, &s_md5_context);
}

#else

static inline void init_md5(uint32_t address, uint32_t size) { }
static inline void md5_update(const uint8_t *data, uint32_t size) { }
static inline void md5_final(uint8_t digets[16]) { }

#endif

esp_loader_error_t flash_esp32_binary(FsFile *file, size_t address)
{
    esp_loader_error_t err;
    static uint8_t payload[1024];

    size_t size = file->fileSize();

    Logger::console("Erasing flash (this may take a while)...");
    err = esp_loader_flash_start(address, size, sizeof(payload));
    if (err != ESP_LOADER_SUCCESS) {
        Logger::console("Erasing flash failed with error %d.", err);
        return err;
    }
    Logger::console("Start programming %u bytes\n", size);

    size_t binary_size = size;
    size_t written = 0;
    int lastPercentage = 0;

    Serial.print("Progress Percentage: ");

    while (size > 0) {
        size_t to_read = min(size, sizeof(payload));
        file->read(payload, to_read);

        err = esp_loader_flash_write(payload, to_read);
        if (err != ESP_LOADER_SUCCESS) {
            Logger::console("\nPacket could not be written! Error %d.", err);
            return err;
        }

        size -= to_read;
        //bin_addr += to_read;
        written += to_read;

        int progress = (int)(((float)written / binary_size) * 100);
        if (progress > (lastPercentage + 4))
        {
            Serial.printf("%d ", progress);
            Serial.flush();
            lastPercentage = progress;
        }
    };

    Serial.printf("\n\nFinished programming\n");

    return ESP_LOADER_SUCCESS;
}

bool flashESP32(const char *filename, uint32_t address)
{
    FsFile file;
    if (file.open(filename, O_READ))
    {
        Logger::console("Found an esp32 update image. Flashing it to esp32");
        loader_port_gevcu_init(115200);
        esp_loader_connect_args_t conn = ESP_LOADER_CONNECT_DEFAULT();
        esp_loader_error_t err = esp_loader_connect(&conn);
        if (err != ESP_LOADER_SUCCESS) {
            Logger::console("Cannot connect to target. Error: %u\n", err);
        }
        Logger::console("Connected to ESP32 target\n");
        if (flash_esp32_binary(&file, address) == ESP_LOADER_SUCCESS)
        {
            loader_port_reset_target();
            sdCard.remove(filename);
            return true;
        }
        file.close();        
    }
    return false;
}

static uint32_t timeout_per_mb(uint32_t size_bytes, uint32_t time_per_mb)
{
    uint32_t timeout = time_per_mb * (size_bytes / 1e6);
    return MAX(timeout, DEFAULT_FLASH_TIMEOUT);
}

esp_loader_error_t esp_loader_connect(esp_loader_connect_args_t *connect_args)
{
    uint32_t spi_config;
    esp_loader_error_t err;
    int32_t trials = connect_args->trials;

    loader_port_enter_bootloader();

    do {
        loader_port_start_timer(connect_args->sync_timeout);
        err = loader_sync_cmd();
        if (err == ESP_LOADER_ERROR_TIMEOUT) {
            if (--trials == 0) {
                return ESP_LOADER_ERROR_TIMEOUT;
            }
            loader_port_delay_ms(100);
        } else if (err != ESP_LOADER_SUCCESS) {
            return err;
        }
    } while (err != ESP_LOADER_SUCCESS);

    RETURN_ON_ERROR( loader_detect_chip(&s_target, &s_reg) );

    if (s_target == ESP8266_CHIP) {
        err = loader_flash_begin_cmd(0, 0, 0, 0, s_target);
    } else {
        RETURN_ON_ERROR( loader_read_spi_config(s_target, &spi_config) );
        loader_port_start_timer(DEFAULT_TIMEOUT);
        err = loader_spi_attach_cmd(spi_config);
    }

    return err;
}

target_chip_t esp_loader_get_target(void)
{
    return s_target;
}

static esp_loader_error_t spi_set_data_lengths(size_t mosi_bits, size_t miso_bits)
{
    if (mosi_bits > 0) {
        RETURN_ON_ERROR( esp_loader_write_register(s_reg->mosi_dlen, mosi_bits - 1) );
    }
    if (miso_bits > 0) {
        RETURN_ON_ERROR( esp_loader_write_register(s_reg->miso_dlen, miso_bits - 1) );
    }

    return ESP_LOADER_SUCCESS;
}

static esp_loader_error_t spi_set_data_lengths_8266(size_t mosi_bits, size_t miso_bits)
{
    uint32_t mosi_mask = (mosi_bits == 0) ? 0 : mosi_bits - 1;
    uint32_t miso_mask = (miso_bits == 0) ? 0 : miso_bits - 1;
    return esp_loader_write_register(s_reg->usr1, (miso_mask << 8) | (mosi_mask << 17));
}

static esp_loader_error_t spi_flash_command(spi_flash_cmd_t cmd, void *data_tx, size_t tx_size, void *data_rx, size_t rx_size)
{
    assert(rx_size <= 32); // Reading more than 32 bits back from a SPI flash operation is unsupported
    assert(tx_size <= 64); // Writing more than 64 bytes of data with one SPI command is unsupported

    uint32_t SPI_USR_CMD  = (1 << 31);
    uint32_t SPI_USR_MISO = (1 << 28);
    uint32_t SPI_USR_MOSI = (1 << 27);
    uint32_t SPI_CMD_USR  = (1 << 18);
    uint32_t CMD_LEN_SHIFT = 28;

    // Save SPI configuration
    uint32_t old_spi_usr;
    uint32_t old_spi_usr2;
    RETURN_ON_ERROR( esp_loader_read_register(s_reg->usr, &old_spi_usr) );
    RETURN_ON_ERROR( esp_loader_read_register(s_reg->usr2, &old_spi_usr2) );

    if (s_target == ESP8266_CHIP) {
        RETURN_ON_ERROR( spi_set_data_lengths_8266(tx_size, rx_size) );
    } else {
        RETURN_ON_ERROR( spi_set_data_lengths(tx_size, rx_size) );
    }

    uint32_t usr_reg_2 = (7 << CMD_LEN_SHIFT) | cmd;
    uint32_t usr_reg = SPI_USR_CMD;
    if (rx_size > 0) {
        usr_reg |= SPI_USR_MISO;
    }
    if (tx_size > 0) {
        usr_reg |= SPI_USR_MOSI;
    }

    RETURN_ON_ERROR( esp_loader_write_register(s_reg->usr, usr_reg) );
    RETURN_ON_ERROR( esp_loader_write_register(s_reg->usr2, usr_reg_2 ) );

    if (tx_size == 0) {
        // clear data register before we read it
        RETURN_ON_ERROR( esp_loader_write_register(s_reg->w0, 0) );
    } else {
        uint32_t *data = (uint32_t *)data_tx;
        uint32_t words_to_write = (tx_size + 31) / (8 * 4);
        uint32_t data_reg_addr = s_reg->w0;

        while (words_to_write--) {
            uint32_t word = *data++;
            RETURN_ON_ERROR( esp_loader_write_register(data_reg_addr, word) );
            data_reg_addr += 4;
        }
    }

    RETURN_ON_ERROR( esp_loader_write_register(s_reg->cmd, SPI_CMD_USR) );

    uint32_t trials = 10;
    while (trials--) {
        uint32_t cmd_reg;
        RETURN_ON_ERROR( esp_loader_read_register(s_reg->cmd, &cmd_reg) );
        if ((cmd_reg & SPI_CMD_USR) == 0) {
            break;
        }
    }

    if (trials == 0) {
        return ESP_LOADER_ERROR_TIMEOUT;
    }

    RETURN_ON_ERROR( esp_loader_read_register(s_reg->w0, (uint32_t *)data_rx) );

    // Restore SPI configuration
    RETURN_ON_ERROR( esp_loader_write_register(s_reg->usr, old_spi_usr) );
    RETURN_ON_ERROR( esp_loader_write_register(s_reg->usr2, old_spi_usr2) );

    return ESP_LOADER_SUCCESS;
}

static esp_loader_error_t detect_flash_size(size_t *flash_size)
{
    uint32_t flash_id = 0;

    RETURN_ON_ERROR( spi_flash_command(SPI_FLASH_READ_ID, NULL, 0, &flash_id, 24) );
    uint32_t size_id = flash_id >> 16;

    if (size_id < 0x12 || size_id > 0x18) {
        return ESP_LOADER_ERROR_UNSUPPORTED_CHIP;
    }

    *flash_size = 1 << size_id;

    return ESP_LOADER_SUCCESS;
}

esp_loader_error_t esp_loader_flash_start(uint32_t offset, uint32_t image_size, uint32_t block_size)
{
    uint32_t blocks_to_write = (image_size + block_size - 1) / block_size;
    uint32_t erase_size = block_size * blocks_to_write;
    s_flash_write_size = block_size;

    size_t flash_size = 0;
    if (detect_flash_size(&flash_size) == ESP_LOADER_SUCCESS) {
        if (image_size > flash_size) {
            return ESP_LOADER_ERROR_IMAGE_SIZE;
        }
        loader_port_start_timer(DEFAULT_TIMEOUT);
        RETURN_ON_ERROR( loader_spi_parameters(flash_size) );
    } else {
        loader_port_debug_print("Flash size detection failed, falling back to default");
    }

    init_md5(offset, image_size);

    loader_port_start_timer(timeout_per_mb(erase_size, ERASE_REGION_TIMEOUT_PER_MB));
    return loader_flash_begin_cmd(offset, erase_size, block_size, blocks_to_write, s_target);
}


esp_loader_error_t esp_loader_flash_write(void *payload, uint32_t size)
{
    uint32_t padding_bytes = s_flash_write_size - size;
    uint8_t *data = (uint8_t *)payload;
    uint32_t padding_index = size;

    while (padding_bytes--) {
        data[padding_index++] = PADDING_PATTERN;
    }

    md5_update((const uint8_t *)payload, (size + 3) & ~3);

    loader_port_start_timer(DEFAULT_TIMEOUT);

    return loader_flash_data_cmd(data, s_flash_write_size);
}


esp_loader_error_t esp_loader_flash_finish(bool reboot)
{
    loader_port_start_timer(DEFAULT_TIMEOUT);

    return loader_flash_end_cmd(!reboot);
}


esp_loader_error_t esp_loader_read_register(uint32_t address, uint32_t *reg_value)
{
    loader_port_start_timer(DEFAULT_TIMEOUT);

    return loader_read_reg_cmd(address, reg_value);
}


esp_loader_error_t esp_loader_write_register(uint32_t address, uint32_t reg_value)
{
    loader_port_start_timer(DEFAULT_TIMEOUT);

    return loader_write_reg_cmd(address, reg_value, 0xFFFFFFFF, 0);
}

esp_loader_error_t esp_loader_change_baudrate(uint32_t baudrate)
{
    if (s_target == ESP8266_CHIP) {
        return ESP_LOADER_ERROR_UNSUPPORTED_FUNC;
    }

    loader_port_start_timer(DEFAULT_TIMEOUT);

    return loader_change_baudrate_cmd(baudrate);
}

#if MD5_ENABLED

static void hexify(const uint8_t raw_md5[16], uint8_t hex_md5_out[32])
{
    static const uint8_t dec_to_hex[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };
    for (int i = 0; i < 16; i++) {
        *hex_md5_out++ = dec_to_hex[raw_md5[i] >> 4];
        *hex_md5_out++ = dec_to_hex[raw_md5[i] & 0xF];
    }
}


esp_loader_error_t esp_loader_flash_verify(void)
{
    if (s_target == ESP8266_CHIP) {
        return ESP_LOADER_ERROR_UNSUPPORTED_FUNC;
    }

    uint8_t raw_md5[16];
    uint8_t hex_md5[MD5_SIZE + 1];
    uint8_t received_md5[MD5_SIZE + 1];

    md5_final(raw_md5);
    hexify(raw_md5, hex_md5);

    loader_port_start_timer(timeout_per_mb(s_image_size, MD5_TIMEOUT_PER_MB));

    RETURN_ON_ERROR( loader_md5_cmd(s_start_address, s_image_size, received_md5) );

    bool md5_match = memcmp(hex_md5, received_md5, MD5_SIZE) == 0;

    if (!md5_match) {
        hex_md5[MD5_SIZE] = '\n';
        received_md5[MD5_SIZE] = '\n';

        loader_port_debug_print("Error: MD5 checksum does not match:\n");
        loader_port_debug_print("Expected:\n");
        loader_port_debug_print((char *)received_md5);
        loader_port_debug_print("Actual:\n");
        loader_port_debug_print((char *)hex_md5);

        return ESP_LOADER_ERROR_INVALID_MD5;
    }

    return ESP_LOADER_SUCCESS;
}

#endif

void esp_loader_reset_target(void)
{
    loader_port_reset_target();
}