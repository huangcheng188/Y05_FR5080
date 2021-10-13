/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "co_log.h"

#include "gap_api.h"
#include "gatt_api.h"
#include "bt_api.h"
#include "hf_api.h"
#include "a2dp_api.h"
#include "avrcp_api.h"
#include "spp_api.h"
#include "pbap_api.h"
#include "ota_service.h"

#include "os_timer.h"
#include "os_mem.h"
#include "os_msg_q.h"
#include "os_task.h"
#include "user_utils.h"
#include "ipc_load_code.h"
#include "dsp_program.h"

#include "driver_uart.h"
#include "driver_flash.h"
#include "driver_syscntl.h"
#include "driver_pmu.h"
#include "driver_button.h"
#include "driver_ipc.h"
#include "driver_gpio.h"
#include "driver_triming.h"
#include "driver_codec.h"
#include "driver_sdc.h"
#include "driver_pwm.h"

#include "app_user_bt.h"
#include "user_task.h"
#include "user_bt.h"
#include "user_fs.h"
#include "user_dsp.h"
#include "app_at.h"
#include "ble_simple_peripheral.h"
#include "audio_source.h"
#include "mass_mal.h"
#include "usbdev.h"
#include "ff.h"
#include "native_playback.h"
#include "memory.h"

__attribute__((section("compile_date_sec")))const uint8 compile_date[]  = __DATE__;
__attribute__((section("compile_time_sec")))const uint8 compile_time[]  = __TIME__;

#if !COPROCESSOR_UART_ENABLE
__attribute__((section("ram_code"))) void pmu_gpio_isr_ram(void)
{
    uint32_t gpio_value = ool_read32(PMU_REG_PORTA_INPUT_VAL);
    //printf("gpio = %x\r\n",gpio_value);
    button_toggle_detected(gpio_value);
   
    ool_write32(PMU_REG_STATE_MON_IN_A,gpio_value);
}
#endif

//extern int ff_test_main5 (void);
uint8_t tog_flag = 0;
extern     os_timer_t bt_reconnect_timer;
extern os_timer_t bt_delay_reconnect_timer;

os_timer_t onkey_timer;
os_timer_t bt_test_reconnect_timer;

void bt_test_reconnect_timer_func(void *arg)
{
    uart_putc_noint('T');
    
    os_timer_start(&bt_test_reconnect_timer,1000,false);
}

void onkey_timer_func(void *arg)
{
    if(REG_PL_RD(0x50000078)&BIT12){
        printf("onkey pressed\r\n");
    }else{
        printf("onkey released\r\n");
        pmu_clear_isr_state(PMU_INT_ONKEY);
        ool_write(PMU_REG_INT_EN_1, ((ool_read(PMU_REG_INT_EN_1) | ((PMU_INT_ONKEY)))));
    }
}

void proj_button_evt_func(struct button_msg_t *msg)
{
    enum bt_state_t bt_state;
    extern struct user_bt_env_tag *user_bt_env;
    
    bt_state = bt_get_state();
    
    LOG_INFO("index = %x,type = %x, cnt = %x\r\n",msg->button_index,msg->button_type,msg->button_cnt);
    if(msg->button_index == BUTTON_FN12){
        if(msg->button_type == BUTTON_SHORT_PRESSED){
            if(bt_state == BT_STATE_HFP_INCOMMING){
                hf_answer_call(user_bt_env->dev[0].hf_chan);
            }else if(bt_state == BT_STATE_HFP_CALLACTIVE){
                hf_hang_up(user_bt_env->dev[0].hf_chan);
            }else if(bt_state == BT_STATE_CONNECTED){
                //printf("%x,%x\r\n",user_bt_env->dev[0].rcp_chan,user_bt_env->dev[1].rcp_chan);
                avrcp_set_panel_key(user_bt_env->dev[0].rcp_chan, AVRCP_POP_PLAY,TRUE);
            }else if(bt_state == BT_STATE_MEDIA_PLAYING){
                avrcp_set_panel_key(user_bt_env->dev[0].rcp_chan, AVRCP_POP_PAUSE,TRUE);
            }
        }else if(msg->button_type == BUTTON_LONG_PRESSED){
            if((bt_state == BT_STATE_HFP_INCOMMING)||(BT_STATE_HFP_OUTGOING)){
                hf_hang_up(user_bt_env->dev[0].hf_chan);
            }
        }else if(msg->button_type == BUTTON_MULTI_PRESSED){
            if(bt_state == BT_STATE_CONNECTED){
                hf_redial(user_bt_env->dev[0].hf_chan);
            }
        }
    }else if(msg->button_index == BUTTON_FN13){
        if(msg->button_type == BUTTON_SHORT_PRESSED){
            #if 0
            if(bt_state <= BT_STATE_STANDBY){
                bt_set_a2dp_type(!bt_get_a2dp_type());
            }
            #else
            //os_malloc(4000);
            if(tog_flag == 1){
                //printf("power off\r\n");
                tog_flag = 0;
                bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE,NULL);
                os_timer_stop(&bt_delay_reconnect_timer);
                os_timer_stop(&bt_reconnect_timer);
                bt_disconnect();
            }else{
                
                //printf("power on\r\n");
                tog_flag = 1;
                //bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
                bt_reconnect(RECONNECT_TYPE_USER_CMD,NULL);
            }
        
            #endif
        }else if(msg->button_type == BUTTON_LONG_PRESSED){
            #if 0
            if(bt_state >= BT_STATE_CONNECTED){
                bt_disconnect();
            }
            #else
            //REG_PL_RD(0x60000000);

            #endif
        }
    }
