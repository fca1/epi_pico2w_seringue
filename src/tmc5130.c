#include "tmc5130.h"
#include "board_config.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "tmc_current.h"

/*
 * TMC5130 register maps for the GCC little-endian ARM target. Packing and
 * compile-time size checks make every register exactly one 32-bit SPI word.
 * Reserved fields are deliberately visible so both zero and one bits can be
 * reviewed without decoding masks and shifts.
 */
#pragma pack(push,1)
typedef union {
 uint32_t value;
 struct {
  uint32_t i_scale_analog:1;
  uint32_t internal_rsense:1;
  uint32_t en_pwm_mode:1;
  uint32_t enc_commutation:1;
  uint32_t shaft:1;
  uint32_t diag0_error:1;
  uint32_t diag0_otpw:1;
  uint32_t diag0_stall:1;
  uint32_t diag1_stall:1;
  uint32_t diag1_index:1;
  uint32_t diag1_onstate:1;
  uint32_t diag1_steps_skipped:1;
  uint32_t diag0_int_pushpull:1;
  uint32_t diag1_pushpull:1;
  uint32_t small_hysteresis:1;
  uint32_t stop_enable:1;
  uint32_t direct_mode:1;
  uint32_t test_mode:1;
  uint32_t reserved_31_18:14;
 } bits;
} tmc_gconf_reg_t;

typedef union {
 uint32_t value;
 struct {
  uint32_t reset:1;
  uint32_t drv_err:1;
  uint32_t uv_cp:1;
  uint32_t reserved_31_3:29;
 } bits;
} tmc_gstat_reg_t;

typedef union {
 uint32_t value;
 struct {
  uint32_t ihold:5;
  uint32_t reserved_7_5:3;
  uint32_t irun:5;
  uint32_t reserved_15_13:3;
  uint32_t iholddelay:4;
  uint32_t reserved_31_20:12;
 } bits;
} tmc_ihold_irun_reg_t;

typedef union {
 uint32_t value;
 struct {
  uint32_t toff:4;
  uint32_t hstrt:3;
  uint32_t hend:4;
  uint32_t fd3:1;
  uint32_t disfdcc:1;
  uint32_t rndtf:1;
  uint32_t chm:1;
  uint32_t tbl:2;
  uint32_t vsense:1;
  uint32_t vhighfs:1;
  uint32_t vhighchm:1;
  uint32_t sync:4;
  uint32_t mres:4;
  uint32_t intpol:1;
  uint32_t dedge:1;
  uint32_t diss2g:1;
  uint32_t reserved_31:1;
 } bits;
} tmc_chopconf_reg_t;

typedef union {
 uint32_t value;
 struct {
  uint32_t semin:4;
  uint32_t reserved_4:1;
  uint32_t seup:2;
  uint32_t reserved_7:1;
  uint32_t semax:4;
  uint32_t reserved_12:1;
  uint32_t sedn:2;
  uint32_t seimin:1;
  uint32_t sgt:7;
  uint32_t reserved_23:1;
  uint32_t sfilt:1;
  uint32_t reserved_31_25:7;
 } bits;
} tmc_coolconf_reg_t;

typedef union {
 uint32_t value;
 struct {
  uint32_t threshold:20;
  uint32_t reserved_31_20:12;
 } bits;
} tmc_tcoolthrs_reg_t;

typedef union {
 uint32_t value;
 struct {
  uint32_t refl_step:1;
  uint32_t refl_dir:1;
  uint32_t encb_dcen_cfg4:1;
  uint32_t enca_dcin_cfg5:1;
  uint32_t drv_enn_cfg6:1;
  uint32_t enc_n_dco_cfg7:1;
  uint32_t sd_mode:1;
  uint32_t swcomp_in:1;
  uint32_t reserved_23_8:16;
  uint32_t version:8;
 } bits;
} tmc_ioin_reg_t;
#pragma pack(pop)

