/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/* DESCRIPTION:
 * This example contains code to make ESP32-S3 based device recognizable by
 * USB-hosts as a USB Mass Storage Device. It either allows the embedded
 * application i.e. example to access the partition or Host PC accesses the
 * partition over USB MSC. They can't be allowed to access the partition at the
 * same time. For different scenarios and behaviour, Refer to README of this
 * example.
 */

#include <errno.h>
#include <dirent.h>
#include "esp_console.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"

#ifdef CONFIG_EXAMPLE_STORAGE_MEDIA_SDMMCCARD
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#endif

#include "M5Unified.h"
#include "M5GFX.h"

#include "background_1.h"
#include "background_2.h"

#define MOUNT_POINT "/sdcard"

#define PIN_NUM_MISO GPIO_NUM_8
#define PIN_NUM_MOSI GPIO_NUM_6
#define PIN_NUM_CLK  GPIO_NUM_7
#define PIN_NUM_CS   GPIO_NUM_NC

static const char *TAG = "example_main";

/* TinyUSB descriptors
 ********************************************************************* */
#define EPNUM_MSC           1
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

enum { ITF_NUM_MSC = 0, ITF_NUM_TOTAL };

enum {
    EDPT_CTRL_OUT = 0x00,
    EDPT_CTRL_IN  = 0x80,

    EDPT_MSC_OUT = 0x01,
    EDPT_MSC_IN  = 0x81,
};

static uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute,
    // power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN,
                       TUD_OPT_HIGH_SPEED ? 512 : 64),
};

static tusb_desc_device_t descriptor_config = {
    .bLength         = sizeof(descriptor_config),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB          = 0x0200,
    .bDeviceClass    = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,  // This is Espressif VID. This needs to be changed
                         // according to Users / Customers
    .idProduct          = 0x4002,
    .bcdDevice          = 0x100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01};

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "TinyUSB",                   // 1: Manufacturer
    "TinyUSB Device",            // 2: Product
    "123456",                    // 3: Serials
    "Example MSC",               // 4. MSC
};
/*********************************************************************** TinyUSB
 * descriptors*/

#define BASE_PATH "/data"  // base path to mount the partition

#define PROMPT_STR CONFIG_IDF_TARGET

// mount the partition and show all the files in BASE_PATH
static void _mount(void) {
    ESP_LOGI(TAG, "Mount storage...");
    ESP_ERROR_CHECK(tinyusb_msc_storage_mount(BASE_PATH));

    // List all the files in this directory
    ESP_LOGI(TAG, "\nls command output:");
    struct dirent *d;
    DIR *dh = opendir(BASE_PATH);
    if (!dh) {
        if (errno == ENOENT) {
            // If the directory is not found
            ESP_LOGE(TAG, "Directory doesn't exist %s", BASE_PATH);
        } else {
            // If the directory is not readable then throw error and exit
            ESP_LOGE(TAG, "Unable to read directory %s", BASE_PATH);
        }
        return;
    }
    // While the next entry is not readable we will print directory files
    while ((d = readdir(dh)) != NULL) {
        printf("%s\n", d->d_name);
    }
    return;
}

extern "C" {
void app_main(void) {
    M5.begin();
    M5.Display.setTextDatum(top_center);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextFont(&fonts::FreeSansBold9pt7b);
    M5.Display.setTextSize(1);
    M5.Display.pushImage(0, 0, 128, 128, image_data_background_1);

    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing storage...");

    static sdmmc_card_t *card = NULL;

    card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    if (card == NULL) {
        ESP_LOGD(TAG, "could not locate new sdmmc_card_t");
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif  // EXAMPLE_FORMAT_IF_MOUNT_FAILED
        // .max_files            = 5,
        .allocation_unit_size = 16 * 1024};

    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "Using SPI peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT
    // (20MHz) For setting a specific frequency, use host.max_freq_khz (range
    // 400kHz - 20MHz for SDSPI) Example: for fixed frequency of 10MHz, use
    // host.max_freq_khz = 10000;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_NUM_MOSI,
        .miso_io_num     = PIN_NUM_MISO,
        .sclk_io_num     = PIN_NUM_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg,
                             SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    // This initializes the slot without card detect (CD) and write protect (WP)
    // signals. Modify slot_config.gpio_cd and slot_config.gpio_wp if your board
    // has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs               = PIN_NUM_CS;
    slot_config.host_id               = (spi_host_device_t)host.slot;
    int card_handle                   = -1;  // uninitialized
    sdspi_host_init_device((const sdspi_device_config_t *)&slot_config,
                           &card_handle);

    while (sdmmc_card_init(&host, card) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sdcard.");
        vTaskDelay(pdMS_TO_TICKS(1000));
    };
    ESP_LOGI(TAG, "Success initialize sdcard.");
    // ESP_LOGI(
    //     TAG, "Size: %lluMB\n",
    //     ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024.0 *
    //     1024.0));

    M5.Display.pushImage(0, 0, 128, 128, image_data_background_2);

    sdmmc_card_print_info(stdout, card);
    const tinyusb_msc_sdmmc_config_t config_sdmmc = {.card = card};
    ESP_ERROR_CHECK(tinyusb_msc_storage_init_sdmmc(&config_sdmmc));

    // mounted in the app by default
    _mount();

    uint64_t out_total_bytes = 0;
    uint64_t out_free_bytes  = 0;

    esp_vfs_fat_info(BASE_PATH, &out_total_bytes, &out_free_bytes);

    float free_mb  = (out_free_bytes / 1024.0 / 1024.0);
    float total_mb = (out_total_bytes / 1024.0 / 1024.0);

    ESP_LOGI(TAG, "Size: %.1f/%.1fMB\n", free_mb, total_mb);

    // M5.Display.fillRect(9, 84, 50, 22, 0x4e7f);

    char free[30];
    char total[30];

    sprintf(free, "F: %.1fMB", free_mb);
    sprintf(total, "T: %.1fMB", total_mb);

    M5.Display.setTextColor(GREEN);
    M5.Display.drawString(free, M5.Display.width() / 2, 52);
    M5.Display.setTextColor(0x4e7f);
    M5.Display.drawString(total, M5.Display.width() / 2, 97);

    ESP_LOGI(TAG, "USB MSC initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &descriptor_config,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count =
            sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy             = false,
        .configuration_descriptor = desc_configuration,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB MSC initialization DONE");

    while (1) {
        M5.update();
        if (M5.BtnA.wasClicked()) {
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
}