#if COPROCESSOR_UART_ENABLE
    else if(msg->button_index == BUTTON_FN20){
        if(msg->button_type == BUTTON_PRESSED){
            //PC4
            //uart_slave_send_wake_ind();
        }
    }
#endif
}

/*********************************************************************
 * @fn      user_custom_parameters
 *
 * @brief   initialize several parameters, this function will be called 
 *          at the beginning of the program. Note that the parameters 
 *          changed here will overlap changes in uart tools 
 *
 * @param   None. 
 *       
 *
 * @return  None.
 */
void user_custom_parameters(void)
{
    uint8_t i = 0;
    volatile struct uart_reg_t *uart_reg = (volatile struct uart_reg_t *)UART_BASE;
    uint8_t temp_div;
    
    retry_handshake();
    system_set_port_pull_up(GPIO_PB6, true);
    uart_reg->lcr.divisor_access = 1;
    temp_div =  uart_reg->u1.dll.data;
    uart_reg->lcr.divisor_access = 0;
    system_regs->reset_ctrl.cm3_uart_sft_rst = 1;
    
    if(temp_div == 1){
        uart_init(BAUD_RATE_921600);
    }else{
        uart_init(BAUD_RATE_115200);
    }
    dsp_program();
        
    LOG_INFO("\r\n Complile Date: ");
    for(i = 0; i < 11; i++){
        LOG_INFO("%c",compile_date[i]);
    }
    LOG_INFO("\r\n");
    LOG_INFO("Ccomplile Time: ");
    for(i = 0; i < 8; i++){
        LOG_INFO("%c",compile_time[i]);
    }
    LOG_INFO("\r\n");
    //printf("mem = %x,%x,%x,%x\r\n",REG_PL_RD(0x40010000),REG_PL_RD(0x40010004),REG_PL_RD(0x40010008),REG_PL_RD(0x4001000c));
#if 0
    /* these parameters can be set for constum application  */
    pskeys.local_bdaddr.addr[0] = 0x09;
    pskeys.local_bdaddr.addr[1] = 0x0a;
    pskeys.local_bdaddr.addr[2] = 0x03;
    pskeys.local_bdaddr.addr[3] = 0x0a;
    pskeys.local_bdaddr.addr[4] = 0x05;
    pskeys.local_bdaddr.addr[5] = 0x08;

    memcpy(pskeys.localname,"FR5080_D",strlen("FR5080_D")+1);

    pskeys.hf_vol = 0x20;
    pskeys.a2dp_vol = 0x20;
    pskeys.hf_vol = 0x20;

    pskeys.enable_profiles = ENABLE_PROFILE_HF|ENABLE_PROFILE_A2DP|ENABLE_PROFILE_SPP;  //enable hfp,a2dp profile
#endif

#if BLE_TEST_MODE
    ///for ble test
    pskeys.system_options = 0xe2;
    pskeys.tws_mode = 2;
#endif

}

__attribute__((section("ram_code"))) void user_entry_before_sleep_imp(void)
{
    uart_putc_noint_no_wait('s');
    
    //pmu_set_gpio_value(GPIO_PORT_B, 1<<GPIO_BIT_0, 0);
}

