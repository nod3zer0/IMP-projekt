#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <math.h>
#include <rom/ets_sys.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "ADC EXAMPLE";

static esp_adc_cal_characteristics_t adc1_chars;

nvs_handle_t init_nvs(esp_err_t *err) {

  *err = nvs_flash_init();
  if (*err == ESP_ERR_NVS_NO_FREE_PAGES ||
      *err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    *err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(*err);

  nvs_handle_t nvsHandle;
  *err = nvs_open("storage", NVS_READWRITE, &nvsHandle);
  return nvsHandle;
}

int write_temp_to_nvs(nvs_handle_t nvsHandle, double temp) {
  esp_err_t err;
  int32_t temp_int = temp * 1000;
  err = nvs_set_i32(nvsHandle, "temp", temp_int);
  return err;
}

double read_temp_from_nvs(nvs_handle_t nvsHandle, esp_err_t *err) {
  int32_t temp_int = 0;
  *err = nvs_get_i32(nvsHandle, "temp", &temp_int);
  return temp_int / 1000.0;

  //   switch (err) {
  //   case ESP_OK:
  //     printf("Done\n");
  //     printf("Restart counter = %" PRIu32 "\n", restart_counter);
  //     break;
  //   case ESP_ERR_NVS_NOT_FOUND:
  //     printf("The value is not initialized yet!\n");
  //     break;
  //   default:
  //     printf("Error (%s) reading!\n", esp_err_to_name(err));
  //   }
}

void app_main(void) {

  // Initialize NVS
  nvs_handle_t nvsHandle;
  esp_err_t err;
  nvsHandle = init_nvs(&err);
  if (err != ESP_OK) {
    printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  }


  uint32_t voltage;

  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT,
                           0, &adc1_chars);

  ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
  ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11));

  while (1) {
    voltage =
        esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_7), &adc1_chars);
    double temp = ((8.194 - (sqrt(((-8.194) * (-8.194)) +
                                  (4 * 0.00262 * (1324.0 - voltage * 1.0))))) /
                   (2 * (-0.00262))) +
                  30;
    ESP_LOGI(TAG, "ADC1_CHANNEL_6: %d mV", (int)voltage);
    ESP_LOGI(TAG, "Temp: %f C", temp);
    write_temp_to_nvs(nvsHandle, temp);
    double temp2 = 0;
    temp2 = read_temp_from_nvs(nvsHandle, &err);
    ESP_LOGI(TAG, "Temp from NVS: %f C", temp2);
    ets_delay_us(1000000);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}