#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <math.h>
#include <rom/ets_sys.h>
#include <stdio.h>
#include <stdlib.h>

// sntp
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

// timer
#include "driver/gpio.h"
#include "esp_timer.h"

// wifi
#include "esp_event.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <esp_http_server.h>

#define ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

#define LED_GPIO GPIO_NUM_2

static const char *TAG = "ADC EXAMPLE";

int temp_limit = 0;
int hystereze = 0;
int led_is_on = 0;

nvs_handle_t nvsHandle;
nvs_handle_t nvsTemperature;
esp_err_t err;

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
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", ESP_WIFI_SSID,
             ESP_WIFI_PASS);
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

void init_nvs(esp_err_t *err) {

  *err = nvs_flash_init();
  if (*err == ESP_ERR_NVS_NO_FREE_PAGES ||
      *err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    *err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(*err);
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
}

double get_temp() {
  uint32_t voltage =
      esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_7), &adc1_chars);
  double temp = ((8.194 - (sqrt(((-8.194) * (-8.194)) +
                                (4 * 0.00262 * (1324.0 - voltage * 1.0))))) /
                 (2 * (-0.00262))) +
                30;
  return temp;
}

void renderForm(httpd_req_t *req) {
  char *resp = malloc(1000);

  sprintf((char *)resp,
          "<h1>temp is: %f</h1>\n"
          "<h1>temp limit is: %d</h1>\n"
          "<form action=\"/\" method=\"post\">\n"
          "  <label for=\"hranice_teploty\">Hranice teploty</label>\n"
          "  <input type=\"text\" id=\"hranice_teploty\" "
          "name=\"hranice_teploty\"><br><br>\n"
          "  <input type=\"submit\" value=\"Submit\">\n"
          "</form>",
          get_temp(), temp_limit);

  // sprintf((char *)resp, "<h1>temp is: %f</h1>", get_temp());
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

esp_err_t root_get_handler(httpd_req_t *req) {

  renderForm(req);
  return ESP_OK;
}

esp_err_t root_post_handler(httpd_req_t *req) {
  char *buf = malloc(req->content_len + 1);

  // int dataLength = esp_http_client_get_post_field(req->handle, buf);

  // print data
  // printf("Data: %s\n", buf);

  int ret = httpd_req_recv(req, buf, req->content_len);

  // parse data
  char *hranice_teploty = strstr(buf, "hranice_teploty=");
  if (hranice_teploty != NULL) {
    hranice_teploty += 16;
    temp_limit = atoi(hranice_teploty);
  }

  // print data
  printf("Data: %s\n", buf);
  renderForm(req);
  return ESP_OK;
}

esp_err_t getTempsHandler(httpd_req_t *req) {
  // get all timestamps from nvs
  // get all temps from nvs

  // Example of listing all the key-value pairs of any type under specified
  // handle (which defines a partition and namespace)
  char *list = malloc(1000);
  nvs_iterator_t it = NULL;
  esp_err_t res = nvs_entry_find("nvs", "temp", NVS_TYPE_ANY, &it);
  sprintf(list, "<h1> Temperatures </h1>\n");
  sprintf(list + strlen(list),
          "<table> <tr> <th> Time </th> <th> Temp </th> </tr>");

  // print res error name
  printf("Error: %s\n", esp_err_to_name(res));

  while (res == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);

    char time[300];
    sprintf(time, "%s", info.key);
    // convert to  human readable time
    time_t t = atoi(time);
    struct tm tm;
    localtime_r(&t, &tm);
    char timeHuman[300];
    strftime(timeHuman, sizeof(timeHuman), "%c", &tm);

    // read value

    int32_t temp_int = 0;
    nvs_get_i32(nvsTemperature, info.key, &temp_int);

    sprintf(list + strlen(list), "<tr><td> %s </td>\n<td> %f </td>\n</tr>\n",
            timeHuman, (temp_int / 1000.0));
    char *tmpList = realloc(list, strlen(list) + 200);
    if (tmpList != NULL) {
      list = tmpList;
    }
    res = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  sprintf(list + strlen(list), "</table>");

  httpd_resp_send(req, list, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

void timeInit() {

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&config);
  if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
    printf("Failed to update system time within 10s timeout");
  }
  // set GMT+2 time zone
  setenv("TZ", "GMT+2", 1);
  tzset();
}

char *getTimeNow() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char *timeNow = malloc(100);
  strftime(timeNow, 100, "%x-%X", &timeinfo);
  return timeNow;
}

