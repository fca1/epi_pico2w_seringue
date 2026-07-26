#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_tasks.h"

void vApplicationMallocFailedHook(void){taskDISABLE_INTERRUPTS();for(;;)tight_loop_contents();}
void vApplicationStackOverflowHook(TaskHandle_t task,char *name){(void)task;(void)name;taskDISABLE_INTERRUPTS();for(;;)tight_loop_contents();}
void vApplicationIdleHook(void){tight_loop_contents();}

int main(void){
 stdio_init_all();
 app_tasks_start();
 vTaskStartScheduler();
 panic("FreeRTOS scheduler stopped");
}
