#include <stdlib.h>
#include <string.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system_interface.h"

void *system_malloc(size_t n)
{
    return malloc(n);
}

void *system_calloc(size_t n, size_t size)
{
    return calloc(n, size);
}

void system_free(void *ptr)
{
    free(ptr);
}

uint32_t system_ticks(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

uint32_t system_timestamp(void)
{
    return system_ticks() / 1000U;
}

void system_sleep(uint32_t time_ms)
{
    vTaskDelay(pdMS_TO_TICKS(time_ms ? time_ms : 1));
}

uint32_t system_random(void)
{
    return esp_random();
}
