#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "bt_api.h"
#include "hf_api.h"
#include "avrcp_api.h"
#include "a2dp_api.h"

#include "os_msg_q.h"
#include "os_mem.h"
#include "os_timer.h"
#include "user_utils.h"

#include "driver_gpio.h"
#include "driver_uart.h"
#include "driver_plf.h"
#include "driver_pmu.h"
#include "driver_codec.h"
#include "driver_button.h"

#include "user_task.h"
#include "audio_source.h"
#include "native_playback.h"
#include "user_bt.h"
#include "user_utils.h"
#include "co_log.h"
#include "mp3_tag_decoder.h"
#include "user_fs.h"
#include "app_at.h"
#include "gap_api.h"

#if COPROCESSOR_UART_ENABLE
///port define
#define UART_SLAVE_WAKE_IO          GPIO_PC4
#define UART_SLAVE_WAKE_PIN_PORT   GPIO_PORT_C    //input,used by master to wake up slave
#define UART_SLAVE_WAKE_PIN_BIT     GPIO_BIT_4
#define UART_SLAVE_IND_PIN_PORT    GPIO_PORT_C    //output,used by slave to indicate master that slave has data to send
#define UART_SLAVE_IND_PIN_BIT      GPIO_BIT_5

#define UART_SLAVE_BAUDRATE     BAUD_RATE_115200

enum uart_lp_rx_state_t{
    UART_LP_RX_STATE_IDLE,
    UART_LP_RX_STATE_HEADER,
    UART_LP_RX_STATE_DATA,
};

//co_list_t uart_msg_list;
struct uart_lp_env_t uart_lp_env;

os_timer_t uart_slave_wake_timer;
os_timer_t uart_slave_check_timer;

#endif

#define AT_RECV_MAX_LEN             32
#if PBAP_FUC == 1
    uint8_t pbap_connect_flag = 0;
#endif
static uint8_t app_at_recv_char;
static uint8_t at_recv_buffer[AT_RECV_MAX_LEN];
static uint8_t at_recv_index = 0;
static uint8_t at_recv_state = 0;

extern struct user_bt_env_tag *user_bt_env;

#include "ff.h"
static void app_at_recv_cmd_A(uint8_t sub_cmd, uint8_t *data)
{
    //struct bd_addr addr;
    //uint8_t tmp_data = 0;
    enum bt_state_t bt_state = bt_get_state();
    bool ret = false;

    switch(sub_cmd)
    {
        case 'A':
            
            GLOBAL_INT_DISABLE();
            
            gpio_set_pin_value(GPIO_PORT_B, GPIO_BIT_0,1);
            co_delay_100us(100);
            gpio_set_pin_value(GPIO_PORT_B, GPIO_BIT_0,0);
            GLOBAL_INT_RESTORE();
            //pskeys_update_app_data();
            printf("OK\r\n");
            break;
        case 'B':
            if((bt_state <= BT_STATE_CONNECTED)&&(true == user_check_native_playback_allowable(USER_EVT_NATIVE_PLAYBACK_NEXT))){
                //ret = native_playback_action_next();
                ret = bt_statemachine(USER_EVT_NATIVE_PLAYBACK_NEXT,NULL);
            }
            if(ret == false){
                uart_send("FAIL\r\n",6);
            }

            break;
        case 'C':
            
            if((bt_state <= BT_STATE_CONNECTED)&&(true == user_check_native_playback_allowable(USER_EVT_NATIVE_PLAYBACK_START))){
                //ret = native_playback_action_play();
                ret = bt_statemachine(USER_EVT_NATIVE_PLAYBACK_START,NULL);
            }
            if(ret == false){
                uart_send("FAIL\r\n",6);
            }

            break;
        case 'D':
            if(true == user_check_native_playback_allowable(USER_EVT_NATIVE_PLAYBACK_STOP)){
                ret = bt_statemachine(USER_EVT_NATIVE_PLAYBACK_STOP,NULL);
            }
            if(ret == true){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }
            //ret = native_playback_action_pause();
            break;
        case 'E':
            if((bt_state <= BT_STATE_CONNECTED)&&(true == user_check_native_playback_allowable(USER_EVT_NATIVE_PLAYBACK_PREV))){
                //ret = native_playback_action_prev();
                ret = bt_statemachine(USER_EVT_NATIVE_PLAYBACK_PREV,NULL);
            }
            if(ret == false){
                uart_send("FAIL\r\n",6);
            }

            break;
        
        case 'G':
            printf("hello world!!!!\r\n");
            co_delay_100us(3000);
            break;
        case 'H':
            printf("VAL: 0x%08x.\r\n", REG_PL_RD(ascii_strn2val((const char *)&data[0], 16, 8)));
            break;
        case 'I':
            REG_PL_WR(ascii_strn2val((const char *)&data[0], 16, 8), ascii_strn2val((const char *)&data[9], 16, 8));
            printf("OK\r\n");
            break;
            
        case 'J':
            if((user_bt_env->dev[user_bt_env->last_active_dev_index].conFlags&LINK_STATUS_HF_CONNECTED) && (user_bt_env->dev[user_bt_env->last_active_dev_index].active == HF_CALL_NONE)) {
                hf_enable_voice_recognition(user_bt_env->dev[user_bt_env->last_active_dev_index].hf_chan, TRUE);
            }
            uart_send("OK\r\n",4);
            break;
        case 'K':
            if((user_bt_env->dev[user_bt_env->last_active_dev_index].conFlags&LINK_STATUS_HF_CONNECTED) && (user_bt_env->dev[user_bt_env->last_active_dev_index].active == HF_CALL_NONE)) {
                hf_enable_voice_recognition(user_bt_env->dev[user_bt_env->last_active_dev_index].hf_chan, FALSE);
            }
            uart_send("OK\r\n",4);
            break;
        case 'L':
            audio_play_tone(0x80+ascii_strn2val((const char*)&data[0],16,2));
            uart_send("OK\r\n",4);
            break;
        case 'M':
            if(bt_state <= BT_STATE_CONNECTED){
                //audio_codec_func_start(AUDIO_CODEC_FUNC_MIC_LOOP);
                bt_statemachine(USER_EVT_MIC_LOOP_START,NULL);
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }
            break;
        case 'N':
            //audio_codec_func_stop(AUDIO_CODEC_FUNC_MIC_LOOP);
            bt_statemachine(USER_EVT_MIC_LOOP_STOP,NULL);
            uart_send("OK\r\n",4);
            break;
        case 'P':
            printf("cnt = %d\r\n",system_get_btdm_active_cnt());
            break;
        case 'Q':
            system_set_btdm_active_cnt(0);
            printf("OK\r\n");
            break;
        case 'S':
            if(bt_state <= BT_STATE_CONNECTED){
                //audio_codec_func_start(AUDIO_CODEC_FUNC_MIC_ONLY);
                ret = bt_statemachine(USER_EVT_MIC_ONLY_START,NULL);
            }
            if(ret == true){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }
            break;
        case 'T':
            //audio_codec_func_stop(AUDIO_CODEC_FUNC_MIC_ONLY);
            bt_statemachine(USER_EVT_MIC_ONLY_STOP,NULL);
            uart_send("OK\r\n",4);
            break;
            
        case 'U':
            {
                uint32_t *ptr = (uint32_t *)(ascii_strn2val((const char *)&data[0], 16, 8) & (~3));
                uint8_t count = ascii_strn2val((const char *)&data[9], 16, 2);
                uint32_t *start = (uint32_t *)((uint32_t)ptr & (~0x0f));
                printf("ptr is 0x%08x, count = %d.\r\n", ptr, count);
                for(uint8_t i=0; i<count;) {
                    if(((uint32_t)start & 0x0c) == 0) {
                        printf("0x%08x: ", start);
                    }
                    if(start < ptr) {
                        printf("        ");
                    }
                    else {
                        i++;
                        printf("%08x", *start);
                    }
                    if(((uint32_t)start & 0x0c) == 0x0c) {
                        printf("\r\n");
                    }
                    else {
                        printf(" ");
                    }
                    start++;
                }
            }
            break;
        case 'V':
            if((bt_get_a2dp_type() == BT_A2DP_TYPE_SRC)&&((bt_state == BT_STATE_CONNECTED)||(bt_state == BT_STATE_MEDIA_PLAYING))){
                ret = audio_source_action_next();
            }
            if(ret == false){
                uart_send("FAIL\r\n",6);
            }
            break;
        case 'W':
            if((bt_get_a2dp_type() == BT_A2DP_TYPE_SRC)&&(bt_state == BT_STATE_CONNECTED)){
                ret = audio_source_action_play();
            }
            if(ret == false){
                uart_send("FAIL\r\n",6);
            }

            break;
        case 'X':
            if((bt_get_a2dp_type() == BT_A2DP_TYPE_SRC)&&(bt_state == BT_STATE_MEDIA_PLAYING)){
                ret = audio_source_action_pause();
            }
            if(ret == false){
                uart_send("FAIL\r\n",6);
            }else{
                uart_send("OK\r\n",4);
            }           
            break;
        case 'Y':
            if((bt_get_a2dp_type() == BT_A2DP_TYPE_SRC)&&((bt_state == BT_STATE_CONNECTED)||(bt_state == BT_STATE_MEDIA_PLAYING))){
                ret = audio_source_action_prev();
            }
            if(ret == false){
                uart_send("FAIL\r\n",6);
            }

            break;
        case 'Z':
            //fs_prepare_next();
            //fs_prepare_next();
            break;
            
        default:
            break;
    }
}

