#include <stdint.h>

#include "pskeys.h"
#include "user_utils.h"

#include "driver_codec.h"
#include "driver_ipc.h"
#include "driver_syscntl.h"
#include "driver_i2s.h"
#include "driver_pmu.h"

#include "os_timer.h"

#define ENABLE_MEDIA_EQ             0

os_timer_t audio_codec_timer;
uint8_t dsp_nrec_start_flag = false;

void audio_codec_timer_func(void *arg)
{
    if(ipc_get_spk_type() == IPC_SPK_TYPE_I2S){
        //analog pd
        REG_PL_WR(0x50022360,0x3b); // adc en
        REG_PL_WR(0x50022368,0xff); // pa&mix pd
        REG_PL_WR(0x5002236c,0x20); // pga pu
        //i2s_init_imp(8000);
    }
    else{
        
        //printf("codec pu\r\n");
        //analog pd
        REG_PL_WR(0x50022360,0x00); // adc en
        REG_PL_WR(0x50022368,0x00); // pa&mix pd
        REG_PL_WR(0x5002236c,0x20); // pga pu
        REG_PL_WR(0x50022378,0x18); // mic config, single

        //ʨ׃spk Ӵ
        REG_PL_WR(0x50022000+(0xd6<<2),pskeys.app_data.hf_vol);
        REG_PL_WR(0x50022000+(0xd7<<2),pskeys.app_data.hf_vol);
        
        REG_PL_WR(0x50022000+(0xdf<<2),0x2f);   // mic gain
    }
    ///unmute source (mic data)
    REG_PL_WR(0x400005D4, (REG_PL_RD(0x400005D4) & ~((uint32_t)0x00400000)));
    
}

void codec_power_off(void)
{
    REG_PL_WR(0x50022360,0x7f);// pd
    REG_PL_WR(0x50022368,0xff);
    REG_PL_WR(0x5002236c,0x3f);
}

/* configure codec working in SCO mode */
void codec_audio_reg_config(void)
{
    uint32_t count = 0;
    system_set_voice_config();
     
     /* disable audio codec first */
    *(uint32_t *)&digital_codec_reg->sys_cfg = 0x00;
    *(uint32_t *)&digital_codec_reg->sys_cfg = 0x30;
    *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0x00;
    
    *(uint32_t *)&digital_codec_reg->sample_rate = 0x00;    // 8K sample rate
    
    digital_codec_reg->adcff_cfg0.aflr = 30;
    digital_codec_reg->dacff_cfg0.aelr = 34;
    
    /* clear dac fifo first */
    digital_codec_reg->esco_mask.esco_mask = 0;
    digital_codec_reg->dacff_cfg1.apb_sel = 1;
    digital_codec_reg->dacff_cfg1.enable = 1;
    for(uint8_t i=0; i<64; i++) {
        digital_codec_reg->dacff_data = 0x00;
    }
    *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0x00;

    ool_write(0xad,0x00); // audio buck&ldo pu 

    *(uint32_t *)&digital_codec_reg->dac_cfg = 0x0a;        // mono, right channel is used
    digital_codec_reg->i2s_cfg.lrswap = 1;
    digital_codec_reg->i2s_cfg.format = 2;
    digital_codec_reg->dac_cfg1.cpy_when_mono = 0;                 // copy output data from left to right
    
    //digital_codec_reg->esco_mask.esco_mask = 0;
    digital_codec_reg->dac_cfg1.int_din_src = 0;
    *(uint32_t *)&digital_codec_reg->sys_cfg = 0x33;
    #if 1
    GLOBAL_INT_DISABLE();
    uint32_t org_diag_cfg = *(volatile uint32_t *)0x40000450;
    *(volatile uint32_t *)0x40000450 = (org_diag_cfg & 0xffff00ff) | 0x0000b200;
    uint32_t org_diag_value = *(volatile uint32_t *)0x40000454;
    org_diag_value &= 0x00000200;
    do {
        uint32_t tmp_diag_value = (*(volatile uint32_t *)0x40000454) & 0x00000200;
        if(count>120){
            break;
        }
        count++;
        co_delay_100us(1);
        if(tmp_diag_value != org_diag_value) {
            break;
        }
    } while(1);
    
    *(uint32_t *)&digital_codec_reg->adcff_cfg1 = 0xb4;
    *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0x94;
    
    ipc_config_voice_dma();
    *(volatile uint32_t *)0x40000450 = org_diag_cfg;
    GLOBAL_INT_RESTORE();
    #else
    *(uint32_t *)&digital_codec_reg->adcff_cfg1 = 0xb4;
    *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0x94;
    
    ipc_config_voice_dma();
    #endif
    //printf("codec audio config\r\n");
    //os_timer_init(&audio_codec_timer, audio_codec_timer_func,NULL);
    //os_timer_start(&audio_codec_timer, 100, 0);
#ifndef CFG_FT_CODE
    if(dsp_nrec_start_flag == true){
        os_timer_init(&audio_codec_timer, audio_codec_timer_func,NULL);
        os_timer_start(&audio_codec_timer, 100, 0);
    }
#endif
#if 0
    if(ipc_get_spk_type() == IPC_SPK_TYPE_I2S){
        //analog pd
        REG_PL_WR(0x50022360,0x3b);// adc en
        REG_PL_WR(0x50022368,0xff); //pa&mix pd
        REG_PL_WR(0x5002236c,0x20); //pga pu
        //i2s_init_imp(8000);
    }else{
        //analog pd
        REG_PL_WR(0x50022360,0x00);// adc en
        REG_PL_WR(0x50022368,0x00); //pa&mix pd
        REG_PL_WR(0x5002236c,0x20); //pga pu
        REG_PL_WR(0x50022378,0x18); // mic config, single

        //ʨ׃spk Ӵ
        REG_PL_WR(0x50022000+(0xd6<<2),pskeys.app_data.hf_vol);
        REG_PL_WR(0x50022000+(0xd7<<2),pskeys.app_data.hf_vol);
        
        REG_PL_WR(0x50022000+(0xdf<<2),0x2f);//mic gain
    }
#endif

    if(ipc_get_mic_type() == IPC_MIC_TYPE_I2S) {
        i2s_init_(I2S_DIR_TX, 2000000, 8000, I2S_MODE_MASTER);
        i2s_set_tx_src(I2S_TX_SRC_IPC_MCU);
        i2s_start_without_int();
    }
}

