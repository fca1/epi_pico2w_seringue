#include "tmc5130.h"
#include "board_config.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
static uint8_t transfer(uint8_t a,uint32_t v,bool w,uint32_t*out){
 uint8_t tx[5]={(uint8_t)(a|(w?0x80:0)),v>>24,v>>16,v>>8,v},rx[5]={0};
 gpio_put(PIN_TMC_SPI_CS,0);spi_write_read_blocking(TMC_SPI_PORT,tx,rx,5);gpio_put(PIN_TMC_SPI_CS,1);
 if(out)*out=((uint32_t)rx[1]<<24)|((uint32_t)rx[2]<<16)|((uint32_t)rx[3]<<8)|rx[4];return rx[0];
}
bool tmc5130_init(void){
 spi_init(TMC_SPI_PORT,TMC_SPI_BAUD_HZ);gpio_set_function(PIN_TMC_SPI_SCK,GPIO_FUNC_SPI);
 gpio_set_function(PIN_TMC_SPI_TX,GPIO_FUNC_SPI);gpio_set_function(PIN_TMC_SPI_RX,GPIO_FUNC_SPI);
 gpio_init(PIN_TMC_SPI_CS);gpio_set_dir(PIN_TMC_SPI_CS,GPIO_OUT);gpio_put(PIN_TMC_SPI_CS,1);
 gpio_init(PIN_TMC_ENABLE);gpio_set_dir(PIN_TMC_ENABLE,GPIO_OUT);tmc5130_enable(false);
 if(!tmc5130_write(TMC_GSTAT,7))return false;tmc_reply_t id=tmc5130_read(TMC_IOIN);uint8_t version=id.value>>24;return id.ok&&version!=0&&version!=0xff;
}
tmc_reply_t tmc5130_read(uint8_t r){uint32_t x,v;transfer(r,0,false,&x);uint8_t s=transfer(r,0,false,&v);return(tmc_reply_t){s,v,s!=0xFF};}
bool tmc5130_write(uint8_t r,uint32_t v){return transfer(r,v,true,0)!=0xFF;}
void tmc5130_enable(bool on){gpio_put(PIN_TMC_ENABLE,on?0:1);}
bool tmc5130_configure(uint16_t ms){unsigned m=0;for(unsigned n=256;n>ms&&m<8;n>>=1)++m;
 return (256u>>m)==ms&&tmc5130_write(TMC_IHOLD_IRUN,8u|(24u<<8)|(6u<<16))&&
 tmc5130_write(TMC_CHOPCONF,3u|(4u<<4)|(1u<<15)|(m<<24));}
bool tmc5130_set_ramp(uint32_t v,uint32_t a,uint32_t d){return tmc5130_write(TMC_VSTART,1)&&
 tmc5130_write(TMC_A1,a)&&tmc5130_write(TMC_V1,v/4)&&tmc5130_write(TMC_AMAX,a)&&
 tmc5130_write(TMC_VMAX,v)&&tmc5130_write(TMC_DMAX,d)&&tmc5130_write(TMC_D1,d)&&tmc5130_write(TMC_VSTOP,10);}
bool tmc5130_velocity(int d,uint32_t v){return tmc5130_write(TMC_VMAX,v)&&tmc5130_write(TMC_RAMPMODE,d>0?1:2);}
bool tmc5130_position(int32_t t){return tmc5130_write(TMC_RAMPMODE,0)&&tmc5130_write(TMC_XTARGET,(uint32_t)t);}
bool tmc5130_stop(void){return tmc5130_write(TMC_VMAX,0);}
bool tmc5130_configure_stallguard(bool enabled,int8_t threshold){if(!enabled)return tmc5130_write(TMC_COOLCONF,0);if(threshold<-64||threshold>63)return false;return tmc5130_write(TMC_COOLCONF,((uint32_t)threshold&0x7fu)<<16);}