__attribute__((section("ram_code"))) void user_entry_after_sleep_imp(void)
{
    /* IO mux have to be reassigned afte wake up from sleep mode */
    app_at_init_app();
    #if 0
    //system_set_dsp_clk(SYSTEM_DSP_SRC_OSC_48M, 0);
    system_set_port_pull_up(GPIO_PC6, true);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_6, PORTC6_FUNC_DSP_UART_RXD);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_7, PORTC7_FUNC_DSP_UART_TXD);
    
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_0, PORTC0_FUNC_PDM0_SCK);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_1, PORTC1_FUNC_PDM0_SDA);
    //pmu_set_gpio_value(GPIO_PORT_B, 1<<GPIO_BIT_0, 1);
    #endif
    uart_putc_noint_no_wait('w');
#if DSP_USE_XIP
#if FR5086D_V1_0
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_0, PORTA0_FUNC_QSPI1_SCLK);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_1, PORTA1_FUNC_QSPI1_CS);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_2, PORTA2_FUNC_QSPI1_MISO0);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_3, PORTA3_FUNC_QSPI1_MISO1);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_4, PORTA4_FUNC_QSPI1_MISO2);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_5, PORTA5_FUNC_QSPI1_MISO3);
#endif
#endif

#if SD_DEVICE_ENABLE == 1
        system_set_port_pull_down(GPIO_PC0|GPIO_PC1|GPIO_PC2, false);
        system_set_port_pull_up(GPIO_PC0|GPIO_PC1|GPIO_PC2, true);
        system_set_port_mux(GPIO_PORT_C, GPIO_BIT_0, PORTC0_FUNC_SDC_CLK);
        system_set_port_mux(GPIO_PORT_C, GPIO_BIT_1, PORTC1_FUNC_SDC_CMD);
        system_set_port_mux(GPIO_PORT_C, GPIO_BIT_2, PORTC2_FUNC_SCD_DAT0);
        system_regs->reset_ctrl.sdc_sft_rst = 1;
        system_regs->sdc_ctrl.sdc_clk_div = 0;
        //MAL_Init(MASS_LUN_IDX_SD_NAND);
        
#endif  // SD_DEVICE_ENABLE == 1

}

void user_entry_before_stack_init(void)
{    
    //*(uint8_t *)(0x2000014e) = 0x73;//bit3 --- ssp enable
    /* initalize uart for AT command usage */
    app_at_init_app();

    LOG_INFO("user_entry_before_stack_init\r\n");
    
    /* register callback functions for BT mode */
    bt_me_set_cb_func(bt_me_evt_func);
    bt_hf_set_cb_func(bt_hf_evt_func);
    bt_a2dp_set_cb_func(bt_a2dp_evt_func);
    bt_avrcp_set_cb_func(bt_avrcp_evt_func);
    bt_spp_set_cb_func(bt_spp_evt_func);
    bt_pbap_client_set_cb_func(PbaClientCallback);
    triming_init();
    pmu_set_ioldo_voltage(PMU_ALDO_VOL_3_3);

} 

void user_entry_after_stack_init(void)
{ 
    LOG_INFO("user_entry_after_stack_init\r\n");
    //local drift set t0 200
    //*(uint32_t *)0x200008a8 = 0x000100c8;

    /* create a user task */
    user_task_init();
    
#if (!BLE_TEST_MODE) & (!FIXED_FREQ_TX_TEST)&(!BT_TEST_MODE)
    /* BLE GAP initialization */
    simple_peripheral_init();

    /* register OTA service */
    ota_gatt_add_service();
#endif


    /* intialize variables used in audio source mode */
    audio_source_init();
    /* intialize variables used in native playback mode */
    native_playback_init();
    
#if MP3_SRC_LOCAL == 0
    /* 
     * In MCU+FR508x application, MCU send raw MP3 data to FR508x for local
     * playback or a2dp source. This function is used to initial variables used
     * when system working in this mode.
     */
    app_bt_audio_play_init();
#endif

    /* disable system enter deep sleep mode for debug */
    system_sleep_disable();

    #if BLE_TEST_MODE|FIXED_FREQ_TX_TEST|BT_TEST_MODE
    uart_init(BAUD_RATE_115200);
    #endif
    
    /* try to reconnect last connected BT device */
    ///reconnect only in sink mode
    if(pskeys.app_data.misc_flag == 0){
        //if(bt_reconnect(RECONNECT_TYPE_POWER_ON,NULL) == false){
            //bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
        //}
    }

#if 1
    /* configure audio input source from analog MIC */
    ipc_set_audio_inout_type(IPC_MIC_TYPE_ANLOG_MIC, IPC_SPK_TYPE_CODEC, IPC_MEDIA_TYPE_BT);
#else
    /* configure audio input source from PDM */
    ipc_set_audio_inout_type(IPC_MIC_TYPE_PDM,IPC_SPK_TYPE_CODEC,IPC_MEDIA_TYPE_BT);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_0, PORTC0_FUNC_PDM0_SCK);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_1, PORTC1_FUNC_PDM0_SDA);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_2, PORTC2_FUNC_PDM1_SCK);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_3, PORTC3_FUNC_PDM1_SDA);