/* 
 * configure codec working in aac mode, aac is decoded in DSP, PCM data
 * are transfered via IPC DMA from DSP to codec
 */
void codec_aac_reg_config(uint8_t sample_rate)
{
    system_set_aac_config();

    ipc_config_media_dma();

    digital_codec_reg->sample_rate.dac_sr = sample_rate;
    ///dac fifo enable
    digital_codec_reg->dacff_cfg1.enable = 1;
    ///dac enable
    digital_codec_reg->sys_cfg.dac_en = 1;
    
#if ENABLE_MEDIA_EQ
    digital_codec_reg->sys_cfg.gpf_en = 1;              // enable gpf
    *(uint32_t *)&digital_codec_reg->dac_cfg = 0x82;    // stereo, data from gpf
#endif
    
    //digital_codec_reg->dacff_cfg1.aept_int_en = 1;
    //digital_codec_reg->dacff_cfg1.full_int_en = 1;
    digital_codec_reg->dac_cfg1.dacff_clk_inv_sel = 1;
    
    if(ipc_get_spk_type() == IPC_SPK_TYPE_CODEC){
        ool_write(0xad,0x00); // audio buck&ldo pu 
        //analog pd
        REG_PL_WR(0x50022360,0x44);// dac en
        REG_PL_WR(0x50022368,0x00); //pa&mix pu
        REG_PL_WR(0x5002236c,0x2f); //pga pd
        REG_PL_WR(0x50022000+(0xd6<<2),pskeys.app_data.a2dp_vol);
        REG_PL_WR(0x50022000+(0xd7<<2),pskeys.app_data.a2dp_vol);
    }
}

