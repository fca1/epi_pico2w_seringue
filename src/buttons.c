#include "buttons.h"
#include "board_config.h"
#include "hardware/gpio.h"
typedef struct {bool stable,sample;uint32_t changed;} db_t;
static db_t p,u,d;
static bool debounce(db_t*d,bool sample,uint32_t now){
  if(sample!=d->sample){d->sample=sample;d->changed=now;}
  if(sample!=d->stable&&now-d->changed>=BUTTON_DEBOUNCE_MS)d->stable=sample;
  return d->stable;
}
void buttons_init(void){
  gpio_init(PIN_SW_PUSH);gpio_set_dir(PIN_SW_PUSH,GPIO_IN);gpio_pull_up(PIN_SW_PUSH);
  gpio_init(PIN_SW_PULL);gpio_set_dir(PIN_SW_PULL,GPIO_IN);gpio_pull_up(PIN_SW_PULL);
  gpio_init(PIN_SW_DOSE);gpio_set_dir(PIN_SW_DOSE,GPIO_IN);gpio_pull_up(PIN_SW_DOSE);
}
button_state_t buttons_update(uint32_t now){
  button_state_t s={debounce(&p,!gpio_get(PIN_SW_PUSH),now),
    debounce(&u,!gpio_get(PIN_SW_PULL),now),debounce(&d,!gpio_get(PIN_SW_DOSE),now),false};
  s.conflict=s.push&&s.pull;return s;
}
