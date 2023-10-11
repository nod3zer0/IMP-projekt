#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <math.h>
#include <rom/ets_sys.h>
#include <stdio.h>
#include <stdlib.h>

// wifi
#include "esp_event.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

static const char *TAG = "ADC EXAMPLE";

static esp_adc_cal_characteristics_t adc1_chars;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < ESP_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "connect to the AP fail");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init_sta(void) {
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = ESP_WIFI_SSID,
              .password = ESP_WIFI_PASS,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished.");

  // wait until connected
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  // write connection result
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", ESP_WIFI_SSID,
             ESP_WIFI_PASS);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
             ESP_WIFI_SSID, ESP_WIFI_PASS);
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }

  // unregister events that are no longer used
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
  vEventGroupDelete(s_wifi_event_group);
}

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

  wifi_init_sta();
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