void codec_test_aac_reg_config(uint8_t sample_rate)
{
    system_set_aac_config();
    
    {
         /* disable audio codec first */
        *(uint32_t *)&digital_codec_reg->sys_cfg = 0x00;
        
        *(uint32_t *)&digital_codec_reg->sample_rate = sample_rate;    // 8K sample rate 0x00;16k sample rate 0x02;
        
        digital_codec_reg->dacff_cfg0.aelr = 32;

        ool_write(0xad,0x00); // audio buck&ldo pu 

        *(uint32_t *)&digital_codec_reg->sys_cfg = 0x22;
        *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0x94;
        
        //digital_codec_reg->dacff_cfg1.aept_int_en = 1;
        //digital_codec_reg->dacff_cfg1.full_int_en = 1;
        digital_codec_reg->dac_cfg1.dacff_clk_inv_sel = 1;

        if(sample_rate == 0xbb) {  // 44100
            system_regs->cdc_mas_clk_adj.cdc_adj_bp = 0;
        }
        else {
            system_regs->cdc_mas_clk_adj.cdc_adj_bp = 1;
        }

        if(ipc_get_spk_type() == IPC_SPK_TYPE_CODEC){
            ool_write(0xad,0x00); // audio buck&ldo pu 
            //analog pd
            REG_PL_WR(0x50022360,0x44);// dac en
            REG_PL_WR(0x50022368,0x00); //pa&mix pu
            REG_PL_WR(0x5002236c,0x2f); //pga pd
            REG_PL_WR(0x50022000+(0xd6<<2),pskeys.app_data.a2dp_vol);
            REG_PL_WR(0x50022000+(0xd7<<2),pskeys.app_data.a2dp_vol);
        }
        else if(ipc_get_spk_type() == IPC_SPK_TYPE_I2S) {
            if(sample_rate == 0xbb) {
                i2s_init_(I2S_DIR_TX, 12000000, 44100, I2S_MODE_MASTER);
            }
            else {
                i2s_init_(I2S_DIR_TX, 12000000, 48000, I2S_MODE_MASTER);
            }
            i2s_set_tx_src(I2S_TX_SRC_SBC_DECODER);
            i2s_start_without_int();
        }
    }
}

/* 
 * configure codec working in sbc mode, sbc is decoded in hardware SBC decoder,
 * PCM data are transfered via IPC DMA from decoder to codec
 */
void codec_sbc_reg_config(uint8_t sample_rate)
{
    system_set_sbc_config();
    
    ipc_config_media_dma_disable();
    digital_codec_reg->sample_rate.dac_sr = sample_rate;
    ///dac fifo enable
    digital_codec_reg->dacff_cfg1.enable = 1;
    ///dac enable
    digital_codec_reg->sys_cfg.dac_en = 1;
    
#if ENABLE_MEDIA_EQ
    digital_codec_reg->sys_cfg.gpf_en = 1;              // enable gpf
    *(uint32_t *)&digital_codec_reg->dac_cfg = 0x82;    // stereo, data from gpf
#endif
    
    //digital_codec_reg->dacff_cfg1.aept_int_en = 1;
    //digital_codec_reg->dacff_cfg1.full_int_en = 1;
    digital_codec_reg->dac_cfg1.dacff_clk_inv_sel = 1;

    if(sample_rate == 0x0b) {  // 44100
        system_regs->cdc_mas_clk_adj.cdc_adj_bp = 0;
    }
    else {
        system_regs->cdc_mas_clk_adj.cdc_adj_bp = 1;
    }

    if(ipc_get_spk_type() == IPC_SPK_TYPE_CODEC){
        ool_write(0xad,0x00); // audio buck&ldo pu 
        //analog pd
        REG_PL_WR(0x50022360,0x44);// dac en
        REG_PL_WR(0x50022368,0x00); //pa&mix pu
        REG_PL_WR(0x5002236c,0x2f); //pga pd
        REG_PL_WR(0x50022000+(0xd6<<2),pskeys.app_data.a2dp_vol);
        REG_PL_WR(0x50022000+(0xd7<<2),pskeys.app_data.a2dp_vol);
    }
    else if(ipc_get_spk_type() == IPC_SPK_TYPE_I2S) {
        if(sample_rate == 0x0b) {
            i2s_init_(I2S_DIR_TX, 12000000, 44100, I2S_MODE_MASTER);
        }
        else {
            i2s_init_(I2S_DIR_TX, 12000000, 48000, I2S_MODE_MASTER);
        }
        i2s_set_tx_src(I2S_TX_SRC_SBC_DECODER);
        i2s_start_without_int();
    }
}