extern int8_t rxrssi;
static void app_at_recv_cmd_B(uint8_t sub_cmd, uint8_t *data)
{
    BtSniffInfo sniff_info; 
    BD_ADDR addr;
    
    switch(sub_cmd)
    {
        case 'A':
            bt_start_inquiry(5,5);
            printf("OK\r\n");
        break;
        case 'B':
            bt_cancel_inquiry();
            printf("OK\r\n");
        break;

        case 'C':
            bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
            printf("OK\r\n");
        break;
    
        case 'D':
            bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE,NULL);
            printf("OK\r\n");
        break;
        
        case 'E':
            sniff_info.maxInterval = 0x320;
            sniff_info.minInterval = 0x320;
            sniff_info.attempt = 0x01;
            sniff_info.timeout = 0x00;
            bt_start_sniff(user_bt_env->dev[0].remDev,(const BtSniffInfo *)&sniff_info);
            printf("OK\r\n");
        break;
        
        case 'F':
            printf("current mode = %d\r\n",bt_get_current_mode(user_bt_env->dev[0].remDev));
        break;
        
        case 'G':
            bt_reconnect(RECONNECT_TYPE_USER_CMD,NULL);
            printf("OK\r\n");
        break;

        case 'H':
            bt_set_spk_volume(BT_VOL_MEDIA,ascii_strn2val((const char*)&data[0],16,2));
            printf("OK\r\n");
        break;

        case 'I':
            bt_set_spk_volume(BT_VOL_HFP,ascii_strn2val((const char*)&data[0],16,2));
            printf("OK\r\n");
        break;
        
        case 'J':
            bt_disconnect();
            printf("OK\r\n");
        break;

        case 'K':
            hf_redial(user_bt_env->dev[0].hf_chan);
            printf("OK\r\n");
        break;
        
        case 'L':
            //hf_hang_up(user_bt_env->dev[0].hf_chan);
            //printf("OK\r\n");
            if(data[2] == '_'){
                ool_write(ascii_strn2val((const char*)&data[0],16,2), ascii_strn2val((const char*)&data[3],16,2));
                printf("\r\nOK\r\n");

            }else{
                printf("\r\n0x%x\r\n",ool_read(ascii_strn2val((const char*)&data[0],16,2)));
            }

        break;
        
        case 'M':
            if(data[8] == '\r') {
            //read uint32
                printf("\r\n0x%x\r\n",REG_PL_RD(ascii_strn2val((const char*)&data[0],16,8)));
                return;
            }else if(data[8] == '_') {
                REG_PL_WR(ascii_strn2val((const char*)&data[0],16,8), ascii_strn2val((const char*)&data[9],16,8));
                printf("\r\nOK\r\n");
            }
        break;

        case 'N':
            #if 0
            addr.A[0] = 0xA7;
            addr.A[1] = 0x50;
            addr.A[2] = 0x58;
            addr.A[3] = 0x16;
            addr.A[4] = 0x52;
            addr.A[5] = 0x1c;
            #else
            addr.A[0] = 0x23;
            addr.A[1] = 0xd2;
            addr.A[2] = 0x57;
            addr.A[3] = 0x16;
            addr.A[4] = 0x52;
            addr.A[5] = 0x1c;

            #endif
            memcpy(&user_bt_env->dev[0].bd,&addr,6);
            a2dp_open_stream(AVDTP_STRM_ENDPOINT_SNK, &addr);
            printf("OK\r\n");
        break;

        case 'O':
            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SINK) {
                bt_set_a2dp_type(BT_A2DP_TYPE_SRC);
            }
            else if(bt_get_a2dp_type() == BT_A2DP_TYPE_SRC) {
                bt_set_a2dp_type(BT_A2DP_TYPE_SINK);
            }
            printf("OK\r\n");
        break;
#if 0
        case 'P':
            a2dp_start_stream(user_bt_env->dev[0].pstream);
            printf("OK\r\n");
        break;
        
        case 'Q':
            a2dp_suspend_stream(user_bt_env->dev[0].pstream);
            printf("OK\r\n");
        break;
#else
        case 'P':
            printf("\r\n0x%x\r\n",REG_PL_RD8(MDM_BASE+ascii_strn2val((const char*)&data[0],16,2)));
            break;
        case 'Q':
            REG_PL_WR8(MDM_BASE+ascii_strn2val((const char*)&data[0],16,2), ascii_strn2val((const char*)&data[3],16,2));
            printf("\r\nOK\r\n");
            break;