#endif

    /* 
     * in MCU+FR508x application, PDM is connected both to MCU and FR508x, this pin is used to 
     * notify MCU release PDM when SCO is connect between FR508x and phone.
     */
#if 1
    pmu_set_pin_to_PMU(GPIO_PORT_B, 1<<GPIO_BIT_0);
    //pmu_set_pin_pull_up(GPIO_PORT_B,1<<GPIO_BIT_0,true);
    pmu_set_pin_dir(GPIO_PORT_B, 1<<GPIO_BIT_0,GPIO_DIR_OUT);
    pmu_set_gpio_value(GPIO_PORT_B, 1<<GPIO_BIT_0, 0);
#else
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_0, PORTB0_FUNC_B0);
    gpio_set_dir(GPIO_PORT_B, GPIO_BIT_0, GPIO_DIR_OUT);
    gpio_set_pin_value(GPIO_PORT_B, GPIO_BIT_0,0);
#endif

    system_set_pclk(SYSTEM_SYS_CLK_48M);

#if 0
    //PB1 for bt sleep led control
    pmu_set_pin_to_PMU(GPIO_PORT_B, 1<<GPIO_BIT_1);
    pmu_set_pin_pull_up(GPIO_PORT_B,1<<GPIO_BIT_1,true);
    pmu_set_pin_dir(GPIO_PORT_B, 1<<GPIO_BIT_1,GPIO_DIR_OUT);
    pmu_set_gpio_value(GPIO_PORT_B, 1<<GPIO_BIT_1, 0);
#endif

#if 0
    /* 
     * button module demonstration
     * driver_button is a software module to generate different button event, @ref button_type_t
     */
    /* set PORTB4 and PORTB5  used in button module */
    button_init(BUTTON_FN12|BUTTON_FN13);
    
    /* 
     * enable monitoring PORTB4 and PORTB5 state by PMU. once the level of PORTB4 and PORTB5 is changed, 
     * an interrupt named pmu_gpio_isr_ram is generated.
     */
    pmu_set_pin_pull_up(GPIO_PORT_B,1<<GPIO_BIT_4,true);
    pmu_set_pin_pull_up(GPIO_PORT_B,1<<GPIO_BIT_5,true);
    pmu_port_wakeup_func_set(BUTTON_FN12|BUTTON_FN13);
    
    /* set a callback to receive button event */
    bt_button_set_cb_func(proj_button_evt_func);
    
#endif

#if COPROCESSOR_UART_ENABLE

    //bt_button_set_cb_func(proj_button_evt_func);
    uart_slave_init();

#endif

#if USB_DEVICE_ENABLE == 1
    system_set_port_pull_up(GPIO_PA6, true);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_6, PORTA6_FUNC_CM3_UART_RXD);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_7, PORTA7_FUNC_CM3_UART_TXD);

    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_6, PORTB6_FUNC_USB_DP);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_7, PORTB7_FUNC_USB_DM);
    system_set_port_pull_down(GPIO_PB6 | GPIO_PB7, false);
    system_set_port_pull_up(GPIO_PB6, true);
    system_set_port_pull_up(GPIO_PB7, false);
    
    usbdev_init();

    Memory_init();
#endif  // USB_DEVICE_ENABLE == 1

#if SD_DEVICE_ENABLE == 1
    system_set_port_pull_down(GPIO_PC0|GPIO_PC1|GPIO_PC2, false);
    system_set_port_pull_up(GPIO_PC0|GPIO_PC1|GPIO_PC2, true);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_0, PORTC0_FUNC_SDC_CLK);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_1, PORTC1_FUNC_SDC_CMD);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_2, PORTC2_FUNC_SCD_DAT0);
    system_regs->reset_ctrl.sdc_sft_rst = 1;
    system_regs->sdc_ctrl.sdc_clk_div = 0;

    sdc_init(0, 0, 7, 0);
    sdc_bus_set_clk(0);
    MAL_Init(MASS_LUN_IDX_SD_NAND);