/* configure codec working in tone mode, the sample rate is 16K */
uint8_t codec_tone_reg_config(uint8_t tone_index)
{
    uint8_t ret = 0;
    if(tone_index == 0){
        ///һҥ؅ߪܺӴ
        ret = 1;
    }else{
        system_set_sbc_config();
        ipc_config_media_dma_disable();
        digital_codec_reg->sample_rate.dac_sr = 0x02; //16k
        ///dac fifo enable
        digital_codec_reg->dacff_cfg1.enable = 1;
        ///dac enable
        digital_codec_reg->sys_cfg.dac_en = 1;
    
        //digital_codec_reg->dacff_cfg1.aept_int_en = 1;
        //digital_codec_reg->dacff_cfg1.full_int_en = 1;
        digital_codec_reg->dac_cfg1.dacff_clk_inv_sel = 1;
        
        if(ipc_get_spk_type() == IPC_SPK_TYPE_CODEC){
            ool_write(0xad,0x00); // audio buck&ldo pu 
            
            //analog pd
            REG_PL_WR(0x50022360,0x44);// dac en
            REG_PL_WR(0x50022368,0x00); //pa&mix pu
            REG_PL_WR(0x5002236c,0x2f); //pga pd
            
            REG_PL_WR(0x50022000+(0xd6<<2),pskeys.app_data.tone_vol);
            REG_PL_WR(0x50022000+(0xd7<<2),pskeys.app_data.tone_vol);
        }
    }
    return ret;
}

void codec_dac_reg_config(uint8_t sample_rate, enum audio_codec_dac_src_t src)
{
    system_set_aac_config();

     /* disable audio codec first */
    *(uint32_t *)&digital_codec_reg->sys_cfg = 0x00;
    /* configure sample rate */
    *(uint32_t *)&digital_codec_reg->sample_rate = sample_rate;
    /* configure fifo almost empty threshold, fifo depth is 64 samples */
    digital_codec_reg->dacff_cfg0.aelr = 32;

    /* audio buck&ldo power on */
    ool_write(0xad,0x00);

    /* enable dac and release dac reset */
    *(uint32_t *)&digital_codec_reg->sys_cfg = 0x22;
    if(src == AUDIO_CODEC_DAC_SRC_IPC_DMA) {
        /* enable dac fifo and empty interrupt, almost empty interrupt */
        *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0x94;
    }
    else {
        /* enable dac fifo */
        *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0x80;
    }
    
    digital_codec_reg->dac_cfg1.dacff_clk_inv_sel = 1;

    if((sample_rate & AUDIO_CODEC_SAMPLE_RATE_DAC_MASK) == AUDIO_CODEC_SAMPLE_RATE_DAC_44100) {  // 44100
        system_regs->cdc_mas_clk_adj.cdc_adj_bp = 0;
    }
    else {
        system_regs->cdc_mas_clk_adj.cdc_adj_bp = 1;
    }

    /* analog power configuration */
    REG_PL_WR(0x50022360,0x44); // remove dac pd
    REG_PL_WR(0x50022368,0x00); // remove pa&mix pd
    REG_PL_WR(0x5002236c,0x2f); // pga pd
    REG_PL_WR(0x50022000+(0xd6<<2),pskeys.app_data.a2dp_vol);
    REG_PL_WR(0x50022000+(0xd7<<2),pskeys.app_data.a2dp_vol);
}