_Static_assert(sizeof(tmc_gconf_reg_t)==4,"TMC GCONF register must be 4 bytes");
_Static_assert(sizeof(tmc_gstat_reg_t)==4,"TMC GSTAT register must be 4 bytes");
_Static_assert(sizeof(tmc_ihold_irun_reg_t)==4,"TMC IHOLD_IRUN register must be 4 bytes");
_Static_assert(sizeof(tmc_chopconf_reg_t)==4,"TMC CHOPCONF register must be 4 bytes");
_Static_assert(sizeof(tmc_coolconf_reg_t)==4,"TMC COOLCONF register must be 4 bytes");
_Static_assert(sizeof(tmc_tcoolthrs_reg_t)==4,"TMC TCOOLTHRS register must be 4 bytes");
_Static_assert(sizeof(tmc_ioin_reg_t)==4,"TMC IOIN register must be 4 bytes");

static const tmc_gstat_reg_t TMC_GSTAT_CLEAR={.bits={
 .reset=1,.drv_err=1,.uv_cp=1,.reserved_31_3=0
}};

static const tmc_gconf_reg_t TMC_GCONF_INTERNAL_RSENSE={.bits={
 .i_scale_analog=0,.internal_rsense=1,.en_pwm_mode=0,.enc_commutation=0,
 .shaft=0,.diag0_error=0,.diag0_otpw=0,.diag0_stall=0,
 .diag1_stall=0,.diag1_index=0,.diag1_onstate=0,.diag1_steps_skipped=0,
 .diag0_int_pushpull=0,.diag1_pushpull=0,.small_hysteresis=0,
 .stop_enable=0,.direct_mode=0,.test_mode=0,.reserved_31_18=0
}};

static const tmc_ihold_irun_reg_t TMC_IHOLD_IRUN_DEFAULT={.bits={
 .ihold=0,.reserved_7_5=0,.irun=0,.reserved_15_13=0,
 .iholddelay=6,.reserved_31_20=0
}};

static const tmc_chopconf_reg_t TMC_CHOPCONF_DEFAULT={.bits={
 .toff=3,.hstrt=4,.hend=0,.fd3=0,.disfdcc=0,.rndtf=0,.chm=0,
 .tbl=1,.vsense=0,.vhighfs=0,.vhighchm=0,.sync=0,.mres=0,
 .intpol=0,.dedge=0,.diss2g=0,.reserved_31=0
}};

static const tmc_coolconf_reg_t TMC_COOLCONF_DISABLED={.bits={
 .semin=0,.reserved_4=0,.seup=0,.reserved_7=0,.semax=0,.reserved_12=0,
 .sedn=0,.seimin=0,.sgt=0,.reserved_23=0,.sfilt=0,.reserved_31_25=0
}};

static const tmc_tcoolthrs_reg_t TMC_TCOOLTHRS_DISABLED={.bits={
 .threshold=0,.reserved_31_20=0
}};

static const tmc_tcoolthrs_reg_t TMC_TCOOLTHRS_ENABLED={.bits={
 .threshold=0xfffff,.reserved_31_20=0
}};

typedef union {uint32_t value;uint8_t byte[4];} tmc_word_t;
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "TMC5130 SPI byte serialization requires a little-endian target"
#endif