#if MP3_SRC_SDCARD == 1
    fs_init();
    //ff_test_main5();
#endif  // MP3_SRC_SDCARD == 1
#endif  // SD_DEVICE_ENABLE == 1

#if DSP_USE_XIP

#if FR5086D_V1_0
//    system_set_port_pull_up(GPIO_PD0, true);
//    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_0, PORTD0_FUNC_DSP_UART_RXD);
//    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_1, PORTD1_FUNC_DSP_UART_TXD);
    //system_set_port_pull_up(GPIO_PC6, true);
    //system_set_port_mux(GPIO_PORT_C, GPIO_BIT_6, PORTC6_FUNC_DSP_UART_RXD);
    //system_set_port_mux(GPIO_PORT_C, GPIO_BIT_7, PORTC7_FUNC_DSP_UART_TXD);
    
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_0, PORTA0_FUNC_QSPI1_SCLK);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_1, PORTA1_FUNC_QSPI1_CS);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_2, PORTA2_FUNC_QSPI1_MISO0);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_3, PORTA3_FUNC_QSPI1_MISO1);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_4, PORTA4_FUNC_QSPI1_MISO2);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_5, PORTA5_FUNC_QSPI1_MISO3);
//    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_6, PORTD6_FUNC_QSPI1_MISO2);
//    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_7, PORTD7_FUNC_QSPI1_MISO3);
    
    system_set_port_pull_up((GPIO_PB1|GPIO_PB2|GPIO_PB3), true);
    system_set_port_pull_down((GPIO_PB1|GPIO_PB2|GPIO_PB3), false);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_0, PORTB0_FUNC_DSP_IO0);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_1, PORTB1_FUNC_DSP_IO1);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_2, PORTB2_FUNC_DSP_IO2);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_3, PORTB3_FUNC_DSP_IO3);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_5, PORTC5_FUNC_DSP_IO5);
    
//    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_1, PORTB1_FUNC_B1);
//    gpio_set_dir(GPIO_PORT_B, GPIO_BIT_1, GPIO_DIR_OUT);
//    gpio_portb_write(gpio_portb_read() | 0x02);
#endif  // FR5086D_V1_0

#if FR5082DM_V1_0
    system_set_port_pull_up(GPIO_PC6, true);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_6, PORTC6_FUNC_DSP_UART_RXD);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_7, PORTC7_FUNC_DSP_UART_TXD);
    
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_0, PORTA0_FUNC_QSPI1_SCLK);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_1, PORTA1_FUNC_QSPI1_CS);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_2, PORTA2_FUNC_QSPI1_MISO0);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_3, PORTA3_FUNC_QSPI1_MISO1);
//    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_4, PORTA4_FUNC_QSPI1_MISO2);
//    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_5, PORTA5_FUNC_QSPI1_MISO3);
    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_6, PORTD6_FUNC_QSPI1_MISO2);
    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_7, PORTD7_FUNC_QSPI1_MISO3);
    
    system_set_port_pull_up((GPIO_PB1|GPIO_PB2|GPIO_PB3), true);
    system_set_port_pull_down((GPIO_PB1|GPIO_PB2|GPIO_PB3), false);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_0, PORTB0_FUNC_DSP_IO0);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_1, PORTB1_FUNC_DSP_IO1);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_2, PORTB2_FUNC_DSP_IO2);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_3, PORTB3_FUNC_DSP_IO3);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_5, PORTC5_FUNC_DSP_IO5);
#endif  // FR5082DM_V1_0

#if FR5086_JET
    system_set_port_pull_up(GPIO_PC6, true);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_6, PORTC6_FUNC_DSP_UART_RXD);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_7, PORTC7_FUNC_DSP_UART_TXD);
    
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_0, PORTA0_FUNC_QSPI1_SCLK);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_1, PORTA1_FUNC_QSPI1_CS);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_2, PORTA2_FUNC_QSPI1_MISO0);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_3, PORTA3_FUNC_QSPI1_MISO1);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_4, PORTA4_FUNC_QSPI1_MISO2);
    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_7, PORTD7_FUNC_QSPI1_MISO3);
    
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_0, PORTB0_FUNC_DSP_IO0);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_2, PORTB2_FUNC_DSP_IO2);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_3, PORTB3_FUNC_DSP_IO3);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_5, PORTC5_FUNC_DSP_IO5);
#endif  // FR5086_JET