void codec_adc_reg_config(uint8_t sample_rate, enum audio_codec_adc_dest_t dest, uint8_t mic_gain)
{
    /* disable audio codec first */
    *(uint32_t *)&digital_codec_reg->sys_cfg = 0x00;
    /* configure sample rate */
    *(uint32_t *)&digital_codec_reg->sample_rate = sample_rate;
    /* configure fifo almost full threshold, fifo depth is 64 samples */
    digital_codec_reg->adcff_cfg0.aflr = 32;

    /* audio buck&ldo power on */
    ool_write(0xad,0x00);

    digital_codec_reg->esco_mask.esco_mask = 1;
    /* enable dac and release dac reset */
    *(uint32_t *)&digital_codec_reg->sys_cfg = 0x11;
    if(dest == AUDIO_CODEC_ADC_DEST_IPC_DMA) {
        /* enable adc fifo and full interrupt, almost full interrupt */
        *(uint32_t *)&digital_codec_reg->adcff_cfg1 = 0x85;
    }
    else if(dest == AUDIO_CODEC_ADC_DEST_USER){
    /* enable adc fifo and full interrupt, almost full interrupt,enable apb option*/
    *(uint32_t *)&digital_codec_reg->adcff_cfg1 = 0xc5;

    }
    else {
        /* enable adc fifo */
        *(uint32_t *)&digital_codec_reg->adcff_cfg1 = 0x80;
    }

    if((sample_rate & AUDIO_CODEC_SAMPLE_RATE_ADC_MASK) == AUDIO_CODEC_SAMPLE_RATE_ADC_44100) {  // 44100
        system_regs->cdc_mas_clk_adj.cdc_adj_bp = 0;
    }
    else {
        system_regs->cdc_mas_clk_adj.cdc_adj_bp = 1;
    }

    /* analog power configuration */
    REG_PL_WR(0x50022360,0x00);// adc en
    REG_PL_WR(0x50022368,0x00); //pa&mix pd
    REG_PL_WR(0x5002236c,0x20); //pga pu
    REG_PL_WR(0x50022378,0x18); // mic config, single
    REG_PL_WR(0x50022000+(0xdf<<2), mic_gain);//mic gain    0x00~0x3F
}

void codec_dac_adc_reg_config(uint8_t sample_rate, enum audio_codec_dac_src_t src, enum audio_codec_adc_dest_t dest, uint8_t mic_gain)
{
    system_set_voice_config();

    /* disable audio codec first */
    *(uint32_t *)&digital_codec_reg->sys_cfg = 0x00;
    
    /* configure sample rate */
    *(uint32_t *)&digital_codec_reg->sample_rate = sample_rate;

    /* configure fifo almost empty threshold, fifo depth is 64 samples */
    digital_codec_reg->dacff_cfg0.aelr = 34;
    /* configure fifo almost full threshold, fifo depth is 64 samples */
    digital_codec_reg->adcff_cfg0.aflr = 30;

    /* audio buck&ldo power on */
    ool_write(0xad,0x00);
    
    digital_codec_reg->esco_mask.esco_mask = 1;
    
    if(dest == AUDIO_CODEC_ADC_DEST_IPC_DMA) {
        /* enable adc fifo and full interrupt, almost full interrupt */
        *(uint32_t *)&digital_codec_reg->adcff_cfg1 = 0x85;
    }
    else if(dest == AUDIO_CODEC_ADC_DEST_USER){
    /* enable adc fifo and full interrupt, almost full interrupt,enable apb option*/
    *(uint32_t *)&digital_codec_reg->adcff_cfg1 = 0xc5;
    }
    else {
        /* enable adc fifo */
        *(uint32_t *)&digital_codec_reg->adcff_cfg1 = 0x80;
    }
    
    if(src == AUDIO_CODEC_DAC_SRC_IPC_DMA) {
        /* enable dac fifo and empty interrupt, almost empty interrupt */
        *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0x94;
        digital_codec_reg->esco_mask.esco_mask = 0;
    }
    else if(src == AUDIO_CODEC_DAC_SRC_USER){
        /* enable dac fifo and apb function*/
        *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0xc0;
    }
    else {
        /* enable dac fifo */
        *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0x80;
    }
    
    *(uint32_t *)&digital_codec_reg->dac_cfg = 0x1a;        // mono, left channel is used
    //digital_codec_reg->i2s_cfg.lrswap = 1;

    digital_codec_reg->dac_cfg1.dacff_clk_inv_sel = 1;

    if((sample_rate & AUDIO_CODEC_SAMPLE_RATE_ADC_MASK) == AUDIO_CODEC_SAMPLE_RATE_ADC_44100) {  // 44100
        system_regs->cdc_mas_clk_adj.cdc_adj_bp = 0;
    }
    else {
        system_regs->cdc_mas_clk_adj.cdc_adj_bp = 1;
    }
        
    /* enable adc, dac and release adc, dac reset */
    *(uint32_t *)&digital_codec_reg->sys_cfg = 0x33;
    
    //analog pd
    REG_PL_WR(0x50022360,0x00);// dac en
    REG_PL_WR(0x50022368,0x00); //pa&mix pu
    REG_PL_WR(0x5002236c,0x20); //pga pd
    REG_PL_WR(0x50022000+(0xd6<<2),pskeys.app_data.a2dp_vol);
    REG_PL_WR(0x50022000+(0xd7<<2),pskeys.app_data.a2dp_vol);

    REG_PL_WR(0x50022378,0x18); // mic config, single
    REG_PL_WR(0x50022000+(0xdf<<2), mic_gain);//mic gain    0x00~0x3F
}

