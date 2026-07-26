#include "status_led.h"
#if DISPENSER_HAS_RADIO
#include "pico/cyw43_arch.h"
#else
#include "pico/stdlib.h"
#endif
static bool available,level;static uint32_t changed_ms;
static void set_led(bool on){
#if DISPENSER_HAS_RADIO
 cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN,on);
#else
 gpio_put(PICO_DEFAULT_LED_PIN,on);
#endif
}
void status_led_init(bool ready){available=ready;
#if !DISPENSER_HAS_RADIO
 gpio_init(PICO_DEFAULT_LED_PIN);gpio_set_dir(PICO_DEFAULT_LED_PIN,GPIO_OUT);
#endif
 if(available)set_led(false);}
void status_led_update(uint32_t now,bool connected,bool moving){if(!available)return;if(moving){if(!level){level=true;set_led(true);}changed_ms=now;return;}uint32_t interval=connected?100u:500u;if(now-changed_ms>=interval){changed_ms=now;level=!level;set_led(level);}}