#if FR5086D_V2_0
    system_set_port_pull_up(GPIO_PC6, true);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_6, PORTC6_FUNC_DSP_UART_RXD);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_7, PORTC7_FUNC_DSP_UART_TXD);
    
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_0, PORTA0_FUNC_QSPI1_SCLK);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_1, PORTA1_FUNC_QSPI1_CS);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_2, PORTA2_FUNC_QSPI1_MISO0);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_3, PORTA3_FUNC_QSPI1_MISO1);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_4, PORTA4_FUNC_QSPI1_MISO2);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_5, PORTA5_FUNC_QSPI1_MISO3);
    
    system_set_port_pull_up((GPIO_PB1|GPIO_PB2|GPIO_PB3), true);
    system_set_port_pull_down((GPIO_PB1|GPIO_PB2|GPIO_PB3), false);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_0, PORTB0_FUNC_DSP_IO0);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_1, PORTB1_FUNC_DSP_IO1);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_2, PORTB2_FUNC_DSP_IO2);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_3, PORTB3_FUNC_DSP_IO3);
#endif  // FR5086D_V2_0
#else   // DSP_USE_XIP
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_0, PORTB0_FUNC_DSP_IO0);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_1, PORTB1_FUNC_DSP_IO1);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_2, PORTB2_FUNC_DSP_IO2);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_3, PORTB3_FUNC_DSP_IO3);
    
    system_set_port_pull_up(GPIO_PC6, true);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_6, PORTC6_FUNC_DSP_UART_RXD);
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_7, PORTC7_FUNC_DSP_UART_TXD);
#endif  // DSP_USE_XIP

#if FIXED_FREQ_TX_TEST
    printf("/**************************************************/\r\n");
    printf("Entering FR5080 TX mode\r\n");
    printf("AT cmd description:\r\n");
    printf("AT#TAaa_bb_cc  --- Modulating Transmit,\r\n");
    printf("'aa' --- channel [0x02,0x50],shall be even\r\n");
    printf("'bb' --- packet type,default:0x00,other type refer to spec\r\n");
    printf("'cc' --- transmit power,[0x00,0x3f]\r\n");
            
    printf("AT#TBaa_bb_cc  --- Carrier Transmit,\r\n");
    printf("'aa' --- channel, [0x02,0x50]\r\n");
    printf("'bb' --- not used,set 0x00 \r\n");
    printf("'cc' --- transmit power,[0x00,0x3f]\r\n");

    printf("AT#TC --- exit test mode\r\n");
    printf("/**************************************************/\r\n");
    printf("Please enter at cmd\r\n");
#endif
    //os_timer_init(&onkey_timer,onkey_timer_func,NULL);
    //ool_write(PMU_REG_INT_EN_1, ((ool_read(PMU_REG_INT_EN_1) | ((PMU_INT_ONKEY)))));


    //os_timer_init(&bt_test_reconnect_timer,bt_test_reconnect_timer_func,NULL);
    //os_timer_start(&bt_test_reconnect_timer,1000,false);
}

 void pmu_onkey_isr_ram(void)
{
    printf("onkey isr\r\n");
    
    system_sleep_direct_enable();
    /*
    if(cur_flag == 0){
        bt_statemachine(USER_EVT_NATIVE_PLAYBACK_START,NULL);
        cur_flag = 1;
    }
    else{
        bt_statemachine(USER_EVT_NATIVE_PLAYBACK_STOP,NULL);
        cur_flag = 0;
    }*/
    os_timer_start(&onkey_timer,500,0);
}

void bt_notify_error_func(uint8_t error)
{
    printf("do nothing\r\n");
}


#if USB_DEVICE_ENABLE == 1
uint8_t usb_in_flag = 0; // 0 --- usb out, 1 --- usb in, 2 --- usb data transfer, 3 --- usb no data transfer

void usb_user_notify(enum      usb_action_t flag)
{
    //printf("usb flag = %d\r\n",flag);
    if(usb_in_flag == flag){
        return;
    }
    else{
        usb_in_flag = flag;
        if(flag == USB_ACTION_IN){
            system_sleep_disable();
        }
        else if(flag == USB_ACTION_OUT){
            system_sleep_enable();
        }
        else if(flag == USB_ACTION_DATA_TRANSFER){
            gap_stop_advertising();
            bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE,NULL);
            bt_disconnect();
        }
        else if(flag == USB_ACTION_IDLE){
            gap_start_advertising(0);
            bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
        }
    }

}

#endif