#endif
        case 'R':
            if(BT_STATUS_PENDING == hf_enable_voice_recognition(user_bt_env->dev[0].hf_chan, TRUE)){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }
        break;
        case 'S':
            if(BT_STATUS_PENDING == hf_enable_voice_recognition(user_bt_env->dev[0].hf_chan, FALSE)){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }
        break;
        case 'T':
            printf("enter bt test mode\r\n");
            bt_enter_bredr_testmode();
            printf("OK\r\n");
        break;
        case 'U':
            if(ascii_strn2val((const char*)&data[0],16,2) == 0){
                printf("enter ble test mode , hci uart: PB6,PB7\r\n");
                co_delay_100us(10);
                bt_enter_ble_testmode(GPIO_PB6,GPIO_PB7);
            }else if(ascii_strn2val((const char*)&data[0],16,2) == 1){
                printf("enter ble test mode , hci uart: PA0,PA1\r\n");
                co_delay_100us(10);
                bt_enter_ble_testmode(GPIO_PA0,GPIO_PA1);
            }else if(ascii_strn2val((const char*)&data[0],16,2) == 2){
                printf("enter ble test mode , hci uart: PA6,PA7\r\n");
                co_delay_100us(10);
                bt_enter_ble_testmode(GPIO_PA6,GPIO_PA7);
            }
        break;
        case 'V':
            printf("enter bt sensitivity test\r\n");
            bt_enter_rx_sensitivity_testmode();
            //avrcp_ct_get_media_info(user_bt_env->dev[0].rcp_chan, 0x41);
            printf("OK\r\n");
        break;
        case 'W':
            {
                uint8_t *data_spp = os_malloc(10);
                memcpy(data_spp,"123456789a",10);
                spp_send_data(data_spp, 10);
                printf("OK\r\n");
            }
        break;
        
        case 'X':
            printf("rssi = %d\r\n",rxrssi);
        break;
                
        case 'Y':
            ool_write(PMU_REG_DLDO_CTRL_0, 0x68);//dldo bypass
            pmu_power_on_DSP();
            ool_write(PMU_REG_DLDO_CTRL_0, 0x48);
            printf("OK\r\n");
        break;
                
        case 'Z':
            pmu_power_off_DSP();
        printf("OK\r\n");
        break;
        default:
            
        break;
    }
}

uint8_t bt_inquiry_flag = false;
uint8_t bt_change_a2dp_flag = 0;
extern uint8_t ipc_mp3_start_flag;

static void app_at_recv_cmd_C(uint8_t sub_cmd, uint8_t *data)
{
    BD_ADDR addr;    
    BtStatus status;
    enum bt_state_t bt_state = bt_get_state();
    uint8_t i = 0;

    switch(sub_cmd)
    {
        case 'A':
            if((bt_state == BT_STATE_HFP_INCOMMING)
                &&(BT_STATUS_PENDING == hf_answer_call(user_bt_env->dev[user_bt_env->last_active_dev_index].hf_chan))){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }

        break;
        case 'B':
            if(((bt_state == BT_STATE_HFP_INCOMMING)||(bt_state == BT_STATE_HFP_OUTGOING)||(bt_state == BT_STATE_HFP_CALLACTIVE))
                &&(BT_STATUS_PENDING == hf_hang_up(user_bt_env->dev[user_bt_env->last_active_dev_index].hf_chan))){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }

        break;

        case 'C':
            if(((bt_state == BT_STATE_CONNECTED)||(bt_state == BT_STATE_MEDIA_PLAYING))
                &&(BT_STATUS_PENDING == hf_redial(user_bt_env->dev[user_bt_env->last_active_dev_index].hf_chan))){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }

        break;
    
        case 'D':
            if((bt_state == BT_STATE_MEDIA_PLAYING)
                &&(BT_STATUS_PENDING == avrcp_set_panel_key(user_bt_env->dev[user_bt_env->last_active_dev_index].rcp_chan, AVRCP_POP_PAUSE,TRUE))){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }

        break;
        
        case 'E':
            if((bt_state == BT_STATE_CONNECTED)
                &&(BT_STATUS_PENDING == avrcp_set_panel_key(user_bt_env->dev[user_bt_env->last_active_dev_index].rcp_chan, AVRCP_POP_PLAY,TRUE))){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }

        break;
        
        case 'F':
            if(((bt_state == BT_STATE_CONNECTED)||(bt_state == BT_STATE_MEDIA_PLAYING))
                &&(BT_STATUS_PENDING == avrcp_set_panel_key(user_bt_env->dev[user_bt_env->last_active_dev_index].rcp_chan, AVRCP_POP_FORWARD,TRUE))){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }
        break;
        
        case 'G':
            if(((bt_state == BT_STATE_CONNECTED)||(bt_state == BT_STATE_MEDIA_PLAYING))
                &&(BT_STATUS_PENDING == avrcp_set_panel_key(user_bt_env->dev[user_bt_env->last_active_dev_index].rcp_chan, AVRCP_POP_BACKWARD,TRUE))){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }
        break;

        case 'H':
            if(bt_state == BT_STATE_PAIRING){
                bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE,NULL);
                bt_start_inquiry(10,10);
                bt_inquiry_flag = true;
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }
        break;

        case 'I':
            bt_cancel_inquiry();
            bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
            bt_inquiry_flag = false;
            uart_send("OK\r\n",4);
        break;

        case 'J':
            if(bt_state != BT_STATE_PAIRING){
                uart_send("FAIL\r\n",6);
            }else{
                if(data[0] == '\r') {
                    if(false == bt_reconnect(RECONNECT_TYPE_USER_CMD,NULL)){
                        uart_send("FAIL\r\n",6);
                    }else{
                        uart_send("OK\r\n",4);
                    }
                }else if(data[12] == '\r'){
                    addr.A[0] = ascii_strn2val((const char*)&data[0],16,2);
                    addr.A[1] = ascii_strn2val((const char*)&data[2],16,2);
                    addr.A[2] = ascii_strn2val((const char*)&data[4],16,2);
                    addr.A[3] = ascii_strn2val((const char*)&data[6],16,2);
                    addr.A[4] = ascii_strn2val((const char*)&data[8],16,2);
                    addr.A[5] = ascii_strn2val((const char*)&data[10],16,2); 
                    if(false == bt_reconnect(RECONNECT_TYPE_USER_CMD,&addr)){
                        uart_send("FAIL\r\n",6);
                    }else{
                        uart_send("OK\r\n",4);
                    }
                }else{
                    uart_send("FAIL\r\n",6);
                }
            }
        break;

        case 'K':
            bt_disconnect();
            uart_send("OK\r\n",4);
        break;
        
        case 'L':
            if(bt_inquiry_flag == true){
                printf("+STATE:1\r\n");
            }else{
                if(bt_state == BT_STATE_PAIRING){
                    printf("+STATE:0\r\n");
                }else if(bt_state == BT_STATE_CONNECTING){
                    printf("+STATE:2\r\n");
                }else if(bt_state == BT_STATE_CONNECTED){
                    printf("+STATE:3\r\n");
                }else if((bt_state == BT_STATE_HFP_CALLACTIVE)||(bt_state == BT_STATE_HFP_INCOMMING)||(bt_state == BT_STATE_HFP_OUTGOING)){
                    printf("+STATE:4\r\n");
                }else if(bt_state == BT_STATE_MEDIA_PLAYING){
                    printf("+STATE:5\r\n");
                }else{
                    printf("+STATE:6\r\n");
                }
            }
        break;
        
        case 'M':
            if(user_bt_env->dev[user_bt_env->last_active_dev_index].mode == 2){
                printf("+MODE:1\r\n");
            }else if(user_bt_env->dev[user_bt_env->last_active_dev_index].mode == 0){
                printf("+MODE:0\r\n");
            }
        break;

        case 'N':
            while(data[i] != '\r'){
                i++;
            }
            if(((bt_state == BT_STATE_CONNECTED)||(bt_state == BT_STATE_MEDIA_PLAYING))
                &&(BT_STATUS_PENDING == hf_dial_number(user_bt_env->dev[user_bt_env->last_active_dev_index].hf_chan,data,i))){
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }

        break;

        case 'P':

        break;

        case 'O':
            if(user_bt_env->access_state == ACCESS_IDLE){
                status = bt_set_a2dp_type(BT_A2DP_TYPE_SRC);
                bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
                if(status == BT_STATUS_SUCCESS){
                    uart_send("OK\r\n",4);
                }else{
                    uart_send("FAIL\r\n",6);
                }
            }else{
                bt_change_a2dp_flag = 1;
                bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE,NULL);
            }

        break;
        
        case 'Q':
            if(user_bt_env->access_state == ACCESS_IDLE){
                status = bt_set_a2dp_type(BT_A2DP_TYPE_SINK);
                bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
                if(status == BT_STATUS_SUCCESS){
                    uart_send("OK\r\n",4);
                }else{
                    uart_send("FAIL\r\n",6);
                }
            }else{
                bt_change_a2dp_flag = 2;
                bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE,NULL);
            }

        break;

        case 'R':
            if((data[0] == 'H')&&(data[1] == 'F')){
                if(true == bt_set_spk_volume(BT_VOL_HFP,ascii_strn2val((const char*)&data[3],16,2))){
                    uart_send("OK\r\n",4);
                }else{
                    uart_send("FAIL\r\n",6);
                }

            }
            else if((data[0] == 'A')&&(data[1] == 'D')){
                if(true == bt_set_spk_volume(BT_VOL_MEDIA,ascii_strn2val((const char*)&data[3],16,2))){
                    uart_send("OK\r\n",4);
                }else{
                    uart_send("FAIL\r\n",6);
                }

            }else{
                uart_send("FAIL\r\n",6);
            }
        break;
            
        case 'S':
            gap_stop_advertising();
            printf("OK\r\n");
            break;
        case 'T':
            gap_start_advertising(0);
            printf("OK\r\n");
            break;
            
        case 'X':
            //gap_stop_advertising();
            if(bt_state == BT_STATE_PAIRING){
                bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE,NULL);
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }


            break;
        case 'Y':
            //gap_start_advertising(0);
            if(bt_state == BT_STATE_IDLE){
                bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
                uart_send("OK\r\n",4);
            }else{
                uart_send("FAIL\r\n",6);
            }

            break;
        case 'Z':
            //bt_stack_reset();
            printf("a2dp type = %d\r\n",bt_get_a2dp_type());
            break;
        default:
        break;
    }
}

