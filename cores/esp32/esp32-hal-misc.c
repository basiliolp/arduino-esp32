// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#ifdef CONFIG_BT_ENABLED
#include "esp_bt.h"
#endif //CONFIG_BT_ENABLED
#include <sys/time.h>
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"
#include "rom/rtc.h"
#include "esp32-hal.h"

//Undocumented!!! Get chip temperature in Farenheit
//Source: https://github.com/pcbreflux/espressif/blob/master/esp32/arduino/sketchbook/ESP32_int_temp_sensor/ESP32_int_temp_sensor.ino
uint8_t temprature_sens_read();

float temperatureRead()
{
    return (temprature_sens_read() - 32) / 1.8;
}

void yield()
{
    vPortYield();
}

static uint32_t _cpu_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ;
static uint32_t _sys_time_multiplier = 1;

bool setCpuFrequency(uint32_t cpu_freq_mhz){
    rtc_cpu_freq_config_t conf, cconf;
    rtc_clk_cpu_freq_get_config(&cconf);
    if(cconf.freq_mhz == cpu_freq_mhz && _cpu_freq_mhz == cpu_freq_mhz){
        return true;
    }
    if(!rtc_clk_cpu_freq_mhz_to_config(cpu_freq_mhz, &conf)){
        log_e("CPU clock could not be set to %u MHz", cpu_freq_mhz);
        return false;
    }
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    log_i("%s: %u / %u = %u Mhz", (conf.source == RTC_CPU_FREQ_SRC_PLL)?"PLL":((conf.source == RTC_CPU_FREQ_SRC_APLL)?"APLL":((conf.source == RTC_CPU_FREQ_SRC_XTAL)?"XTAL":"8M")), conf.source_freq_mhz, conf.div, conf.freq_mhz);
    delay(10);
#endif
    rtc_clk_cpu_freq_set_config_fast(&conf);
    _cpu_freq_mhz = conf.freq_mhz;
    _sys_time_multiplier = 80000000 / getApbFrequency();
    return true;
}

uint32_t getCpuFrequency(){
    rtc_cpu_freq_config_t conf;
    rtc_clk_cpu_freq_get_config(&conf);
    return conf.freq_mhz;
}

uint32_t getApbFrequency(){
    rtc_cpu_freq_config_t conf;
    rtc_clk_cpu_freq_get_config(&conf);
    if(conf.freq_mhz >= 80){
        return 80000000;
    }
    return (conf.source_freq_mhz * 1000000) / conf.div;
}

unsigned long IRAM_ATTR micros()
{
    return (unsigned long) (esp_timer_get_time()) * _sys_time_multiplier;
}

unsigned long IRAM_ATTR millis()
{
    return (unsigned long) (micros() / 1000);
}

void delay(uint32_t ms)
{
    vTaskDelay((ms * _cpu_freq_mhz) / (portTICK_PERIOD_MS * 240));
}

void IRAM_ATTR delayMicroseconds(uint32_t us)
{
    uint32_t m = micros();
    if(us){
        uint32_t e = (m + us);
        if(m > e){ //overflow
            while(micros() > e){
                NOP();
            }
        }
        while(micros() < e){
            NOP();
        }
    }
}

void initVariant() __attribute__((weak));
void initVariant() {}

void init() __attribute__((weak));
void init() {}

#ifdef CONFIG_BT_ENABLED
//overwritten in esp32-hal-bt.c
bool btInUse() __attribute__((weak));
bool btInUse(){ return false; }
#endif

void initArduino()
{
#ifdef F_CPU
    setCpuFrequency(F_CPU/1000000L);
#endif
#if CONFIG_SPIRAM_SUPPORT
    psramInit();
#endif
    esp_log_level_set("*", CONFIG_LOG_DEFAULT_LEVEL);
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES){
        const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
        if (partition != NULL) {
            err = esp_partition_erase_range(partition, 0, partition->size);
            if(!err){
                err = nvs_flash_init();
            } else {
                log_e("Failed to format the broken NVS partition!");
            }
        }
    }
    if(err) {
        log_e("Failed to initialize NVS! Error: %u", err);
    }
#ifdef CONFIG_BT_ENABLED
    if(!btInUse()){
        esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
    }
#endif
    init();
    initVariant();
}

//used by hal log
const char * IRAM_ATTR pathToFileName(const char * path)
{
    size_t i = 0;
    size_t pos = 0;
    char * p = (char *)path;
    while(*p){
        i++;
        if(*p == '/' || *p == '\\'){
            pos = i;
        }
        p++;
    }
    return path+pos;
}