int saveTempToNVSwithTimestamp(nvs_handle_t nvsHandle, double temp) {
  esp_err_t err;
  char timeNow[100];
  int32_t temp_int = temp * 1000;
  time_t now;
  time(&now);
  sprintf(timeNow, "%llu", now);
  err = nvs_set_i32(nvsHandle, timeNow, temp_int);
  nvs_commit(nvsHandle);
  return err;
}

void readTemperature_timer(void *param) {
  uint32_t voltage;
  voltage =
      esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_7), &adc1_chars);
  double temp = ((8.194 - (sqrt(((-8.194) * (-8.194)) +
                                (4 * 0.00262 * (1324.0 - voltage * 1.0))))) /
                 (2 * (-0.00262))) +
                30;
  ESP_LOGI(TAG, "ADC1_CHANNEL_6: %d mV", (int)voltage);
  ESP_LOGI(TAG, "Temp: %f C", temp);
  write_temp_to_nvs(nvsHandle, temp);
  saveTempToNVSwithTimestamp(nvsTemperature, temp);
  double temp2 = 0;
  temp2 = read_temp_from_nvs(nvsHandle, &err);
  ESP_LOGI(TAG, "Temp from NVS: %f C", temp2);
  // ets_delay_us(1000000);

  if ((temp < (temp_limit - hystereze)) && led_is_on == 1) {
    led_is_on = 0;
    gpio_set_level(LED_GPIO, 1);
  }

  if ((temp > (temp_limit + hystereze)) && led_is_on == 0) {
    led_is_on = 1;
    gpio_set_level(LED_GPIO, 0);
  }
  ESP_LOGI(TAG, "LED: %d", led_is_on);
}

void initTimer() {
  const esp_timer_create_args_t my_timer_args = {
      .callback = &readTemperature_timer, .name = "Read Temp"};
  esp_timer_handle_t timer_handler;
  ESP_ERROR_CHECK(esp_timer_create(&my_timer_args, &timer_handler));
  ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handler, 1000000));
}

void app_main(void) {

  gpio_reset_pin(LED_GPIO);
  // Set the GPIO as a push/pull output
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

  // init time
  timeInit();
  // Initialize NVS

  init_nvs(&err);
  err = nvs_open("temp", NVS_READWRITE, &nvsTemperature);
  if (err != ESP_OK) {
    printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  }
  err = nvs_open("storage", NVS_READWRITE, &nvsHandle);
  if (err != ESP_OK) {
    printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  }

  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT,
                           0, &adc1_chars);

  ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
  ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11));

  // wifi connect
  wifi_init_sta();

  // HTTP Server
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {

    httpd_uri_t uri = {.uri = "/",
                       .method = HTTP_GET,
                       .handler = root_get_handler,
                       .user_ctx = NULL};
    httpd_register_uri_handler(server, &uri);

    httpd_uri_t uri2 = {.uri = "/",
                        .method = HTTP_POST,
                        .handler = root_post_handler,
                        .user_ctx = NULL};
    httpd_register_uri_handler(server, &uri2);

    httpd_uri_t getTempsUri = {.uri = "/getTemps",
                               .method = HTTP_GET,
                               .handler = getTempsHandler,
                               .user_ctx = NULL};
    httpd_register_uri_handler(server, &getTempsUri);
  }
  // init timer
  initTimer();
  //______________
  while (1) {

    vTaskDelay(pdMS_TO_TICKS(100)); // to prevent watchdog reset
  }
}