static void app_at_recv_cmd_F(uint8_t sub_cmd, uint8_t *data)
{
    enum bt_state_t bt_state = bt_get_state();
    uint8_t uart_buf[20];
    switch(sub_cmd)
    {
        case 'A':
            if(false == fs_handle_mp3_info_req(NULL,ascii_strn2val((const char*)&data[0],16,2))){
                uart_send("FAIL\r\n",6);
            }
            break;
        case 'B':
            if(false == fs_handle_mp3_list_req(NULL,ascii_strn2val((const char*)&data[0],16,2))){
                uart_send("FAIL\r\n",6);
            }
            break;
        case 'C':
            co_sprintf((void *)uart_buf,"+MNUM:%02x\r\n",fs_get_mp3_num());
            uart_buf[10] = '\0';
            uart_send(uart_buf,10);
            break;
        case 'D':
            ///get bt addr
            co_sprintf((void *)uart_buf,"+MAC:%02x%02x%02x%02x%02x%02x\r\n",pskeys.local_bdaddr.addr[0],pskeys.local_bdaddr.addr[1], \
                pskeys.local_bdaddr.addr[2],pskeys.local_bdaddr.addr[3],pskeys.local_bdaddr.addr[4],pskeys.local_bdaddr.addr[5]);
            uart_buf[19] = '\0';
            uart_send(uart_buf,19);
            break;
        case 'E':
            uart_send("+VER:1.3.4\r\n",12);
            break;
        case 'F':
            bt_set_user_evt_notify_enable(ascii_strn2val((const char*)&data[0],16,2));
            uart_send("OK\r\n",4);
            //native_playback_action_specified(item1);
        break;
        case 'G':
            if(bt_get_state() >= BT_STATE_CONNECTED){
                bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE,NULL);
                bt_disconnect();
            }else{
                bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE,NULL);
            }
            gap_stop_advertising();
            pskeys.slp_max_dur = 0x3a0000;
            system_sleep_direct_enable();
            
            uart_send("OK\r\n",4);
        break;
        default:
            break;
    };
}

#if FIXED_FREQ_TX_TEST
struct lld_test_params
{
    /// Type (0: RX | 1: TX)
    uint8_t type;

    /// RF channel, N = (F - 2402) / 2
    uint8_t channel;

    /// Length of test data
    uint8_t data_len;

    /**
     * Packet payload
     * 0x00 PRBS9 sequence "11111111100000111101" (in transmission order) as described in [Vol 6] Part F, Section 4.1.5
     * 0x01 Repeated "11110000" (in transmission order) sequence as described in [Vol 6] Part F, Section 4.1.5
     * 0x02 Repeated "10101010" (in transmission order) sequence as described in [Vol 6] Part F, Section 4.1.5
     * 0x03 PRBS15 sequence as described in [Vol 6] Part F, Section 4.1.5
     * 0x04 Repeated "11111111" (in transmission order) sequence
     * 0x05 Repeated "00000000" (in transmission order) sequence
     * 0x06 Repeated "00001111" (in transmission order) sequence
     * 0x07 Repeated "01010101" (in transmission order) sequence
     * 0x08-0xFF Reserved for future use
     */
    uint8_t payload;

    /**
     * Tx/Rx PHY
     * For Tx PHY:
     * 0x00 Reserved for future use
     * 0x01 LE 1M PHY
     * 0x02 LE 2M PHY
     * 0x03 LE Coded PHY with S=8 data coding
     * 0x04 LE Coded PHY with S=2 data coding
     * 0x05-0xFF Reserved for future use
     * For Rx PHY:
     * 0x00 Reserved for future use
     * 0x01 LE 1M PHY
     * 0x02 LE 2M PHY
     * 0x03 LE Coded PHY
     * 0x04-0xFF Reserved for future use
     */
    uint8_t phy;
};

#define POWER_MAX 0x3f

