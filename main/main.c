/* app_main.c */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lora_gateway.h"

void app_main(void)
{
    lora_gateway_start();
}