static uint8_t transfer(uint8_t a,uint32_t v,bool w,uint32_t*out){
 const tmc_word_t write_word={.value=v};tmc_word_t read_word={.value=0};
 uint8_t tx[5]={(uint8_t)(a|(w?0x80:0)),write_word.byte[3],write_word.byte[2],write_word.byte[1],write_word.byte[0]},rx[5]={0};
 gpio_put(PIN_TMC_SPI_CS,0);spi_write_read_blocking(TMC_SPI_PORT,tx,rx,5);gpio_put(PIN_TMC_SPI_CS,1);
 read_word.byte[3]=rx[1];read_word.byte[2]=rx[2];read_word.byte[1]=rx[3];read_word.byte[0]=rx[4];if(out)*out=read_word.value;return rx[0];
}
bool tmc5130_init(void){
 spi_init(TMC_SPI_PORT,TMC_SPI_BAUD_HZ);gpio_set_function(PIN_TMC_SPI_SCK,GPIO_FUNC_SPI);
 gpio_set_function(PIN_TMC_SPI_TX,GPIO_FUNC_SPI);gpio_set_function(PIN_TMC_SPI_RX,GPIO_FUNC_SPI);
 gpio_init(PIN_TMC_SPI_CS);gpio_set_dir(PIN_TMC_SPI_CS,GPIO_OUT);gpio_put(PIN_TMC_SPI_CS,1);
 gpio_init(PIN_TMC_ENABLE);gpio_set_dir(PIN_TMC_ENABLE,GPIO_OUT);tmc5130_enable(false);
 if(!tmc5130_write(TMC_GSTAT,TMC_GSTAT_CLEAR.value))return false;tmc_reply_t id=tmc5130_read(TMC_IOIN);tmc_ioin_reg_t ioin={.value=id.value};return id.ok&&ioin.bits.version!=0&&ioin.bits.version!=0xff;
}
tmc_reply_t tmc5130_read(uint8_t r){uint32_t x,v;transfer(r,0,false,&x);uint8_t s=transfer(r,0,false,&v);return(tmc_reply_t){s,v,s!=0xFF};}
bool tmc5130_write(uint8_t r,uint32_t v){return transfer(r,v,true,0)!=0xFF;}
void tmc5130_enable(bool on){gpio_put(PIN_TMC_ENABLE,on?0:1);}
bool tmc5130_configure(uint16_t ms,uint16_t run_mA,uint16_t hold_mA){uint8_t mres;switch(ms){case 256:mres=0;break;case 128:mres=1;break;case 64:mres=2;break;case 32:mres=3;break;case 16:mres=4;break;case 8:mres=5;break;case 4:mres=6;break;case 2:mres=7;break;case 1:mres=8;break;default:return false;}uint8_t irun=tmc_internal_current_scale_from_mA(run_mA,TMC_INTERNAL_RREF_OHM),ihold=tmc_internal_current_scale_from_mA(hold_mA,TMC_INTERNAL_RREF_OHM);
 tmc_ihold_irun_reg_t current=TMC_IHOLD_IRUN_DEFAULT;current.bits.ihold=ihold;current.bits.irun=irun;
 tmc_chopconf_reg_t chopper=TMC_CHOPCONF_DEFAULT;chopper.bits.mres=mres;
 return tmc5130_write(TMC_GCONF,TMC_GCONF_INTERNAL_RSENSE.value)&&
 tmc5130_write(TMC_IHOLD_IRUN,current.value)&&tmc5130_write(TMC_CHOPCONF,chopper.value);}
bool tmc5130_set_ramp(uint32_t v,uint32_t a1,uint32_t amax,uint32_t dmax,uint32_t d1){return tmc5130_write(TMC_VSTART,1)&&
 tmc5130_write(TMC_A1,a1)&&tmc5130_write(TMC_V1,v/4)&&tmc5130_write(TMC_AMAX,amax)&&
 tmc5130_write(TMC_VMAX,v)&&tmc5130_write(TMC_DMAX,dmax)&&tmc5130_write(TMC_D1,d1)&&tmc5130_write(TMC_VSTOP,10);}
bool tmc5130_velocity(int d,uint32_t v){return tmc5130_write(TMC_VMAX,v)&&tmc5130_write(TMC_RAMPMODE,d>0?1:2);}
bool tmc5130_position(int32_t t){return tmc5130_write(TMC_RAMPMODE,0)&&tmc5130_write(TMC_XTARGET,(uint32_t)t);}
bool tmc5130_stop(void){return tmc5130_write(TMC_VMAX,0);}
bool tmc5130_configure_stallguard(bool enabled,int8_t threshold){
 if(!enabled)return tmc5130_write(TMC_TCOOLTHRS,TMC_TCOOLTHRS_DISABLED.value)&&tmc5130_write(TMC_COOLCONF,TMC_COOLCONF_DISABLED.value);
 if(threshold<-64||threshold>63)return false;
 tmc_coolconf_reg_t coolconf=TMC_COOLCONF_DISABLED;coolconf.bits.sgt=(uint8_t)threshold;
 return tmc5130_write(TMC_COOLCONF,coolconf.value)&&tmc5130_write(TMC_TCOOLTHRS,TMC_TCOOLTHRS_ENABLED.value);
}