static void app_at_recv_cmd_T(uint8_t sub_cmd, uint8_t *data)
{
    uint32_t val_B,temp;
    uint8_t val_A;
    uint8_t power,channel,type;
        
    switch(sub_cmd)
    {
        //ble 1Mbps 调制发�?
        case 'A':
            
            REG_PL_WR8(0x500210f1,0xc8);  //shall not change if dsp is working
            // parameter check
            power = ascii_strn2val((const char*)&data[6],16,2);
            channel = ascii_strn2val((const char *)&data[0], 16, 2);
            type = ascii_strn2val((const char *)&data[3], 16, 2);
            
            if(power > POWER_MAX){
                printf("error power parameter,power shall be in range[0x00,0x3f]....\r\n");
                return;
            }
            if(channel&0x01){
                printf("error channel parameter,channel shall be even....\r\n");
                return;
            }else if((channel == 0)||(channel > 80)){
                printf("error channel parameter,channel shall be in range[0x02,0x50]....\r\n");
                return;
            }
            if(type>7){
                printf("error data type parameter,type shall be in range[0x00,0x07]....\r\n");
                return;
            }
            
            *(volatile uint32_t *)0x400008d0 = 0x80008000;

            //REG_PL_WR8(0x50021051,0x40|power);
            REG_PL_WR8(0x50021081,power);

            struct lld_test_params test_params;
            test_params.type = 1;
            test_params.channel = channel/2 - 1;//0-39
            test_params.data_len = 0x25;
            test_params.payload = ascii_strn2val((const char *)&data[3], 16, 2);// type
            test_params.phy = 1;
            //lld_test_start(&test_params);
            ((uint8_t (*)(struct lld_test_params*))(0x000558f9))(&test_params);
            
            printf("OK\r\n");
        break;
        //单载波发射，非调�?
        case 'B':
            // parameter check
            power = ascii_strn2val((const char*)&data[6],16,2);
            channel = ascii_strn2val((const char *)&data[0], 16, 2);
            type = ascii_strn2val((const char *)&data[3], 16, 2);
            if(power > POWER_MAX){
                printf("error power parameter,power shall be in range[0x00,0x3f]....\r\n");
                return;
            }
            if((channel < 2)||(channel > 80)){
                printf("error channel parameter,channel shall be in range[0x02,0x50]....\r\n");
                return;
            }

            GLOBAL_INT_DISABLE();
            REG_PL_WR8(0x500210f1,0xff);
            REG_PL_WR8(0x50021000,0x00);
            REG_PL_WR8(0x50021001,0xff);

            //信道
            temp = 2400 + channel;
            val_A = temp/18;
            val_B = (temp-val_A*18)*0xffffff/18;
            
            REG_PL_WR8(0x50021023,val_A);
            REG_PL_WR8(0x50021024,(val_B>>16)&0xff);
            REG_PL_WR8(0x50021025,(val_B>>8)&0xff);
            REG_PL_WR8(0x50021026,val_B&0xff);
            
            REG_PL_WR8(0x5002100d,0x7e);
            REG_PL_WR8(0x500210f2,0x00);
            REG_PL_WR8(0x500210f2,0x01);
            
            co_delay_100us(1);
            REG_PL_WR8(0x50021002,0x01);
            REG_PL_WR8(0x50021002,0x81);
            
            //REG_PL_WR8(0x50021051,0x40|power);
            REG_PL_WR8(0x50021081,power);
            //REG_PL_WR8(0x5002104b,0xa9);
            GLOBAL_INT_RESTORE();
            printf("OK\r\n");
        break;

        //停止测试
        case 'C':
            REG_PL_WR8(0x500210f1,0xc8);  //shall not change if dsp is working
            REG_PL_WR8(0x5002100d,0x6f);
            REG_PL_WR8(0x50021002,0x03);
            //REG_PL_WR8(0x5002104b,0xa1);
            
            REG_PL_WR8(0x50021081,0x1f);
            //lld_test_stop();
            ((uint8_t (*)(void))(0x00055b75))();
            printf("OK\r\n");

        break;

        // BR 调制发�?
        case 'D':
            //infinite tx/rx
            *(volatile uint32_t *)0x400004d0 = 0x80008000;

            //bt_rftestfreq_rxfreq_setf(2);
            
        break;

        //EDR 调制发�?
        case 'E':
        break;


        default:
            break;

    }       
}
#endif