/* used to power down codec to save power */
void codec_off_reg_config(void)
{
    ipc_config_reset_dma();
    codec_power_off();
    
    if(ipc_get_spk_type() == IPC_SPK_TYPE_I2S){
        REG_PL_WR(I2S_REG_CTRL,0x00);
    }
    ool_write(0xad,0x09); // audio buck&ldo pd 
    
    if(ipc_get_spk_type() == IPC_SPK_TYPE_I2S) {
        i2s_stop_();
    }
}


void codec_mic_loop_reg_config(void)
{
    system_set_voice_config();
    
    /* clear dac fifo first */
    digital_codec_reg->dacff_cfg1.apb_sel = 1;
    digital_codec_reg->dacff_cfg1.enable = 1;
    for(uint8_t i=0; i<64; i++) {
        digital_codec_reg->dacff_data = 0x00;
    }
    *(uint32_t *)&digital_codec_reg->dacff_cfg1 = 0x00;
    #if DSP_MIC_LOOP
    codec_dac_adc_reg_config(AUDIO_CODEC_SAMPLE_RATE_ADC_8000|AUDIO_CODEC_SAMPLE_RATE_DAC_8000, AUDIO_CODEC_DAC_SRC_IPC_DMA,AUDIO_CODEC_ADC_DEST_IPC_DMA,0x3b);
    ipc_config_mic_loop_dma();
    #else
    codec_dac_adc_reg_config(AUDIO_CODEC_SAMPLE_RATE_ADC_16000|AUDIO_CODEC_SAMPLE_RATE_DAC_16000,AUDIO_CODEC_DAC_SRC_USER,AUDIO_CODEC_ADC_DEST_USER, 0x3b);
    #endif
}

void codec_mic_only_reg_config(void)
{
    system_set_voice_config();
    
    codec_adc_reg_config(AUDIO_CODEC_SAMPLE_RATE_ADC_16000,AUDIO_CODEC_ADC_DEST_USER, 0x3b);
}

void codec_native_playback_reg_config(void)
{
    system_set_aac_config();

    codec_dac_reg_config(AUDIO_CODEC_SAMPLE_RATE_DAC_44100, AUDIO_CODEC_DAC_SRC_IPC_DMA);
    ipc_config_media_dma();
}

