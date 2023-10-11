#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include <math.h>

static const char *TAG = "ADC EXAMPLE";

static esp_adc_cal_characteristics_t adc1_chars;

void app_main(void)
{
    uint32_t voltage;

    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);

    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11));

    while (1)
    {
        voltage = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_7), &adc1_chars);
        double temp = ((8.194 - (sqrt(((-8.194)*(-8.194))+(4*0.00262*(1324.0-voltage*1.0)))))/(2*(-0.00262)))+30;
        ESP_LOGI(TAG, "ADC1_CHANNEL_6: %d mV", (int)voltage);
        ESP_LOGI(TAG, "Temp: %f C", temp);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}