#if PBAP_FUC == 1
os_timer_t pcconnect_req_timeout_handle_id ={
  NULL  
};
static void pcconnect_req_timeout_handle(void *param)
{
   printf("+PTO");
   pba_abort_client(); 
}
void user_pull_sim_phonebook(uint8_t phonetype,uint16_t list_offset,uint16_t maxlist)  
{
    static      Pbap_Vcard_Filter filter;
    uint16_t    listOffset, maxListCount;
    char        pName[64];     
    listOffset = list_offset;
    maxListCount = maxlist;
    memset(pName,0,sizeof(pName));
    memset(&filter.byte,0,sizeof(Pbap_Vcard_Filter));
    filter.byte[0] = 0x84;
    switch(phonetype)
    {
        case PhoneBookList:
            memcpy(pName,PB_SIM_STORE_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case IncomingCallList:
            memcpy(pName,PB_SIM_ICH_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case OutgoingCallList:
            memcpy(pName,PB_SIM_OCH_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case MissCallList:
            memcpy(pName,PB_SIM_MCH_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case CombinedCallList:
            memcpy(pName,PB_SIM_CCH_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case SpeedDialList:
            memcpy(pName,PB_SIM_SPD_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case FavoriteNumberList:
            memcpy(pName,PB_SIM_FAV_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        default:
            memcpy(pName,PB_SIM_STORE_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
    }
    PBA_PullPhonebook(pName, &filter, maxListCount, listOffset,VCARD_FORMAT_30);
}

void user_pull_phonebook(uint8_t phonetype,uint16_t list_offset,uint16_t maxlist)
{
    
    static      Pbap_Vcard_Filter filter;
    uint16_t    listOffset, maxListCount;
    char        pName[64];     
    listOffset = list_offset;
    maxListCount = maxlist;
    memset(pName,0,sizeof(pName));
    memset(&filter.byte,0,sizeof(Pbap_Vcard_Filter));
    filter.byte[0] = 0x84;
    switch(phonetype)
    {
        case PhoneBookList:
            memcpy(pName,PB_LOCAL_STORE_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case IncomingCallList:
            memcpy(pName,PB_LOCAL_ICH_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case OutgoingCallList:
            memcpy(pName,PB_LOCAL_OCH_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case MissCallList:
            memcpy(pName,PB_LOCAL_MCH_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case CombinedCallList:
            memcpy(pName,PB_LOCAL_CCH_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case SpeedDialList:
            memcpy(pName,PB_LOCAL_SPD_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        case FavoriteNumberList:
            memcpy(pName,PB_LOCAL_FAV_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
        default:
            memcpy(pName,PB_LOCAL_STORE_NAME,sizeof(PB_LOCAL_STORE_NAME));
            break;
    }
    PBA_PullPhonebook(pName, &filter, maxListCount, listOffset,VCARD_FORMAT_30);
}
#if 0
void user_pull_vcarlist(void)
{
    char        searchValue[15];
    uint16_t    listOffset, maxListCount;
    listOffset = 0;
    maxListCount = 0xffff;
    memset(searchValue,0,15);
     
    PBA_PullVcardListing(PB_LOCAL_STORE_NAME,VCARD_SEARCH_ATTRIB_NUMBER,searchValue,VCARD_SORT_ORDER_INDEXED,maxListCount,listOffset);
}
void user_entry_vcard(void)
{
    Pbap_Vcard_Filter filter;
    memset(&filter.byte,0,sizeof(Pbap_Vcard_Filter));
    PBA_PullVcardEntry(PB_LOCAL_STORE_NAME,&filter,VCARD_FORMAT_30);
}
#endif
void app_at_recv_cmd_P(uint8_t cmd,uint8_t *param)
{
    switch(cmd)
    {
        case 'C':
        {
            if(!pbap_connect_flag)
            {
                pba_connect();
                //uart_send("OK\r\n",4);
                if(pcconnect_req_timeout_handle_id.timer_id == NULL)
                {
                    os_timer_init(&pcconnect_req_timeout_handle_id,pcconnect_req_timeout_handle,NULL);
                }
                os_timer_start(&pcconnect_req_timeout_handle_id,5000,false);
            }else{
                uart_send("FAIL\r\n",6);
            }
        }
            break;
        
        case 'P':
        {
            if(pbap_connect_flag)
            {
                uint16_t list_offset = 0,max_list  = 0xffff;
                uint8_t phone_type = 0;
                phone_type = ascii_strn2val((const char*)param,16,2);
                list_offset = ascii_strn2val((const char*)param+3,16,4);
                max_list = ascii_strn2val((const char*)param+8,16,4);           
                //printf("list_offset=%x,max list=%x\r\n",list_offset,max_list);
                user_pull_phonebook(phone_type,list_offset,max_list); 
                //printf("OK");
            }else{
                uart_send("FAIL\r\n",6);
            }
        }
            break;
        case 'S':
        {
            if(pbap_connect_flag)
            {
                uint16_t list_offset = 0,max_list  = 0xffff;
                uint8_t phone_type = 0;
                phone_type = ascii_strn2val((const char*)param,16,2);
                list_offset = ascii_strn2val((const char*)param+3,16,4);
                max_list = ascii_strn2val((const char*)param+8,16,4);
                user_pull_sim_phonebook(phone_type,list_offset,max_list); 
                //printf("OK");
            }else{
                uart_send("FAIL\r\n",6);
            }
        }
            break;
        case 'T':
        {
            if(pbap_connect_flag)
            {
                pba_disconnect();
                //printf("OK");
            }else{
                uart_send("FAIL\r\n",6);
            }
        }
            break;
        default:
            //printf("FAIL");
            break;
    }
}
#endif

void app_at_cmd_recv_handler(uint8_t *data, uint16_t length)
{
    switch(data[0])
    {
        case 'A':
            
            //uart_putc_noint('2');
            //printf("%c,%c,%c,%c\r\n",data[1],data[2],data[3],data[4]);
            app_at_recv_cmd_A(data[1], &data[2]);
            break;
        case 'B':
            app_at_recv_cmd_B(data[1], &data[2]);
            break;
        case 'C':
            app_at_recv_cmd_C(data[1], &data[2]);
            break;
        case 'F':
            app_at_recv_cmd_F(data[1], &data[2]);
            break;
#if PBAP_FUC == 1
        case 'P':
            app_at_recv_cmd_P(data[1],&data[2]);
            break;
#endif
        #if FIXED_FREQ_TX_TEST
        case 'T':
            app_at_recv_cmd_T(data[1], &data[2]);
            break;

        #endif
        default:
            break;
    }
}

#define __RAM_CODE          __attribute__((section("ram_code")))
__RAM_CODE static void app_at_recv_c(uint8_t c)
{
    switch(at_recv_state)
    {
        case 0:
            if(c == 'A')
            {
                at_recv_state++;
            }
            break;
        case 1:
            if(c == 'T')
                at_recv_state++;
            else
                at_recv_state = 0;
            break;
        case 2:
            if(c == '#')
                at_recv_state++;
            else
                at_recv_state = 0;
            break;
        case 3:
            at_recv_buffer[at_recv_index++] = c;
            if((c == '\n')
               ||(at_recv_index >= AT_RECV_MAX_LEN))
            {
                os_event_t at_cmd_event;
                at_cmd_event.event_id = USER_EVT_AT_COMMAND;///event id
                at_cmd_event.param = at_recv_buffer;        ///���ڽ���buffer
                at_cmd_event.param_len = at_recv_index;     ///���ڽ���buffer����
                os_msg_post(user_task_id, &at_cmd_event);   ///������Ϣ
                at_recv_state = 0;
                at_recv_index = 0;
            }
            break;
    }
}

__attribute__((section("ram_code"))) void app_at_uart_recv(void*dummy, uint8_t status)
{
    app_at_recv_c(app_at_recv_char);
    uart0_read_for_hci(&app_at_recv_char, 1, app_at_uart_recv, NULL);
}

#if COPROCESSOR_UART_ENABLE
__attribute__((section("ram_code"))) void app_at_init_app(void)
{
    system_set_port_pull_up(GPIO_PA6, true);
    system_regs->reset_ctrl.cm3_uart_sft_rst = 1;
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_6, PORTA6_FUNC_CM3_UART_RXD);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_7, PORTA7_FUNC_CM3_UART_TXD);
    
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_6, PORTB6_FUNC_B6);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_7, PORTB7_FUNC_B7);
    uart_init(BAUD_RATE_115200);
    //uart0_read_for_hci(&app_at_recv_char, 1, app_at_uart_recv, NULL);
}
#elif USB_DEVICE_ENABLE 
__attribute__((section("ram_code"))) void app_at_init_app(void)
{
    system_set_port_pull_up(GPIO_PA6, true);
    system_regs->reset_ctrl.cm3_uart_sft_rst = 1;
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_6, PORTA6_FUNC_CM3_UART_RXD);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_7, PORTA7_FUNC_CM3_UART_TXD);
    
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_6, PORTB6_FUNC_B6);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_7, PORTB7_FUNC_B7);
    uart_init(BAUD_RATE_115200);
    uart0_read_for_hci(&app_at_recv_char, 1, app_at_uart_recv, NULL);
}

#else

__attribute__((section("ram_code"))) void app_at_init_app(void)
{
//    uart_putc_noint('A');
    //printf("app_at init app\r\n");
    system_set_port_pull_up(GPIO_PB6, true);
    system_regs->reset_ctrl.cm3_uart_sft_rst = 1;
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_6, PORTB6_FUNC_CM3_UART_RXD);
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_7, PORTB7_FUNC_CM3_UART_TXD);
    
    //uart_putc_noint('B');
#if FIXED_FREQ_TX_TEST|BLE_TEST_MODE
    uart_init(BAUD_RATE_115200);
#else
    uart_init(BAUD_RATE_115200);
#endif
    //uart_putc_noint('C');
#if !BLE_TEST_MODE
    uart0_read_for_hci(&app_at_recv_char, 1, app_at_uart_recv, NULL);
#endif
    //printf("after app_at init app\r\n");
    //uart_putc_noint('D');
}
#endif


#if COPROCESSOR_UART_ENABLE

#if 0
void uart_slave_wake_timer_func(void *arg)
{
    struct uart_slave_msg_elem_t *elem;
    LOG_INFO("uart_slave_wake_timer_func\r\n");
    if(uart_slave_env.state == UART_SLEEP_WAKING_STATE){
        uart_slave_env.state = UART_SLEEP_STATE;
        system_sleep_enable();
        while(!co_list_is_empty(&uart_slave_env.msg_list)){
            elem = (struct uart_slave_msg_elem_t *)co_list_pop_front(&uart_slave_env.msg_list);
            os_free((void *)elem->data);
            os_free((void *)elem);
        }
        uart_slave_env.msg_total_num = 0;
    }
}

void uart_slave_check_timer_func(void *arg)
{
    LOG_INFO("uart_slave_check_timer_func\r\n");
    if(uart_slave_env.state == UART_WORK_STATE){
        uart_write("+SLEEP\r\n",sizeof("+SLEEP\r\n"));
        co_delay_100us(10);
        uart_slave_env.state = UART_SLEEP_STATE;
        system_sleep_enable();
    }
}

void uart_slave_send_wake_ind(void)
{
    struct uart_slave_msg_elem_t *elem;
    LOG_INFO("uart_slave_send_wake_ind,%d\r\n",uart_slave_env.state);
    if(uart_slave_env.state == UART_SLEEP_WAKING_STATE){
        os_timer_stop(&uart_slave_wake_timer);
        uart_slave_env.state = UART_WORK_STATE;
        os_timer_start(&uart_slave_check_timer,5000,false);
        while(!co_list_is_empty(&uart_slave_env.msg_list)){
            elem = (struct uart_slave_msg_elem_t *)co_list_pop_front(&uart_slave_env.msg_list);
            uart_write(elem->data,elem->data_len);
            os_free((void *)elem->data);
            os_free((void *)elem);
            uart_slave_env.msg_total_num --;
        }
    }
    if(uart_slave_env.state != UART_WORK_STATE){
        system_sleep_disable();
        uart_write("+WAKE\r\n",sizeof("+WAKE\r\n"));
        uart_slave_env.state = UART_WORK_STATE;
        os_timer_start(&uart_slave_check_timer,5000,false);
    }

}

void uart_slave_init(void)
{
    //pmu_set_pin_to_PMU(UART_SLAVE_WAKE_PIN_PORT, 1<<UART_SLAVE_WAKE_PIN_BIT);
    //pmu_set_pin_dir(UART_SLAVE_WAKE_PIN_PORT, 1<<UART_SLAVE_WAKE_PIN_BIT, GPIO_DIR_IN);

    
    pmu_set_pin_to_PMU(UART_SLAVE_IND_PIN_PORT, 1<<UART_SLAVE_IND_PIN_BIT);
    pmu_set_pin_dir(UART_SLAVE_IND_PIN_PORT, 1<<UART_SLAVE_IND_PIN_BIT, GPIO_DIR_OUT);
    pmu_set_gpio_value(UART_SLAVE_IND_PIN_PORT, 1<<UART_SLAVE_IND_PIN_BIT,1);

    os_timer_init(&uart_slave_wake_timer,uart_slave_wake_timer_func,NULL);
    co_list_init(&uart_slave_env.msg_list);
    
    uart_slave_env.state = UART_WORK_STATE;
    //system_sleep_disable();
    //os_timer_init(&uart_slave_gpio_debounce_timer,uart_slave_gpio_debounce_timer_func,NULL);
    os_timer_init(&uart_slave_check_timer,uart_slave_check_timer_func,NULL);
    os_timer_start(&uart_slave_check_timer,5000,false);
    
}

void uart_send_to_master(uint8_t *data, uint8_t len)
{
    struct uart_slave_msg_elem_t *elem;
    if(uart_slave_env.state == UART_WORK_STATE){
        uart_write(data,len);
        os_timer_start(&uart_slave_check_timer,5000,false);
    }
    else if(uart_slave_env.state == UART_SLEEP_STATE){
        system_sleep_disable();
        uart_slave_env.state = UART_SLEEP_WAKING_STATE;
        elem = (struct uart_slave_msg_elem_t *)os_malloc(sizeof(struct uart_slave_msg_elem_t));
        elem->data_len = len;
        elem->data = (uint8_t *)os_malloc(len);
        memcpy(elem->data,data,len);
        co_list_push_back(&uart_slave_env.msg_list, &elem->hdr);
        pmu_set_gpio_value(UART_SLAVE_IND_PIN_PORT, 1<<UART_SLAVE_IND_PIN_BIT, 0);
        os_timer_start(&uart_slave_wake_timer,500,false);
    }
    else if(uart_slave_env.state == UART_SLEEP_WAKING_STATE){
        elem->data_len = len;
        elem->data = (uint8_t *)os_malloc(len);
        memcpy(elem->data,data,len);
        co_list_push_back(&uart_slave_env.msg_list, &elem->hdr);
        os_timer_start(&uart_slave_wake_timer,500,false);
    }
    else{
        ///shutoff state,shall not be here
        //printf("error:data can't send in shutoff state.\r\n");
    }
}
#endif

uint8_t uart_recv[512];

void uart_slave_init(void)
{
    //enable pc4 as wakeup pin
    button_init(GPIO_PC4);
    pmu_set_pin_pull_up(GPIO_PORT_C, 1<<GPIO_BIT_4,true);
    pmu_port_wakeup_func_set(GPIO_PC4);
    
    pmu_set_pin_to_PMU(UART_SLAVE_IND_PIN_PORT, 1<<UART_SLAVE_IND_PIN_BIT);
    pmu_set_pin_dir(UART_SLAVE_IND_PIN_PORT, 1<<UART_SLAVE_IND_PIN_BIT, GPIO_DIR_OUT);
    pmu_set_gpio_value(UART_SLAVE_IND_PIN_PORT, 1<<UART_SLAVE_IND_PIN_BIT,1);

    uart_lp_env.rx_state = UART_LP_RX_STATE_IDLE;
    
}

void uart_recv_callback(void*dummy, uint8_t status)
{
//    struct uart_msg_elem_t *elem = NULL;
    os_event_t uart_tx_ready_event;
//    uint8_t rx_size = 0;
    uint8_t rx_state = uart_lp_env.rx_state;
    //printf("5080 rx:%d,%c,%c\r\n",rx_state,uart_recv[0],uart_recv[6]);
    
    //uart_putc_noint(rx_state);
    switch(rx_state)
    {
        case UART_LP_RX_STATE_IDLE:
            if(memcmp(&uart_recv[0],"AA#OK\r\n",7) == 0){
                ///recv mcu ack,post msg,send buffer data in task
                uart_enable_isr(0, 0);
                pmu_set_gpio_value(UART_SLAVE_IND_PIN_PORT, 1<<UART_SLAVE_IND_PIN_BIT,1);
                uart_tx_ready_event.event_id = USER_EVT_UART_TX_READY;
                uart_tx_ready_event.param = NULL;        
                uart_tx_ready_event.param_len = 0;     
                os_msg_post(user_task_id, &uart_tx_ready_event); 
            }
            break;
        case UART_LP_RX_STATE_HEADER:
            if(memcmp(&uart_recv[0],"ZZ#",3) == 0){
                ///recv mcu data,5 bytes header first
                uart_lp_env.rx_size = ascii_strn2val((const char*)&uart_recv[3],16,4);
                //uart_lp_env.rx_size = (uart_recv[3<<8)|uart_recv[4];
                uart_lp_env.rx_state = UART_LP_RX_STATE_DATA;
                uart_enable_isr(1, 0);
                uart0_read_for_hci(&uart_recv[0], uart_lp_env.rx_size, uart_recv_callback, NULL);
            }
            break;
        case UART_LP_RX_STATE_DATA:
            os_event_t at_cmd_event;
            
            at_cmd_event.event_id = USER_EVT_AT_COMMAND;
            at_cmd_event.param = &uart_recv[3]; //memcpy here? TBD
            at_cmd_event.param_len = uart_lp_env.rx_size-3;
            os_msg_post(user_task_id, &at_cmd_event);

            uart_lp_env.rx_state = UART_LP_RX_STATE_IDLE;
            uart_enable_isr(0,0);
            system_sleep_enable();
            break;
        default:
            break;
    }
    
}

void uart_send_ack(void)
{
    uart_write("AA#OK\r\n",7);
    uart_enable_isr(1, 0);
    uart_lp_env.rx_state = UART_LP_RX_STATE_HEADER;
    uart0_read_for_hci(&uart_recv[0], 7, uart_recv_callback, NULL);
}

void uart_req_tx(void)
{
    pmu_set_gpio_value(UART_SLAVE_IND_PIN_PORT, 1<<UART_SLAVE_IND_PIN_BIT,0);
    uart_enable_isr(1, 0);
    uart_lp_env.rx_state = UART_LP_RX_STATE_IDLE;
    uart0_read_for_hci(&uart_recv[0], 7, uart_recv_callback, NULL);
}

void uart_send(uint8_t *data, uint16_t data_len)
{
    struct uart_msg_elem_t *elem;
    elem = (struct uart_msg_elem_t*)os_malloc(sizeof(struct uart_msg_elem_t));
    elem->data_len = data_len;
    elem->data = (uint8_t *)os_malloc(data_len);
    memcpy(elem->data,data,data_len);
    GLOBAL_INT_DISABLE();
    co_list_push_back(&uart_lp_env.msg_list, &elem->hdr);
    uart_lp_env.pending_num ++;
    GLOBAL_INT_RESTORE();
    if((uart_lp_env.tx_sending == false)&&(uart_lp_env.rx_pending == false)){
        uart_lp_env.tx_sending = true;
        //uart_putc_noint('T');
        uart_req_tx();
    }
}

__attribute__((section("ram_code"))) void pmu_gpio_isr_ram(void)
{
    uint32_t gpio_value = ool_read32(PMU_REG_PORTA_INPUT_VAL);
    
    //printf("gpio new value = 0x%08x.\r\n", gpio_value);
    if((gpio_value&GPIO_PC4) == 0){
        if(uart_lp_env.tx_sending == true){
            uart_lp_env.rx_pending = true;
        }else{
            //ack mcu,enable uart rx isr, set recv threshold to 5(recv header)
            system_sleep_disable();
            uart_send_ack();
        }
    }
    ool_write32(PMU_REG_STATE_MON_IN_A,gpio_value);

}
#else
void uart_send(uint8_t *data, uint16_t data_len)
{
    for(uint16_t i = 0; i < data_len; i++)
    {
        uart_putc_noint(data[i]);
    }
}

#endif

typedef void (*rwip_eif_callback) (void*, uint8_t);

struct uart_txrxchannel
{
    /// call back function pointer
    rwip_eif_callback callback;
};

struct uart_env_tag
{
    /// rx channel
    struct uart_txrxchannel rx;
    uint32_t rxsize;
    uint8_t *rxbufptr;
    void *dummy;
    /// error detect
    uint8_t errordetect;
    /// external wakeup
    bool ext_wakeup;
};

#if COPROCESSOR_UART_ENABLE
__attribute__((section("ram_code"))) void uart_isr_ram(void)
{
    uint8_t int_id;
    uint8_t c;
    rwip_eif_callback callback;
    void *dummy;
    volatile struct uart_reg_t *uart_reg = (volatile struct uart_reg_t *)UART_BASE;
    struct uart_env_tag *uart1_env = (struct uart_env_tag *)0x200015bc;
    int_id = uart_reg->u3.iir.int_id;
    if(int_id == 0x04 || int_id == 0x0c )   /* Receiver data available or Character time-out indication */
    {
        while(uart_reg->lsr & 0x01)
        {
            c = uart_reg->u1.data;
            *uart1_env->rxbufptr++ = c;
            uart1_env->rxsize--;
            
            //uart_putc_noint(uart1_env->rxsize);
            if((uart1_env->rxsize == 0)
               &&(uart1_env->rx.callback))
            {
                uart_reg->u3.fcr.data = 0xf1;
                NVIC_DisableIRQ(UART_IRQn);
                uart_reg->u3.fcr.data = 0x21;
                callback = uart1_env->rx.callback;
                dummy = uart1_env->dummy;
                uart1_env->rx.callback = 0;
                uart1_env->rxbufptr = 0;
                callback(dummy, 0);
                break;
            }

        }
    }else if(int_id == 0x02){
        //tx empty
        
    }
}
#else
__attribute__((section("ram_code"))) void uart_isr_ram(void)
{
    uint8_t int_id;
    uint8_t c;
    rwip_eif_callback callback;
    void *dummy;
    volatile struct uart_reg_t *uart_reg = (volatile struct uart_reg_t *)UART_BASE;
    struct uart_env_tag *uart1_env = (struct uart_env_tag *)0x200015bc;

    int_id = uart_reg->u3.iir.int_id;
    //uart_putc_noint('#');
    if(int_id == 0x04 || int_id == 0x0c )   /* Receiver data available or Character time-out indication */
    {
        while(uart_reg->lsr & 0x01)
        {
            c = uart_reg->u1.data;
            *uart1_env->rxbufptr++ = c;
            uart1_env->rxsize--;
            if((uart1_env->rxsize == 0)
               &&(uart1_env->rx.callback))
            {
                uart_reg->u3.fcr.data = 0xf1;
                NVIC_DisableIRQ(UART_IRQn);
                uart_reg->u3.fcr.data = 0x21;
                callback = uart1_env->rx.callback;
                dummy = uart1_env->dummy;
                uart1_env->rx.callback = 0;
                uart1_env->rxbufptr = 0;
                callback(dummy, 0);
                break;
            }
        }
    }
    else if(int_id == 0x06)
    {
        volatile uint32_t line_status = uart_reg->lsr;
    }
}
#endif