/* used to do codec initialziation */
__attribute__((section("ram_code"))) void codec_init_reg_config(void)
{
    REG_PL_WR(0x50022340,0x08);
    REG_PL_WR(0x50022344,0x05);
    REG_PL_WR(0x50022348,0x23);
    REG_PL_WR(0x5002234c,0x00);
    REG_PL_WR(0x50022350,0x3c);
    REG_PL_WR(0x50022354,0xd1);
    
    REG_PL_WR(0x50022358,0x0f);
    REG_PL_WR(0x5002235c,0x0f);
    
    REG_PL_WR(0x50022360,0x7f);// pd
    
    REG_PL_WR(0x50022364,0x00);
    
    REG_PL_WR(0x50022368,0xff);
    REG_PL_WR(0x5002236c,0x3f);

    REG_PL_WR(0x50022370,0x55);
    REG_PL_WR(0x50022374,0x07);
    REG_PL_WR(0x50022378,0x08);
    REG_PL_WR(0x5002237c,0x17);
    REG_PL_WR(0x50022380,0x00);
    REG_PL_WR(0x50022384,0x00);
    REG_PL_WR(0x50022388,0x00);
    REG_PL_WR(0x5002238c,0x45);
    REG_PL_WR(0x50022390,0xba);
    
#if ENABLE_MEDIA_EQ
    REG_PL_WR(0x50022134, 0x00003f68);
    REG_PL_WR(0x50022138, 0x0001812f);
    REG_PL_WR(0x5002213c, 0x00003f68);
    REG_PL_WR(0x50022140, 0x00007ecc);
    REG_PL_WR(0x50022144, 0x0001c12c);
    REG_PL_WR(0x50022148, 0x00003e99);
    REG_PL_WR(0x5002214c, 0x000182cd);
    REG_PL_WR(0x50022150, 0x00003e99);
    REG_PL_WR(0x50022154, 0x00007d2f);
    REG_PL_WR(0x50022158, 0x0001c2c9);
    REG_PL_WR(0x5002215c, 0x00002845);
    REG_PL_WR(0x50022160, 0x00002845);
    REG_PL_WR(0x50022164, 0x00000000);
    REG_PL_WR(0x50022168, 0x0001ef75);
    REG_PL_WR(0x5002216c, 0x00000000);
    REG_PL_WR(0x50022170, 0x00003d00);
    REG_PL_WR(0x50022174, 0x000189e0);
    REG_PL_WR(0x50022178, 0x0000393e);
    REG_PL_WR(0x5002217c, 0x000075cf);
    REG_PL_WR(0x50022180, 0x0001c971);
    REG_PL_WR(0x50022184, 0x00002b86);
    REG_PL_WR(0x50022188, 0x0001e8f7);
    REG_PL_WR(0x5002218c, 0x000009a4);
    REG_PL_WR(0x50022190, 0x000034af);
    REG_PL_WR(0x50022194, 0x0001ed2f);
    REG_PL_WR(0x50022198, 0x00004000);
    REG_PL_WR(0x5002219c, 0x00000000);
    REG_PL_WR(0x500221a0, 0x00000000);
    REG_PL_WR(0x500221a4, 0x00000000);
    REG_PL_WR(0x500221a8, 0x00000000);
    REG_PL_WR(0x500221ac, 0x00004000);
    REG_PL_WR(0x500221b0, 0x00000000);
    REG_PL_WR(0x500221b4, 0x00000000);
    REG_PL_WR(0x500221b8, 0x00000000);
    REG_PL_WR(0x500221bc, 0x00000000);
    REG_PL_WR(0x500221c0, 0x0000ca62);
    REG_PL_WR(0x500221c4, 0x00000000);
    REG_PL_WR(0x500221c8, 0x00000000);
    REG_PL_WR(0x500221cc, 0x00000000);
    REG_PL_WR(0x500221d0, 0x00000000);
    REG_PL_WR(0x500221d4, 0x00004000);
    REG_PL_WR(0x500221d8, 0x00000000);
    REG_PL_WR(0x500221dc, 0x00000000);
    REG_PL_WR(0x500221e0, 0x00000000);
    REG_PL_WR(0x500221e4, 0x00000000);
    REG_PL_WR(0x500221e8, 0x00004000);
    REG_PL_WR(0x500221ec, 0x00000000);
    REG_PL_WR(0x500221f0, 0x00000000);
    REG_PL_WR(0x500221f4, 0x00000000);
    REG_PL_WR(0x500221f8, 0x00000000);
    REG_PL_WR(0x500221fc, 0x00004000);
    REG_PL_WR(0x50022200, 0x00000000);
    REG_PL_WR(0x50022204, 0x00000000);
    REG_PL_WR(0x50022208, 0x00000000);
    REG_PL_WR(0x5002220c, 0x00000000);
    REG_PL_WR(0x50022210, 0x00004000);
    REG_PL_WR(0x50022214, 0x00000000);
    REG_PL_WR(0x50022218, 0x00000000);
    REG_PL_WR(0x5002221c, 0x00000000);
    REG_PL_WR(0x50022220, 0x00000000);
    REG_PL_WR(0x50022224, 0x00003f68);
    REG_PL_WR(0x50022228, 0x0001812f);
    REG_PL_WR(0x5002222c, 0x00003f68);
    REG_PL_WR(0x50022230, 0x00007ecc);
    REG_PL_WR(0x50022234, 0x0001c12c);
    REG_PL_WR(0x50022238, 0x00003e99);
    REG_PL_WR(0x5002223c, 0x000182cd);
    REG_PL_WR(0x50022240, 0x00003e99);
    REG_PL_WR(0x50022244, 0x00007d2f);
    REG_PL_WR(0x50022248, 0x0001c2c9);
    REG_PL_WR(0x5002224c, 0x00002845);
    REG_PL_WR(0x50022250, 0x00002845);
    REG_PL_WR(0x50022254, 0x00000000);
    REG_PL_WR(0x50022258, 0x0001ef75);
    REG_PL_WR(0x5002225c, 0x00000000);
    REG_PL_WR(0x50022260, 0x00003d00);
    REG_PL_WR(0x50022264, 0x000189e0);
    REG_PL_WR(0x50022268, 0x0000393e);
    REG_PL_WR(0x5002226c, 0x000075cf);
    REG_PL_WR(0x50022270, 0x0001c971);
    REG_PL_WR(0x50022274, 0x00002b86);
    REG_PL_WR(0x50022278, 0x0001e8f7);
    REG_PL_WR(0x5002227c, 0x000009a4);
    REG_PL_WR(0x50022280, 0x000034af);
    REG_PL_WR(0x50022284, 0x0001ed2f);
    REG_PL_WR(0x50022288, 0x00004000);
    REG_PL_WR(0x5002228c, 0x00000000);
    REG_PL_WR(0x50022290, 0x00000000);
    REG_PL_WR(0x50022294, 0x00000000);
    REG_PL_WR(0x50022298, 0x00000000);
    REG_PL_WR(0x5002229c, 0x00004000);
    REG_PL_WR(0x500222a0, 0x00000000);
    REG_PL_WR(0x500222a4, 0x00000000);
    REG_PL_WR(0x500222a8, 0x00000000);
    REG_PL_WR(0x500222ac, 0x00000000);
    REG_PL_WR(0x500222b0, 0x0000ca62);
    REG_PL_WR(0x500222b4, 0x00000000);
    REG_PL_WR(0x500222b8, 0x00000000);
    REG_PL_WR(0x500222bc, 0x00000000);
    REG_PL_WR(0x500222c0, 0x00000000);
    REG_PL_WR(0x500222c4, 0x00004000);
    REG_PL_WR(0x500222c8, 0x00000000);
    REG_PL_WR(0x500222cc, 0x00000000);
    REG_PL_WR(0x500222d0, 0x00000000);
    REG_PL_WR(0x500222d4, 0x00000000);
    REG_PL_WR(0x500222d8, 0x00004000);
    REG_PL_WR(0x500222dc, 0x00000000);
    REG_PL_WR(0x500222e0, 0x00000000);
    REG_PL_WR(0x500222e4, 0x00000000);
    REG_PL_WR(0x500222e8, 0x00000000);
    REG_PL_WR(0x500222ec, 0x00004000);
    REG_PL_WR(0x500222f0, 0x00000000);
    REG_PL_WR(0x500222f4, 0x00000000);
    REG_PL_WR(0x500222f8, 0x00000000);
    REG_PL_WR(0x500222fc, 0x00000000);
    REG_PL_WR(0x50022300, 0x00004000);
    REG_PL_WR(0x50022304, 0x00000000);
    REG_PL_WR(0x50022308, 0x00000000);
    REG_PL_WR(0x5002230c, 0x00000000);
    REG_PL_WR(0x50022310, 0x00000000);
    REG_PL_WR(0x50022318, 0x00000001);
    REG_PL_WR(0x50022080, 0x0000003f);
    REG_PL_WR(0x50022084, 0x0000003f);
    REG_PL_WR(0x50022090, 0x00000002);
    REG_PL_WR(0x500220a8, 0x00000003);
    REG_PL_WR(0x500220e4, 0x00000010);
    REG_PL_WR(0x500220e8, 0x00000016);
    REG_PL_WR(0x50022314, 0x00000000);
#endif

    system_regs->cdc_mas_clk_adj.cdc_adj_bp = 1;
}

//uint16_t pcm_sin_wave_1k[]={0x5175,0x0000,0xae8a,0x8ccc,0xae8a,0xffff,0x5175,0x7333};
void cdc_isr_imp(void)
{
    if(digital_codec_reg->adcff_cfg1.afull_status)
    {
        for(uint8_t i=0; i<32; i++)
        {
            uint16_t tmp = digital_codec_reg->adcff_data;
            digital_codec_reg->dacff_data = tmp;
        }
    }
}

