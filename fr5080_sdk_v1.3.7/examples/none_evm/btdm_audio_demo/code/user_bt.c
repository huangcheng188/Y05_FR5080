#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "os_timer.h"
#include "os_mem.h"
#include "pskeys.h"
#include "co_log.h"

#include "driver_plf.h"
#include "driver_ipc.h"
#include "driver_gpio.h"
#include "driver_pdm.h"
#include "driver_i2s.h"
#include "driver_pmu.h"
#include "driver_uart.h"
#include "driver_codec.h"
#include "driver_syscntl.h"

#include "bt_api.h"
#include "a2dp_api.h"
#include "hf_api.h"
#include "avrcp_api.h"
#include "spp_api.h"
#include "pbap_api.h"

#include "user_bt.h"
#include "user_dsp.h"
#include "user_task.h"
#include "audio_source.h"
#include "native_playback.h"
#include "app_at.h"

#define BT_RECONNECT_TO     1500

struct user_bt_env_tag *user_bt_env = (struct user_bt_env_tag *)0x20004c1c;

uint8_t pbap_addr_index = 0;
os_timer_t bt_reconnect_timer;
static os_timer_t bt_linkloss_timer;
static os_timer_t bt_avrcp_timer;
os_timer_t bt_delay_reconnect_timer;

uint8_t ipc_mp3_start_flag = 0;
uint8_t delay_reconnect_flag = false;
extern SbcStreamInfo sbcinfo;

uint8_t do_disconnect = false;
uint8_t bt_connect_stage = 0;//0---no connect,1---acl connect,2---profile connect
//bool bt_nrec_wait_dsp_open = false;

extern os_timer_t audio_codec_timer;
extern uint8_t dsp_nrec_start_flag;
extern uint8_t bt_change_a2dp_flag;

enum user_bt_state_t
{
    USER_BT_STATE_IDLE,
    USER_BT_STATE_NATIVE_PLAYBACK,
    USER_BT_STATE_MIC_ONLY,
    USER_BT_STATE_MIC_LOOP,
    USER_BT_STATE_AUDIO_WAIT_DSP_OPEN,
    USER_BT_STATE_AUDIO_CONNECTED,
    USER_BT_STATE_A2DP_STARTED,
};

static enum user_bt_state_t user_bt_state = USER_BT_STATE_IDLE;
static enum user_bt_state_t user_bt_saved_state = USER_BT_STATE_IDLE;
os_timer_t bt_delay_resume_local_timer;
#if PBAP_FUC == 1
extern uint8_t pbap_connect_flag;
#endif

uint8_t user_evt_notify_enable = 0xff;


void audio_codec_timer_func(void *arg);
uint8_t get_bt_reconnect_state(void);
void set_bt_reconnect_state(uint8_t flag);

void bt_delay_resume_local_timer_func(void *arg)
{
    if(user_bt_state == USER_BT_STATE_IDLE){
        if(user_bt_saved_state == USER_BT_STATE_NATIVE_PLAYBACK){
            if(true == native_playback_action_play()){
                user_bt_state = USER_BT_STATE_NATIVE_PLAYBACK;
            }
            user_bt_saved_state = USER_BT_STATE_IDLE;
        }else if(user_bt_saved_state == USER_BT_STATE_MIC_ONLY){
            audio_codec_func_start(AUDIO_CODEC_FUNC_MIC_ONLY);
            user_bt_state = USER_BT_STATE_MIC_ONLY;
            user_bt_saved_state = USER_BT_STATE_IDLE;
        }
    }else{
        user_bt_saved_state = USER_BT_STATE_IDLE;
    }
}

void bt_set_user_evt_notify_enable(uint8_t flag)
{
    user_evt_notify_enable = flag;
}

uint8_t bt_find_device(BD_ADDR *bdAddrP)
{
    bool searchingDevices;
    uint8_t   devNdx;

    devNdx = 0;
    do {
        searchingDevices = FALSE;
        if (NUM_DEVICES > devNdx) {
            if (0 != memcmp(bdAddrP, &user_bt_env->dev[devNdx].bd,6)) {
                searchingDevices = TRUE;
                devNdx += 1;
            }
        }
    } while(TRUE == searchingDevices);

    return(devNdx);

}

uint8_t bt_find_free_dev_index()
{
    uint8_t i;
    for(i=0;i<NUM_DEVICES;i++)
    {
        if(user_bt_env->dev[i].inUse == 0){
            break;
        }
    }
    return i;
}

static void bt_avrcp_connect_timer_func(void *arg)
{
    if((user_bt_env->dev[user_bt_env->last_active_dev_index].conFlags&LINK_STATUS_AVC_CONNECTED) == 0){
        if(user_bt_env->dev[user_bt_env->last_active_dev_index].conFlags&LINK_STATUS_AV_CONNECTED){
            avrcp_connect(&user_bt_env->dev[user_bt_env->last_active_dev_index].bd);
        }
    }
    os_timer_destroy(&bt_avrcp_timer);
}

static void bt_reconnect_timer_func(void *arg)
{
    if(false ==bt_reconnect((enum bt_reconnect_type_t)user_bt_env->dev[0].reconnecting,&user_bt_env->dev[0].bd)){
        bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
        LOG_INFO("reconnect fail\r\n");
    }
}

static void bt_linkloss_timer_func(void *arg)
{
    if(false ==bt_reconnect(RECONNECT_TYPE_LINK_LOSS,&user_bt_env->dev[0].bd)){
        bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
        LOG_INFO("reconnect fail\r\n");
    }
}

/* do BT reconnect according which profile is enabled */
static BtStatus reconnect_according_to_profile(BD_ADDR *bdaddr)
{
    BtStatus ret = BT_STATUS_SUCCESS;
    //printf("reconnect profile,%x,%x,%d\r\n",bdaddr->A[0],bdaddr->A[1],user_bt_env->dev[0].reconnect_times);
    //bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE, NULL);
    //uart_putc_noint('$');
    if(pskeys.enable_profiles & ENABLE_PROFILE_HF) {
        ret = hf_connect(bdaddr);
    }
    else if(pskeys.enable_profiles & ENABLE_PROFILE_A2DP) {
        if(bt_get_a2dp_type() == BT_A2DP_TYPE_SRC){
            ret = a2dp_open_stream(AVDTP_STRM_ENDPOINT_SNK, bdaddr);
        }else{
            ret = a2dp_open_stream(AVDTP_STRM_ENDPOINT_SRC, bdaddr);
        }
    }

    return ret;
}

static void bt_delay_reconnect_timer_func(void *arg)
{
    //uart_putc_noint('=');
    bt_reset_controller();
    if(user_bt_env->dev[0].reconnecting > 0){
        reconnect_according_to_profile(&(user_bt_env->dev[0].bd));
    }else{
        //uart_putc_noint('+');
        set_bt_reconnect_state(0);
        bt_reconnect(RECONNECT_TYPE_POWER_ON,NULL);

    }
}

void bt_hf_evt_func(hf_event_t *event)
{
    uint8_t i = 0;
    uint8_t deviceNum = NUM_DEVICES;
    const char *callerid_ptr;
    uint8_t *buf,volume;
    uint8_t send_buf[25];
    ///查询本地设备列表中，改地址对应的设备序�?
    deviceNum = bt_find_device(event->addr);
    //printf("hf func:event=%d\r\n",event->type);
    switch(event->type){
        ///收到AG发起的HFP连接请求
        case HF_EVENT_SERVICE_CONNECT_REQ:
            ///将responder标志位置TRUE
            user_bt_env->dev[deviceNum].responder = TRUE;
        break;
        ///HFP服务层连接建立完�?
        case HF_EVENT_SERVICE_CONNECTED:
        {
            LOG_INFO("hf profile connected,devnum=%d\r\n",deviceNum);
            ///保存HFP连接信息
            user_bt_env->dev[deviceNum].conFlags |= LINK_STATUS_HF_CONNECTED;
            user_bt_env->dev[deviceNum].hf_chan = event->chan;
            bt_connect_stage  = 2;
            ///若responder标志位没有被置起，表示是HF端主动连接，此时需要根据pskeys及状态看是否需要建立A2DP连接
            if((user_bt_env->dev[deviceNum].responder == FALSE)
                &&((user_bt_env->dev[deviceNum].conFlags&LINK_STATUS_AV_CONNECTED)==0)
                &&(pskeys.enable_profiles & ENABLE_PROFILE_A2DP)
                &&(bt_a2dp_get_connecting_state() == FALSE)){
                //printf("a2dp open stream\r\n");
                a2dp_open_stream(AVDTP_STRM_ENDPOINT_SRC, event->addr);
            }

            ///使能来电显示
            hf_enable_caller_id_notify(event->chan, TRUE);
            //printf("status= %d\r\n",status);

            ///使能三方通话
            hf_enable_call_wait_notify(event->chan, TRUE);

            ///不使能远端NREC算法
            hf_disable_nrec(user_bt_env->dev[deviceNum].hf_chan);

            ///使能耳机电池电量显示
            buf = (uint8_t *)os_malloc(sizeof("AT+XAPL=AAAA-1111_01,10"));
            if(buf != NULL) {
                memcpy(buf, "AT+XAPL=AAAA-1111_01,10", sizeof("AT+XAPL=AAAA-1111_01,10"));
                hf_send_at_command(event->chan, (const char *)buf);
            }

            ///查询本地所有连接是否都已成功连接上，若是，则保存连接信息到flash
            bt_check_conn(deviceNum);
        }
        break;
        ///HFP服务层连接断开
        case HF_EVENT_SERVICE_DISCONNECTED:
        {
            LOG_INFO("hf profile disconnected,devnum=%d,err=%d\r\n",deviceNum,event->errCode);
            ///保存相应信息到本�?
            user_bt_env->dev[deviceNum].responder = FALSE;
            user_bt_env->dev[deviceNum].conFlags &= ~LINK_STATUS_HF_CONNECTED;
            user_bt_env->dev[deviceNum].hf_chan = NULL;
            user_bt_env->dev[deviceNum].prf_all_connected = false;
        }
        break;
        ///HFP音频连接成功建立
        case HF_EVENT_AUDIO_CONNECTED:
        {
            ///置位相应状�?
            user_bt_env->dev[deviceNum].conFlags |= LINK_STATUS_SCO_CONNECTED;
            user_bt_env->last_active_dev_index = deviceNum;

            codec_power_off();//avoid calling in playing music, spk output noise when dsp is not ready
            system_sleep_disable();
            bt_statemachine(USER_EVT_HFP_AUDIO_CONNECTED, NULL);
            #if 0
            if(dsp_open(DSP_LOAD_TYPE_VOICE_ALGO) == DSP_OPEN_SUCCESS) {
                dsp_nrec_start_flag = true;
                dsp_working_label_set(DSP_WORKING_LABEL_VOICE_ALGO);
                os_timer_init(&audio_codec_timer,audio_codec_timer_func,NULL);
                os_timer_start(&audio_codec_timer,300,0);
                ipc_msg_send((enum ipc_msg_type_t)IPC_MSG_WITHOUT_PAYLOAD, IPC_SUB_MSG_NREC_START, NULL);
            }
            else {
                // DSP_OPEN_PENDING
                bt_nrec_wait_dsp_open = true;
            }
            #endif
            if(ipc_get_mic_type() == IPC_MIC_TYPE_PDM){
                pdm_init(0);
                pdm_init(1);
                gpio_set_pin_value(GPIO_PORT_B, GPIO_BIT_0,1);
            }

            uart_send("+AC\r\n",5);
        }
        break;
        ///HFP音频连接断开
        case HF_EVENT_AUDIO_DISCONNECTED:
        {
            ///置位相应状�?
            user_bt_env->dev[deviceNum].conFlags &= ~LINK_STATUS_SCO_CONNECTED;
            bt_statemachine(USER_EVT_HFP_AUDIO_DISCONNECTED, NULL);

            if(ipc_get_mic_type() == IPC_MIC_TYPE_PDM){
                gpio_set_pin_value(GPIO_PORT_B, GPIO_BIT_0,0);
                pdm_stop(0);
                pdm_stop(1);
            }
            system_sleep_enable();

            uart_send("+AD\r\n",5);
        }
        break;

        ///HFP通话状态上�?
        case HF_EVENT_CALL_IND:
            user_bt_env->dev[deviceNum].active  = event->param.call;
            break;
        ///HFP通话建立状态上�?
        case HF_EVENT_CALLSETUP_IND:
            user_bt_env->dev[deviceNum].setup_state = event->param.callSetup;
            break;
        ///来电号码提示
        case HF_EVENT_CALLER_ID_NOTIFY:
        ///log打印来电号码
            //printf("+CLI:");
            
            callerid_ptr = event->param.callerIdParms->number;
            i = 0;
            while(callerid_ptr[i] != '\0'){
                //printf("%c",callerid_ptr[i]);
                //send_buf[7+i] = callerid_ptr[i];
                i++;
            }
            co_sprintf((void *)send_buf,"+CLI:%02x,",i+2);
            i = 0;
            while(callerid_ptr[i] != '\0'){
                send_buf[8+i] = callerid_ptr[i];
                i++;
            }
            send_buf[8+i] = '\r';
            send_buf[8+i+1] = '\n';
            
            uart_send(send_buf,i+8);
            break;
        case HF_EVENT_RING_IND:
            uart_send("+RING\r\n",7);
            break;
        case HF_EVENT_SPEAKER_VOLUME:
            
            if(user_evt_notify_enable&USER_EVT_NOTIFY_VOLUME){
                co_sprintf((void *)send_buf,"+VOL:%02x\r\n",event->param.gain);
                uart_send(send_buf,9);
            }
            if(event->param.gain == 0){
            volume = 1;
            }else{
            volume = event->param.gain * 63/15;
            }
            REG_PL_WR(0x50022000+(0xd6<<2),volume);
            REG_PL_WR(0x50022000+(0xd7<<2),volume);
            break;

        ///HFP AT命令发送完�?
        case HF_EVENT_COMMAND_COMPLETE:
        ///检测是否是自定义AT命令发送完�?
            if(event->param.command->type == HF_COMMAND_SEND_AT_COMMAND){
             if(memcmp((uint8_t *)event->param.command->parms[0], "AT+XAPL=AAAA-1111_01,10", sizeof("AT+XAPL=AAAA-1111_01,10"))==0){
                 buf = (uint8_t *)os_malloc(24);
                 if(buf != NULL) {
                     ///显示电量
                     memcpy(buf, "AT+IPHONEACCEV=1,1,9", 24);
                     hf_send_at_command(event->chan, (const char *)buf);
                 }
             }
             ///注意释放发送自定义At命令时所分配的内�?
             os_free((uint8 *)event->param.command->parms[0]);
            }
            break;

        default:
            break;
    }
}

void bt_a2dp_evt_func(a2dp_event_t *event)
{
    uint8_t deviceNum = NUM_DEVICES;
    uint8_t error = A2DP_ERR_NO_ERROR;
    AvdtpCodec       *codec;
    uint8_t               *elements;
    uint8_t               *reqElements;
    //查询本地设备列表中，该地址对应的设备序�?
    deviceNum = bt_find_device(event->addr);
    if(deviceNum >= NUM_DEVICES){
        LOG_INFO("av func: event=%d,%x,%x,erorr device num.....\r\n",event->event,event->addr->A[0],user_bt_env->dev[0].bd.A[0]);
        deviceNum = 0;
        //return;

    }
    //printf("av func:%d\r\n",event->event);
    switch(event->event){
        ///收到远端的a2dp连接请求
        case A2DP_EVENT_STREAM_OPEN_IND:
            ///远端请求codec类型为sbc
            if (AVDTP_CODEC_TYPE_SBC == event->codec->codecType) {
                ///获取当前stream注册的codec类型
                codec = a2dp_get_registered_codec(event->stream);
                elements = codec->elements;
                reqElements = event->codec->elements;

                ///判断请求的参数是否与本地注册的参数冲�?
                if (!(reqElements[0] & (elements[0] & 0xF0))) {
                    error = A2DP_ERR_NOT_SUPPORTED_SAMP_FREQ;
                } else if (!(reqElements[0] & (elements[0] & 0x0F))) {
                    error = A2DP_ERR_NOT_SUPPORTED_CHANNEL_MODE;
                } else if (!(reqElements[1] & (elements[1] & 0x0C))) {
                    error = A2DP_ERR_NOT_SUPPORTED_SUBBANDS;
                } else if (!(reqElements[1] & (elements[1] & 0x03))) {
                    error = A2DP_ERR_NOT_SUPPORTED_ALLOC_METHOD;
                } else if (reqElements[2] < elements[2]) {
                    error = A2DP_ERR_NOT_SUPPORTED_MIN_BITPOOL_VALUE;
                } else if (reqElements[3] > elements[3]) {
                    error = A2DP_ERR_NOT_SUPPORTED_MAX_BITPOOL_VALUE;
                }
                ///保存请求的sbc的采样率
                codec->freq = (reqElements[0] & 0xF0) >> 5;
                ///回复response给远�?
                a2dp_open_stream_rsp(event->stream, error, 0);
            }
            ///远端请求codec类型为aac
            else if(AVDTP_CODEC_TYPE_MPEG2_4_AAC== event->codec->codecType) {
                ///获取当前stream注册的codec类型
                codec = a2dp_get_registered_codec(event->stream);
                elements = codec->elements;
                reqElements = event->codec->elements;

                ///保存请求的sbc的采样率
                if(reqElements[1] & 0x01) {
                    ///44.1k
                    codec->freq = 1;
                }
                else if(reqElements[2] & 0x80) {
                    ///48k
                    codec->freq = 0;
                }
                ///回复response给远�?
                a2dp_open_stream_rsp(event->stream, error, 0);
            }
            else {
                ///不支持的codec类型，拒绝连接请�?
                a2dp_open_stream_rsp(event->stream, AVRCP_ERR_UNKNOWN_ERROR, 0);
            }
            break;

        ///A2DP streamԉ٦ղߪ
        case A2DP_EVENT_STREAM_OPEN:
            ///保存状态信息及stream到本�?
            user_bt_env->dev[deviceNum].conFlags |= LINK_STATUS_AV_CONNECTED;
            user_bt_env->dev[deviceNum].pstream = event->stream;
            bt_connect_stage  = 2;

            ///查询本地所有连接是否都已成功连接上，若是，则保存连接信息到flash
            bt_check_conn(deviceNum);
            ///若AVRCP连接还没有建立，发起avrcp连接
            if((user_bt_env->dev[deviceNum].conFlags&LINK_STATUS_AVC_CONNECTED) == 0){
                os_timer_init(&bt_avrcp_timer,bt_avrcp_connect_timer_func,NULL);
                os_timer_start(&bt_avrcp_timer,500,false);
                //avrcp_connect(event->addr);
            }
            /// audio source,配置dsp的bitpool
            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SRC){
                sbcinfo.bitPool = bt_a2dp_get_sbc_bitpool();
            }

            LOG_INFO("av profile connected\r\n");
            break;
        ///A2DP streamژҕ
        case A2DP_EVENT_STREAM_CLOSED:
            ///保存状态信息到本地
            LOG_INFO("av profile disconnected\r\n");
            user_bt_env->dev[deviceNum].conFlags &= (~LINK_STATUS_AV_CONNECTED);
            user_bt_env->dev[deviceNum].pstream = NULL;
            user_bt_env->dev[deviceNum].prf_all_connected = false;
            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SRC){
                i2s_stop_();
                NVIC_DisableIRQ(I2S_IRQn);
                dsp_working_label_clear(DSP_WORKING_LABEL_AUDIO_SOURCE);
                audio_source_env.a2dp_stream_start_flag = false;
            }
            break;
        ///A2DP streamࠕ�?
        case A2DP_EVENT_STREAM_IDLE:
            ///保存状态信息到本地
            user_bt_env->dev[deviceNum].conFlags &= (~LINK_STATUS_AV_CONNECTED);
            user_bt_env->dev[deviceNum].pstream = NULL;
            user_bt_env->dev[deviceNum].prf_all_connected = false;
            break;
        ///A2DP stream ߪʼԫˤȫȳ
        case A2DP_EVENT_STREAM_START_IND:
            ///ܘشresponse
            a2dp_start_stream_rsp(user_bt_env->dev[deviceNum].pstream , A2DP_ERR_NO_ERROR);
            break;
        ///A2DP streamߪʼԫˤ
        case A2DP_EVENT_STREAM_STARTED:
            LOG_INFO("a2dp started\r\n");
             ///保存状态信息到本地
            //printf("a2dp started\r\n");
            user_bt_env->current_playing_index = deviceNum;
            user_bt_env->dev[deviceNum].conFlags |= LINK_STATUS_MEDIA_PLAYING;
            user_bt_env->last_active_dev_index = deviceNum;
            //ipc_set_audio_inout_type(IPC_MIC_TYPE_ANLOG_MIC,IPC_SPK_TYPE_CODEC,IPC_MEDIA_TYPE_BT);
            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SRC){
                audio_source_statemachine(USER_EVT_A2DP_STREAM_STARTED, NULL);

                #if !MP3_SRC_LOCAL
                if(app_audio_play_env.audio_event&AUDIO_EVENT_CONTINUE){
                    app_audio_play_env.audio_play_notify_ack_cb(CONTINUE_PLAY);
                    app_audio_play_env.audio_event& = ~AUDIO_EVENT_CONTINUE;
                }
                #endif
                //avrcp_ct_get_media_info(user_bt_env->dev[deviceNum].rcp_chan, 0x41);
            }else{
                if(user_bt_env->dev[deviceNum].rcp_chan != NULL){
                    avrcp_ct_get_media_info(user_bt_env->dev[deviceNum].rcp_chan, 0x41);
                }
            }

            bt_statemachine(USER_EVT_A2DP_STREAM_STARTED, NULL);
            uart_send("+MS\r\n",5);
            break;
        case A2DP_EVENT_STREAM_SUSPENDED:
            LOG_INFO("a2dp suspended\r\n");
            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SRC){
                audio_source_statemachine(USER_EVT_A2DP_STREAM_SUSPENDED, NULL);
                bt_prevent_sniff_clear(1);
                #if !MP3_SRC_LOCAL
                if(app_audio_play_env.audio_event&AUDIO_EVENT_END){
                    app_audio_play_env.audio_play_complet_ack_cb(PLAY_ALLOWABLE);
                    app_audio_play_env.audio_event& = ~AUDIO_EVENT_END;
                }else if(app_audio_play_env.audio_event&AUDIO_EVENT_PAUSE){
                    app_audio_play_env.audio_play_notify_ack_cb(PAUSE_PLAY);
                    app_audio_play_env.audio_event& = ~AUDIO_EVENT_PAUSE;
                }else if(app_audio_play_env.audio_event&AUDIO_EVENT_SWITCH){
                    app_audio_play_env.audio_play_notify_ack_cb(SWITCH_SONG);
                    app_audio_play_env.audio_event& = ~AUDIO_EVENT_SWITCH;
                }
                #endif
            }
            user_bt_env->dev[deviceNum].conFlags &= (~LINK_STATUS_MEDIA_PLAYING);

            bt_statemachine(USER_EVT_A2DP_STREAM_SUSPENDED, NULL);
            uart_send("+MT\r\n",5);
            break;


        case A2DP_EVENT_STREAM_SBC_PACKET_SENT:
            GLOBAL_INT_DISABLE();
            if(audio_source_env.sbc_pkt_num > 0)
                audio_source_env.sbc_pkt_num--;
            GLOBAL_INT_RESTORE();

            os_free((void *)event->sbcPacket->data);
            os_free((void *)(event->sbcPacket));
            break;
        case A2DP_EVENT_STREAM_DATA_IND:
            break;
        default:
            LOG_WARN("av event %d\r\n",event->event);
            break;
    }
}
void bt_me_evt_func(me_event_t *event)
{
    uint8_t i,k,len = 0;
    uint8_t device_index;
    uint8_t *ptr;
    BtStatus status;
    char inq_buf[128];
    //char temp_buf[9];
    //LOG_INFO("me func = %d\r\n",event->type);
    switch(event->type){
        case BTEVENT_HCI_INITIALIZED:
            //蓝牙已连接情况下，将蓝牙设置成不可发现，不可连接状�?
            bt_set_accessible_mode_c(BAM_NOT_ACCESSIBLE,NULL);

            //蓝牙未连接情况下，将蓝牙设置成可发现，可连接状�?
            #if BLE_TEST_MODE|FIXED_FREQ_TX_TEST
            bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE,NULL);
            #else
            bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
            #endif
#if 0
            if(bt_reconnect(RECONNECT_TYPE_POWER_ON,NULL) == false){
                bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
            }
#endif
            break;

        case BTEVENT_INQUIRY_CANCELED:

            break;
        case BTEVENT_INQUIRY_COMPLETE:
            //搜索完成后，重新搜索附近设备，搜索个数限制为10个，搜索时间限制�?0*1.28s
            bt_start_inquiry(10,10);
            break;
        case BTEVENT_INQUIRY_RESULT:
            //搜索结果打印出来，此处打印的是设备地址，其他参数可参考结构体BtInquiryResult
            #if 0
            printf("+INQ:");
            for(i=0;i<6;i++)
            {
                printf("%02x",event->param.inqResult->bdAddr.A[i]);
            }
            printf(",");
            i = 0;
            ptr = &event->param.inqResult->extInqResp[0];
            while(i<240){
                if(*ptr == 0){
                    i = 240;
                }else{
                    if(*(ptr+1+i) == 0x09){
                        len = *(ptr+i);
                        k = i;
                        i = 240;
                    }else{
                        i += *ptr;
                    }
                }
            }
            for(i=0;i<len-1;i++)
            {
                printf("%c",event->param.inqResult->extInqResp[k+i+2]);
            }
            printf(",rssi=%d\r\n",event->param.inqResult->rssi);
            printf("\r\n");
            #else
            //printf("cod:%x\r\n",event->param.inqResult->classOfDevice);
            if(((event->param.inqResult->classOfDevice)&0xff00) == 0x0400){
                inq_buf[0] = '+';
                inq_buf[1] = 'I';
                inq_buf[2] = 'N';
                inq_buf[3] = 'Q';
                inq_buf[4] = ':';
    
                co_sprintf(&inq_buf[5],"%02x%02x%02x%02x%02x%02x",event->param.inqResult->bdAddr.A[0],event->param.inqResult->bdAddr.A[1],
                    event->param.inqResult->bdAddr.A[2],event->param.inqResult->bdAddr.A[3],event->param.inqResult->bdAddr.A[4],
                    event->param.inqResult->bdAddr.A[5]);
                
                i = 0;
                ptr = &event->param.inqResult->extInqResp[0];
                while(i<240){
                    if(*ptr == 0){
                        i = 240;
                    }else{
                        if(*(ptr+1+i) == 0x09){
                            len = *(ptr+i);
                            k = i;
                            i = 240;
                        }else{
                            i += *ptr;
                        }
                    }
                }
    
                co_sprintf(&inq_buf[17],",rssi=%02d,%02x\r\n",event->param.inqResult->rssi,len+1);
                                        
                for(i=0;i<len-1;i++)
                {
                    inq_buf[31+i] = event->param.inqResult->extInqResp[k+i+2];
                }
                inq_buf[31+len-1] = '\r';
                inq_buf[31+len] = '\n';
                //co_sprintf(&inq_buf[18+len-1],",rssi=%d\r\n",event->param.inqResult->rssi);
                //inq_buf[30+len+1] = '\0';
                uart_send((void *)inq_buf,31+len+1);
            }
            
            #endif
            
            break;

        case BTEVENT_LINK_CONNECT_IND:
            //收到远端发起的连接请�?
            device_index = bt_find_free_dev_index();
            pbap_addr_index = device_index;
            if(device_index == NUM_DEVICES){
                 LOG_WARN("shall not be here,me_callback\r\n");
                return;
            }
            if(event->errCode == BEC_NO_ERROR){
                user_bt_env->dev[device_index].inUse = 1;
                memcpy(&user_bt_env->dev[device_index].bd,event->addr,6);
                user_bt_env->dev[device_index].remDev = event->remDev;
                user_bt_env->dev[device_index].state = BT_CONNECTED;
                user_bt_env->last_active_dev_index = device_index;
                os_timer_stop(&bt_delay_reconnect_timer);
                os_timer_stop(&bt_reconnect_timer);
                user_bt_env->dev[device_index].reconnecting = 0;
                bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
                set_bt_reconnect_state(0);
            }

            break;
        case BTEVENT_LINK_CONNECT_CNF:
            //本地连接请求被确认，是否成功参考errCode
            device_index = bt_find_device(event->addr);
            if(do_disconnect == false){
                bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
            }
            if((device_index == 0)&&((event->errCode == BEC_PAGE_TIMEOUT)
                ||(event->errCode == BEC_CONNECTION_TIMEOUT)
                ||(event->errCode == BEC_USER_TERMINATED)
                ||(event->errCode == BEC_HOST_TIMEOUT))){
                if(do_disconnect == false){
                    os_timer_init(&bt_reconnect_timer,bt_reconnect_timer_func,NULL);
                    os_timer_start(&bt_reconnect_timer,BT_RECONNECT_TO,0);
                }
                else{
                    do_disconnect = false;
                }

            }else if(event->errCode == BEC_NO_ERROR) {
                user_bt_env->dev[device_index].state = BT_CONNECTED;
                user_bt_env->dev[device_index].remDev = event->remDev;
                user_bt_env->dev[device_index].reconnecting = 0;
                user_bt_env->last_active_dev_index = device_index;
                set_bt_reconnect_state(0);
                bt_connect_stage = 1;
                if(do_disconnect == true){
                    bt_remove_acl_link(user_bt_env->dev[device_index].remDev);
                }
            }else{
                memset(&(user_bt_env->dev[device_index]),0,sizeof(user_bt_env->dev[device_index]));
                if(do_disconnect == true){

                    do_disconnect = false;
                }
            }

            break;

        case BTEVENT_LINK_DISCONNECT:
            //连接断开事件，断开原因参考errCode
            device_index = bt_find_device(event->addr);
            memset(&(user_bt_env->dev[device_index]),0,sizeof(APP_DEVICE));
            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SRC){
                NVIC_DisableIRQ(I2S_IRQn);
                i2s_stop_();
                audio_source_env.a2dp_stream_start_flag = false;
            }

            bt_stop_sniff_monitor();
            user_bt_env->dev[device_index].prf_all_connected = false;
            if(event->errCode == BEC_CONNECTION_TIMEOUT){
                if(pskeys.app_data.misc_flag == 0){
                    memcpy(&(user_bt_env->dev[device_index].bd),event->addr,sizeof(BD_ADDR));
                    if(do_disconnect == false){
                        os_timer_init(&bt_linkloss_timer,bt_linkloss_timer_func,NULL);
                        os_timer_start(&bt_linkloss_timer,3000,0);
                    }
                }

                uart_send("+DISC:LL\r\n",10);
            }
            else if(event->errCode == BEC_USER_TERMINATED){
                if(bt_connect_stage == 1){
                    ///only acl connection,no profile connection,do linkloss reconnect
                    if(pskeys.app_data.misc_flag == 0){
                        memcpy(&(user_bt_env->dev[device_index].bd),event->addr,sizeof(BD_ADDR));
                        if(do_disconnect == false){
                            os_timer_init(&bt_linkloss_timer,bt_linkloss_timer_func,NULL);
                            os_timer_start(&bt_linkloss_timer,3000,0);
                        }
                    }
                }
                uart_send("+DISC:UT\r\n",10);
            }else if(event->errCode == BEC_LOCAL_TERMINATED){
                uart_send("+DISC:LT\r\n",10);
            }else{
                if(bt_connect_stage == 1){
                    ///only acl connection,no profile connection,do linkloss reconnect
                    if(pskeys.app_data.misc_flag == 0){
                        memcpy(&(user_bt_env->dev[device_index].bd),event->addr,sizeof(BD_ADDR));
                        if(do_disconnect == false){
                            os_timer_init(&bt_linkloss_timer,bt_linkloss_timer_func,NULL);
                            os_timer_start(&bt_linkloss_timer,3000,0);
                        }
                    }
                }
                uart_send("+DISC:OT\r\n",10);
            }
            bt_statemachine(USER_EVT_CONN_DISCONNECTED, NULL);
            bt_prevent_sniff_clear(0xff);
            bt_connect_stage = 0;
            #if PBAP_FUC
            pbap_connect_flag = 0;
            #endif
            if(do_disconnect == true){
                do_disconnect = false;
            }
            break;
        case BTEVENT_ACCESSIBLE_CHANGE:
            //本地接入模式变化事件上报
            if(event->param.aMode == BAM_GENERAL_ACCESSIBLE){
                user_bt_env->access_state = ACCESS_PAIRING;
                uart_send("+ACC:II\r\n",9);
                //system_sleep_disable();
            }else if(event->param.aMode == BAM_CONNECTABLE_ONLY){
                user_bt_env->access_state = ACCESS_STANDBY;
                uart_send("+ACC:IJ\r\n",9);
            }else if(event->param.aMode == BAM_NOT_ACCESSIBLE){
                user_bt_env->access_state = ACCESS_IDLE;
                uart_send("+ACC:IK\r\n",9);
                if(bt_change_a2dp_flag == 1){
                    bt_change_a2dp_flag = 0;
                    status = bt_set_a2dp_type(BT_A2DP_TYPE_SRC);
                    bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
                    if(status == BT_STATUS_SUCCESS){
                        uart_send("OK\r\n",4);
                    }else{
                        uart_send("FAIL\r\n",6);
                    }
                }else if(bt_change_a2dp_flag == 2){
                    bt_change_a2dp_flag = 0;
                    status = bt_set_a2dp_type(BT_A2DP_TYPE_SINK);
                    bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE,NULL);
                    if(status == BT_STATUS_SUCCESS){
                        uart_send("OK\r\n",4);
                    }else{
                        uart_send("FAIL\r\n",6);
                    }
                }
            }

            //system_sleep_enable();
            break;

        case BTEVENT_MODE_CHANGE:
            //BT工作模式改变事件上报，sniff或者active
            device_index = bt_find_device(event->addr);
            user_bt_env->dev[device_index].mode = event->param.modeChange.curMode;
            if(user_bt_env->dev[device_index].mode == BLM_SNIFF_MODE){
                //printf("+MODE:1\r\n");
                bt_stop_sniff_monitor();
                pmu_set_gpio_value(GPIO_PORT_B, 1<<GPIO_BIT_1, 1);
            }else{
                //printf("+MODE:0\r\n");
                bt_start_sniff_monitor(5);
                pmu_set_gpio_value(GPIO_PORT_B, 1<<GPIO_BIT_1, 0);

                audio_source_statemachine(USER_EVT_BT_EXIT_SNIFF, NULL);
            }
            #if 0
            if(event->param.modeChange.curMode == BLM_SNIFF_MODE){
                pmu_set_gpio_value(GPIO_PORT_B, 1<<GPIO_BIT_0, 0);
            }else{
                pmu_set_gpio_value(GPIO_PORT_B, 1<<GPIO_BIT_0, 1);
            }
            #endif
            break;
        case BTEVENT_ROLE_CHANGE:

            break;
        default:
            break;
    }
}

void bt_avrcp_evt_func(avrcp_event_t *event)
{
    uint8_t deviceNum;
    uint8_t send_buf[200];
    uint8_t len0,len1;
    ///查询本地设备列表中，该地址对应的设备序�?
    deviceNum = bt_find_device(event->addr);
    if(deviceNum >= NUM_DEVICES){
        LOG_WARN("avrcp func: event=%d,erorr device num,%x,%x,%x.....\r\n",event->event,event->addr->A[0],user_bt_env->dev[0].bd.A[0],user_bt_env->dev[1].bd.A[0]);
    }

    //printf("avrcp event = %d\r\n",event->event);
    switch(event->event){
        ///收到远端连接请求
        case AVRCP_EVENT_CONNECT_IND:
        {
        ///接收该连接并发送response给远�?
            avrcp_connect_rsp(event->chnl, true);
        }
        break;
        ///AVRCP连接成功建立
        case AVRCP_EVENT_CONNECT:
        {
            ///存储本地状态信�?
            user_bt_env->dev[deviceNum].conFlags|= LINK_STATUS_AVC_CONNECTED;
            user_bt_env->dev[deviceNum].rcp_chan = event->chnl;
            ///查询本地所有连接是否都已成功连接上，若是，则保存连接信息到flash
            bt_check_conn(deviceNum);
            bt_connect_stage  = 2;

            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SINK){
            ///查询远端TG支持的event
                avrcp_ct_get_capabilities(event->chnl, AVRCP_CAPABILITY_EVENTS_SUPPORTED);
            }
            ///设置本地TG支持的event
            avrcp_tg_set_event_mask(event->chnl, AVRCP_ENABLE_VOLUME_CHANGED);

            LOG_INFO("avrcp profile connected...\r\n");
        }
        break;
        ///AVRCPlޓ׏ߪ
        case AVRCP_EVENT_DISCONNECT:
            ///ѣզ״̬хϢ
            user_bt_env->dev[deviceNum].conFlags &= (~LINK_STATUS_AVC_CONNECTED);
            user_bt_env->dev[deviceNum].rcp_chan = NULL;

            user_bt_env->dev[deviceNum].prf_all_connected = false;
            LOG_INFO("avrcp profile disconnected...\r\n");
            break;
        ///
        case AVRCP_EVENT_PANEL_CNF:
            ///被确认消息是名利‘按下’事�?
            if(event->param.panelCnf.press == TRUE){
                ///根据不同命令，自动发送对应的‘释放’命�?
                switch(event->param.panelCnf.operation) {
                    case AVRCP_POP_PAUSE:
                        avrcp_set_panel_key(event->chnl, AVRCP_POP_PAUSE, FALSE);
                        break;
                    case AVRCP_POP_PLAY:
                        avrcp_set_panel_key(event->chnl, AVRCP_POP_PLAY, FALSE);
                        break;
                    case AVRCP_POP_FORWARD:
                        avrcp_set_panel_key(event->chnl, AVRCP_POP_FORWARD, FALSE);
                        break;
                    case AVRCP_POP_BACKWARD:
                        avrcp_set_panel_key(event->chnl, AVRCP_POP_BACKWARD, FALSE);
                        break;
                    case AVRCP_POP_STOP:
                        avrcp_set_panel_key(event->chnl, AVRCP_POP_STOP, FALSE);
                        break;
                }
            }
            break;

        case AVRCP_EVENT_PANEL_RELEASE:
            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SRC){
                if(event->param.panelInd.operation == AVRCP_POP_PLAY){
                    audio_source_action_play();
                }else if(event->param.panelInd.operation == AVRCP_POP_PAUSE){
                    a2dp_suspend_stream(user_bt_env->dev[0].pstream);
                }
            }
            break;
            ///接收远端TG的控制信息，通常只有音量信息
        case AVRCP_EVENT_ADV_INFO:
            if(event->advOp == AVRCP_OP_SET_ABSOLUTE_VOLUME) {
                //LOG_INFO("SET_ABSOLUTE_VOLUME is %d.\r\n", event->param.adv.info.volume);
                //printf("+VOL:%02x\r\n",event->param.adv.info.volume/2);
                
                if(user_evt_notify_enable&USER_EVT_NOTIFY_VOLUME){
                    co_sprintf((void *)send_buf,"+VOL:%02x\r\n",event->param.adv.info.volume/2);
                    uart_send(send_buf,9);
                }
                REG_PL_WR(0x50022000+(0xd6<<2),event->param.adv.info.volume/2);
                REG_PL_WR(0x50022000+(0xd7<<2),event->param.adv.info.volume/2);
            }
            break;
        ///Ѿ֘ADVļ®ԉ٦ע̍
        case AVRCP_EVENT_ADV_TX_DONE:
            break;
        ///ܘشѾ֘өѯܰעӡļ®˂ݾ
        case AVRCP_EVENT_ADV_RESPONSE:
            ///本地查询TG支持事件，返回消�?
            if((event->advOp == AVRCP_OP_GET_CAPABILITIES)
                &&(event->param.adv.rsp.capability.type == AVRCP_CAPABILITY_EVENTS_SUPPORTED)){
                if(event->param.adv.rsp.capability.info.eventMask & AVRCP_ENABLE_PLAY_STATUS_CHANGED){
                    ///注册播放状态改变事件，当TG播放状态改变时，产生AVRCP_EVENT_ADV_NOTIFY事件
                    avrcp_ct_register_notification(event->chnl, AVRCP_ENABLE_PLAY_STATUS_CHANGED, 0);
                }
                if(bt_get_a2dp_type() == BT_A2DP_TYPE_SINK){
                    if(event->param.adv.rsp.capability.info.eventMask & AVRCP_ENABLE_TRACK_CHANGED){
                        ///注册歌曲改变事件，当TG播放状态改变时，产生AVRCP_EVENT_ADV_NOTIFY事件
                        avrcp_ct_register_notification(event->chnl, AVRCP_ENABLE_TRACK_CHANGED, 0);
                    }
                }
            }
            ///本地注册事件，返回的当前状�?
            if(event->advOp == AVRCP_OP_REGISTER_NOTIFY) {
                ///播放状态改变通知的当前状�?
                if(event->param.adv.notify.event == AVRCP_EID_MEDIA_STATUS_CHANGED) {
                    if((event->param.adv.notify.p.mediaStatus == AVRCP_MEDIA_STOPPED)
                        ||(event->param.adv.notify.p.mediaStatus == AVRCP_MEDIA_PAUSED)) {
                        ///保存状态到本地，音乐暂停状态比A2DP中的suspend事件更早产生,  有利于APP状态判�?
                        user_bt_env->dev[deviceNum].conFlags &= ~LINK_STATUS_MEDIA_PLAYING;
                    }
                    else {

                        user_bt_env->dev[deviceNum].conFlags |= LINK_STATUS_MEDIA_PLAYING;
                        user_bt_env->current_playing_index = deviceNum;
                    }
                }
            }
            if(event->advOp == AVRCP_OP_GET_MEDIA_INFO){
                //printf("element1 = %x,%x,%x,%s\r\n",event->param.adv.rsp.element.txt[0].attrId,event->param.adv.rsp.element.txt[0].charSet,event->param.adv.rsp.element.txt[0].length,event->param.adv.rsp.element.txt[0].string);
                //printf("mdia info = %d,%s,%s\r\n",event->param.adv.rsp.element.numIds,event->param.adv.rsp.element.txt[0].string,event->param.adv.rsp.element.txt[1].string);
                //printf("element2 = %x,%x,%x,%s\r\n",event->param.adv.rsp.element.txt[1].attrId,event->param.adv.rsp.element.txt[1].charSet,event->param.adv.rsp.element.txt[1].length,event->param.adv.rsp.element.txt[1].string);
                len0 = event->param.adv.rsp.element.txt[0].length;
                len1 = event->param.adv.rsp.element.txt[1].length;
                if(user_evt_notify_enable&USER_EVT_NOTIFY_MEDIA_INFO){
                    co_sprintf((void *)send_buf,"+MN:%02d,%s,%02d,",len0,event->param.adv.rsp.element.txt[0].string,len1);
                    uint16_t i = 0;
                    for(i = 0;i< len1;i++){
                        //printf("%c",event->param.adv.rsp.element.txt[1].string[i]);
                        send_buf[i+11+len0] = event->param.adv.rsp.element.txt[1].string[i];
                    }
                    send_buf[i+11+len0] = '\r';
                    send_buf[i+12+len0] = '\n';
                    send_buf[i+13+len0] = '\0';
                    if(i+13+len0 > 255){
                        uart_send(send_buf,255);
                    }else{
                    
                        //printf("t=%d\r\n",i+13+len0);
                        uart_send(send_buf,i+13+len0);
                    }
                }

            }

            break;
        ///עӡք˂ݾ֪ͨ׵ܘ
        case AVRCP_EVENT_ADV_NOTIFY:
            ///播放状态改变通知
            if(event->param.adv.notify.event == AVRCP_EID_MEDIA_STATUS_CHANGED) {
                if((event->param.adv.notify.p.mediaStatus == AVRCP_MEDIA_STOPPED)
                    ||(event->param.adv.notify.p.mediaStatus == AVRCP_MEDIA_PAUSED)) {
                    user_bt_env->dev[deviceNum].conFlags &= ~LINK_STATUS_MEDIA_PLAYING;
                }
                else {
                    user_bt_env->dev[deviceNum].conFlags |= LINK_STATUS_MEDIA_PLAYING;
                    user_bt_env->current_playing_index = deviceNum;
                }
                ///重新注册播放状态改变通知消息
                avrcp_ct_register_notification(event->chnl, AVRCP_ENABLE_PLAY_STATUS_CHANGED,0);
            }
            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SINK){
                ///歌曲改变通知
                if(event->param.adv.notify.event == AVRCP_EID_TRACK_CHANGED) {
                     //printf("track changed\r\n");
                     ///重新注册歌曲改变通知消息
                     avrcp_ct_register_notification(event->chnl, AVRCP_ENABLE_TRACK_CHANGED,0);
                     avrcp_ct_get_media_info(user_bt_env->dev[deviceNum].rcp_chan, 0x41);
                }
            }

            break;
        default:
            LOG_WARN("other avrcp event %d\r\n",event->event);
            break;
    }
}

void bt_spp_evt_func(spp_event_t *event)
{
    uint16_t i;
    switch(event->event) {
        case SPP_EVENT_REMDEV_CONNECTED:
            LOG_INFO("spp connected\r\n");
            break;
        case SPP_EVENT_REMDEV_DISCONNECTED:
            LOG_INFO("spp disconnected\r\n");
            break;
        case SPP_EVENT_DATA_IND:
            LOG_INFO("spp data rx: ");
            for(i = 0; i < event->datalen; i++)
            {
                LOG_INFO("%x ",*((uint8_t *)event->data + i));
            }
            LOG_INFO("\r\n");
            break;
        case SPP_EVENT_SEND_COMPLETE:
            //ke_free(Info->p.pkt->data);
            os_free((void *)event->data);
            break;
    }
}
uint16_t pbap_tx_len = 4;

void PbapWriteVcard(uint8_t *tx, uint8_t* pbuffer, uint32_t len)
{
#if 0
    if (memcmp("FN:", pbuffer, sizeof("FN:") - 1) == 0) {
        uart_write(pbuffer, len);
    } else if (memcmp("END:", pbuffer, sizeof("END:") - 1) == 0) {
        uart_write(pbuffer, len);
    } else if (memcmp("TEL;", pbuffer, sizeof("TEL;") - 1) == 0) {
        uart_write(pbuffer, len);
    } else if (memcmp("BEGIN:", pbuffer, sizeof("BEGIN:") - 1) == 0) {
        uart_write(pbuffer, len);
    } else if(memcmp("TEL:", pbuffer, sizeof("TEL:") - 1) == 0){
        uart_write(pbuffer,len);
    }
#endif
    if((memcmp("FN:", pbuffer, sizeof("FN:") - 1) == 0)
        ||(memcmp("END:", pbuffer, sizeof("END:") - 1) == 0)
        ||(memcmp("TEL;", pbuffer, sizeof("TEL;") - 1) == 0)
        ||(memcmp("BEGIN:", pbuffer, sizeof("BEGIN:") - 1) == 0)
        ||(memcmp("TEL:", pbuffer, sizeof("TEL:") - 1) == 0)){
            memcpy(tx,pbuffer,len);
            pbap_tx_len += len;
        }

}
extern os_timer_t pcconnect_req_timeout_handle_id;

void PbaClientCallback(PbapClientCallbackParms_new *Parms)
{
    //_Bool           newWindow;
    static uint16_t     offset = 0;
    static uint8_t *pbap_buffer = NULL;
    static uint8_t  left_size = 0;
    uint8_t *pbap_tx;
    uint8_t pbap_send[20];
    char* pstart;
    char* pstr;
    char* pend;
    char tmp;
    
    switch (Parms->event) {
    case PBAP_EVENT_PARAMS_RX:


        if ((Parms->oper == PBAPOP_PULL_PHONEBOOK) ||
            (Parms->oper == PBAPOP_PULL_VCARD_LISTING)) {
            if (Parms->u.paramsRx.newMissedCalls) {
                /* Indicate up any new missed calls */
//                printf("Client: PBAP Parameters, New Missed Calls: %d.",
//                            Parms->u.paramsRx.newMissedCalls);
            }

            if (Parms->u.paramsRx.phonebookSize) {
                /* If a maxListCount of zero was issued, this is the
                 * size of the phonebook (e.g. maxListCount =
                 * Parms->u.paramsRx.phonebookSize;).
                 */
//                printf("Client: PBAP Parameters, Phonebook Size: %d.",
//                            Parms->u.paramsRx.phonebookSize);
            }
        }

        break;

    case PBAP_EVENT_DATA_IND:
        if (offset == 0) {
            if (!pbap_buffer)
            {
                left_size = 0;
                pbap_buffer = os_malloc(512);
            }
        } 
        pbap_tx = os_malloc(Parms->u.dataInd.len + 9);

        pbap_tx_len = 10;
        //printf("+PP:%d,off=%d\r\n",Parms->u.dataInd.len,offset);
        offset += Parms->u.dataInd.len;
        pstart = (char *)Parms->u.dataInd.buffer;
        pstr = pstart;
        pend = pstart + Parms->u.dataInd.len;
        while (pstr<pend)
        {
            for(pstr = pstart;pstr<pend;pstr++)
            {
                if (*pstr == '\n' && *(pstr-1) == '\r')
                {
                    pstr++;
                    if (left_size)
                    {
                        memcpy(pbap_buffer + left_size, pstart, pstr - pstart);
                        PbapWriteVcard(&pbap_tx[pbap_tx_len],pbap_buffer, pstr - pstart + left_size);
                        
                        //uart_write(pbap_buffer, pstr - pstart + left_size);
                        left_size = 0;
                    } else {
                        PbapWriteVcard(&pbap_tx[pbap_tx_len],(void *)pstart, pstr - pstart);
                        
                        //uart_write(pstart, pstr - pstart);
                    }
                    pstart = pstr;
                    break;
                }
            }
            if (pstart != pstr)
            {
                left_size = pstr - pstart;
                memcpy(pbap_buffer, pstart, left_size);
            }
        }
        tmp = pbap_tx[10];
        co_sprintf((void *)pbap_tx,"+PP:%04x\r\n",pbap_tx_len-10);
        //co_sprintf will add '\0' at the end of string
        pbap_tx[10] = tmp;
                
        uart_send(pbap_tx,pbap_tx_len);
        os_free(pbap_tx);
        break;

    case PBAP_EVENT_TP_CONNECTED:
        pbap_set_client_state(PBA_TP_CONNECTED);
        break;

    case PBAP_EVENT_TP_DISCONNECTED:

        pbap_set_client_state(PBA_IDLE);
        //App_Progress(GetClientUIContext(), 0, 0);
        break;

    case PBAP_EVENT_CONTINUE:
        /* Always call continue to keep the commands flowing */
        pbap_clientcontinue();
        break;

    case PBAP_EVENT_ABORTED:
        /* The requested operation was aborted. */
//        printf("Client: PBAP operation was aborted, \"%s\".", pbap_abort_msg(Parms->u.abortReason));
        co_sprintf((void *)pbap_send,"+PEA:0x%02x\r\n",Parms->u.abortReason);
        uart_send(pbap_send,11);
        break;

    case PBAP_EVENT_COMPLETE:
        /* The requested operation has completed. */
        co_sprintf((void *)pbap_send,"+PEC:0x%02x\r\n",Parms->oper);
        uart_send(pbap_send,11);
        switch(Parms->oper)
        {
             case PBAPOP_NONE:
                break;
            case PBAPOP_CONNECT:
                pbap_connect_flag = 1;
                os_timer_stop(&pcconnect_req_timeout_handle_id);
                break;
            case PBAPOP_DISCONNECT:
                pbap_connect_flag = 0;
                break;
            case PBAPOP_PULL:
                break;
            case PBAPOP_PULL_PHONEBOOK:
                break;
            case PBAPOP_SET_PHONEBOOK:
                break;
            case PBAPOP_PULL_VCARD_LISTING:
                break;
            case PBAPOP_PULL_VCARD_ENTRY:
                break;
        }
        //App_Progress(GetClientUIContext(), 0, 0);

        /* The requested operation has completed. */
        if (Parms->oper == PBAPOP_CONNECT) {
            /* Transport and OBEX connection are established. */
            pbap_set_client_state(PBA_CONNECTED);
        }

        /* Reset operation offset */
        offset = 0;
        if (pbap_buffer)
        {
            os_free(pbap_buffer);
            pbap_buffer = NULL;
			left_size = 0;
        }
        break;

    default:

        break;
    }
}


enum bt_state_t bt_get_state(void)
{
    enum bt_state_t ret_state = BT_STATE_IDLE;
    if((user_bt_env->dev[0].state == BT_CONNECTED)||(user_bt_env->dev[1].state == BT_CONNECTED)){
        ret_state = BT_STATE_CONNECTED;
        if(user_bt_env->dev[user_bt_env->last_active_dev_index].active == HF_CALL_ACTIVE){
            //multi part call
            ret_state = BT_STATE_HFP_CALLACTIVE;
            #if 0
            if((user_bt_env->dev[user_bt_env->last_active_dev_index].call_held == HF_CALL_HELD_ACTIVE)||(user_bt_env->dev[user_bt_env->last_active_dev_index].call_held == HF_CALL_HELD_NO_ACTIVE)){
                ret_state = APP_STATE_CAMULTY;
            }

            if(user_bt_env->dev[user_bt_env->last_active_dev_index].setup_state == HF_CALL_SETUP_IN){
                ret_state = APP_STATE_HFP_CAIMG;
            }
            else if((user_bt_env->dev[user_bt_env->last_active_dev_index].setup_state == HF_CALL_SETUP_OUT)
                ||(user_bt_env->dev[user_bt_env->last_active_dev_index].setup_state == HF_CALL_SETUP_ALERT)){
                ret_state = APP_STATE_HFP_CAOGG;
            }
            #endif
        }
        else{
            if(user_bt_env->dev[user_bt_env->last_active_dev_index].setup_state == HF_CALL_SETUP_IN){
                ret_state = BT_STATE_HFP_INCOMMING;
            }
            else if((user_bt_env->dev[user_bt_env->last_active_dev_index].setup_state == HF_CALL_SETUP_OUT)
                ||(user_bt_env->dev[user_bt_env->last_active_dev_index].setup_state == HF_CALL_SETUP_ALERT)){
                ret_state = BT_STATE_HFP_OUTGOING;
            }else if(user_bt_env->dev[user_bt_env->last_active_dev_index].conFlags&LINK_STATUS_MEDIA_PLAYING){
                ret_state = BT_STATE_MEDIA_PLAYING;
            }
        }
    }
    else if((user_bt_env->dev[0].state == BT_CONNECTING)||(user_bt_env->dev[1].state == BT_CONNECTING)){
        ret_state = BT_STATE_CONNECTING;
    }
    else{
        if(user_bt_env->access_state == ACCESS_PAIRING){
            ret_state = BT_STATE_PAIRING;
        }
        else if(user_bt_env->access_state == ACCESS_STANDBY){
            ret_state = BT_STATE_STANDBY;
        }
        else{
            ret_state = BT_STATE_IDLE;
        }
    }
    return ret_state;
}

bool bt_reconnect(enum bt_reconnect_type_t type,BD_ADDR *addr)
{
    bool ret = false;
    BD_ADDR bdaddr;
    uint8_t get_proper_dev_flag = false;

    LOG_INFO("reconnect... %x,%x,%d\r\n",user_bt_env->dev[0].reconnecting,user_bt_env->dev[0].reconnect_times,type);
    if((type == 0)&&get_bt_reconnect_state()&&(user_bt_env->dev[0].state != BT_CONNECTED)){
        os_timer_start(&bt_delay_reconnect_timer,5000,0);
        user_bt_env->dev[0].state = BT_IDLE;
        //uart_putc_noint('*');
        return false;
    }

    if(type == RECONNECT_TYPE_POWER_ON){
        if(bt_get_last_device(&bdaddr) == BT_STATUS_SUCCESS){
            //last dev addr exists
            get_proper_dev_flag = true;
        }
    }
    else if(type == RECONNECT_TYPE_USER_CMD){
        if(addr == NULL){
            if(bt_get_last_device(&bdaddr) == BT_STATUS_SUCCESS){
                //last dev addr exists
                get_proper_dev_flag = true;
            }
        }else{
            get_proper_dev_flag = true;
            memcpy(bdaddr.A,addr->A,BD_ADDR_SIZE);
        }
    }
    else if(type == RECONNECT_TYPE_LINK_LOSS){
        memcpy(bdaddr.A, addr->A, BD_ADDR_SIZE);
        get_proper_dev_flag = true;
    }

    if(get_proper_dev_flag == false) {
        user_bt_env->dev[0].state = BT_IDLE;
        bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE, NULL);
    }
    else{
        if(user_bt_env->dev[0].reconnecting == 0){
            user_bt_env->dev[0].state = BT_CONNECTING;
            //user_bt_env->dev[0].inUse = 1;
            user_bt_env->dev[0].reconnecting = type;
            if(type == RECONNECT_TYPE_POWER_ON){
                user_bt_env->dev[0].reconnect_times = pskeys.power_on_reconnect.reconnect_times;
                memcpy( user_bt_env->dev[0].bd.A, bdaddr.A, BD_ADDR_SIZE);
            }
            else if(type == RECONNECT_TYPE_LINK_LOSS){
                user_bt_env->dev[0].reconnect_times = pskeys.link_loss_reconnect.reconnect_times;
                memcpy( user_bt_env->dev[0].bd.A, bdaddr.A, BD_ADDR_SIZE);
            }
            else if(type == RECONNECT_TYPE_USER_CMD){
                user_bt_env->dev[0].reconnect_times = pskeys.press_button_reconnect.reconnect_times;

                memcpy( user_bt_env->dev[0].bd.A, bdaddr.A, BD_ADDR_SIZE);
            }
        }

        if(user_bt_env->dev[0].reconnect_times > 0){
            if(user_bt_env->dev[0].reconnect_times != 0xff)
                user_bt_env->dev[0].reconnect_times --;

            if(user_bt_env->access_state ==ACCESS_IDLE){
                bt_reset_controller();
                if(reconnect_according_to_profile(&bdaddr) == BT_STATUS_FAILED){
                    memset(&(user_bt_env->dev[0]),0,sizeof(user_bt_env->dev[0]));
                    bt_set_accessible_mode_nc(BAM_GENERAL_ACCESSIBLE, NULL);
                }else{
                    ret = true;
                }
            }
            else{

                bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE, NULL);
                os_timer_init(&bt_delay_reconnect_timer,bt_delay_reconnect_timer_func,NULL);
                os_timer_start(&bt_delay_reconnect_timer,30,0);
                ret = true;
            }
        }
        else{
            bt_set_accessible_mode_nc(BAM_NOT_ACCESSIBLE, NULL);
            ret = true;
            memset(&(user_bt_env->dev[0]),0,sizeof(APP_DEVICE));
        }
    }

    return ret;
}

void bt_disconnect(void)
{
    uint8_t i = 0;
    do_disconnect = true;
    if(user_bt_env->access_state == ACCESS_PAIRING){
        do_disconnect = false;
        return;
    }
    if(user_bt_env->dev[i].state == BT_CONNECTED){
        if(user_bt_env->dev[i].conFlags == 0){
            //hf_close_channel();
            bt_remove_acl_link(user_bt_env->dev[i].remDev);
        }else{
            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SINK){

            }
            if(((user_bt_env->dev[i].conFlags&(LINK_STATUS_HF_CONNECTED|LINK_STATUS_AVC_CONNECTED|LINK_STATUS_AV_CONNECTED))
                == (LINK_STATUS_HF_CONNECTED|LINK_STATUS_AVC_CONNECTED|LINK_STATUS_AV_CONNECTED))
                ||(bt_get_a2dp_type() == BT_A2DP_TYPE_SRC)){
                if(user_bt_env->dev[i].conFlags&LINK_STATUS_SCO_CONNECTED){
                    hf_disconnect_audio_link(user_bt_env->dev[i].hf_chan);
                }
                if(user_bt_env->dev[i].conFlags&LINK_STATUS_HF_CONNECTED){
                    hf_disconnect(user_bt_env->dev[i].hf_chan);
                }
                if(user_bt_env->dev[i].conFlags&LINK_STATUS_AVC_CONNECTED){
                    avrcp_disconnect(user_bt_env->dev[i].rcp_chan);
                }
                if(user_bt_env->dev[i].conFlags&LINK_STATUS_AV_CONNECTED){
                    a2dp_close_stream(user_bt_env->dev[i].pstream);
                }
            }

        }
    }else{
    }

}

bool bt_set_spk_volume(enum bt_volume_type_t type,uint8_t vol)
{
    uint8_t avrcp_vol,hf_vol;

    if((type >= BT_VOL_MAX)||(vol > 0x3f)){
        return false;
    }
    if(type == BT_VOL_HFP){
        pskeys.app_data.hf_vol = vol;
        if(user_bt_env->dev[0].conFlags&LINK_STATUS_SCO_CONNECTED){
            if(vol == 0){
                hf_vol = 0;
            }
            else{
                hf_vol = 1+ (15*vol)/64;
            }
            hf_report_speaker_volume(user_bt_env->dev[0].hf_chan, hf_vol);

            REG_PL_WR(0x50022000+(0xd6<<2),vol);
            REG_PL_WR(0x50022000+(0xd7<<2),vol);
        }
    }
    else if(type == BT_VOL_MEDIA){
        pskeys.app_data.a2dp_vol = vol;
        if(user_bt_env->dev[0].conFlags&LINK_STATUS_MEDIA_PLAYING){
            if(vol == 0x3f){
                avrcp_vol = 127;
            }
            else{
                avrcp_vol = (127*vol)/64;
            }
            if(bt_get_a2dp_type() == BT_A2DP_TYPE_SINK){
                avrcp_tg_set_absolute_volume(user_bt_env->dev[0].rcp_chan,avrcp_vol);
                REG_PL_WR(0x50022000+(0xd6<<2),vol);
                REG_PL_WR(0x50022000+(0xd7<<2),vol);
            }else{
                avrcp_ct_set_absolute_volume(user_bt_env->dev[0].rcp_chan,avrcp_vol);
            }

        }else{
            REG_PL_WR(0x50022000+(0xd6<<2),vol);
            REG_PL_WR(0x50022000+(0xd7<<2),vol);
        }
    }
    else if(type == BT_VOL_TONE){
        pskeys.app_data.tone_vol = vol;
    }

    pskeys_update_app_data();
    return true;
}

/* used to check whether all profiles are connected, if so link information will be stored into flash */
void bt_check_conn(uint8_t dev_num)
{
    uint8_t conn_flag = 0;

    //start timer to make sure all connections connected
    if(pskeys.enable_profiles&ENABLE_PROFILE_HF){
        conn_flag |=  LINK_STATUS_HF_CONNECTED;
    }
    if(pskeys.enable_profiles&ENABLE_PROFILE_A2DP){
        conn_flag |=  LINK_STATUS_AV_CONNECTED | LINK_STATUS_AVC_CONNECTED;
    }

    //co_printf("conFlags=%x,conn_flag=%x,and=%x\r\n",app_env.dev[dev_num].conFlags,conn_flag,conn_flag & (app_env.dev[dev_num].conFlags));
    if((conn_flag & (user_bt_env->dev[dev_num].conFlags)) != conn_flag){
        //restart timer
    }
    else{
        /*
        if(dev_num == 0){
            timer_id = APP_EVENT_BT_DEV0_ACT2SNIFF_TO;
        }else if(dev_num == 1){
            timer_id = APP_EVENT_BT_DEV1_ACT2SNIFF_TO;
        }
        app_env.dev[dev_num].mode = BLM_ACTIVE_MODE;
        if(((pskeys.dev_info.localfeatures0&SNIFF_FEATURE)==SNIFF_FEATURE)&&(pskeys.active_to_sniff !=0xff)){
            ke_timer_set(timer_id,TASK_APP,pskeys.active_to_sniff*100);
        }
        app_bt_env.dev[dev_num].tone_dis_flag = true;
        audio_codec_play_single_tone(APP_TONE_CONNECTED,AUDIO_CODEC_TONE_PRIO_MEDIAL);
        DDB_SaveLastDevice(&app_env.dev[dev_num].bd);
        */

        //pskeys_update_app();
        bt_save_last_device(&user_bt_env->dev[dev_num].bd);
        //system_sleep_enable();
        user_bt_env->dev[dev_num].prf_all_connected = true;
        //system_sleep_enable();
        uart_send("+CONN\r\n",7);
        bt_start_sniff_monitor(5);
        #if !MP3_SRC_LOCAL
        if(app_audio_play_env.audio_play_req_flag == true){
            app_audio_play_env.audio_play_req_flag = false;
            app_audio_play_env.audio_play_allowed_cb(PLAY_ALLOWABLE,NULL);
        }
        #endif
        LOG_INFO("\r\nIV\r\n");
        if(do_disconnect == true){
            bt_disconnect();
        }
    }
}

/**
 * @brief get current BT status
 *
 * @return uint8_t BLM_ACTIVE_MODE or BLM_SNIFF_MODE
 */
uint8_t bt_get_link_mode(void)
{
    return user_bt_env->dev[user_bt_env->last_active_dev_index].mode;
}

/*
* bt_statemachine
* 此状态机控制本地播放，mic录音模式，source耳机播放，sink通话的相互切�?
* 当前状态切换的依据是通过各种事件的优先级判断
* MIC_LOOP > Tone > CALL > Music > Mic record > Native playback
* 例如：MIC录音可以打断正在进行的本地播放，停止MIC录音后，本地继续播放
* 但是在播放音乐或者打电话时，不能进行MIC录音，用户需收到暂停或挂断电�?
* 后进行，而在MIC录音时，可以被手机音乐播放和通话打断，音乐暂停和通话�?
* 束后，自动开启MIC录音�?
*/
bool bt_statemachine(uint8_t event, void *arg)
{
    bool ret = true;
//    uint8_t check_flag = 0;
    //printf("bt statemachine:event=%d,state=%d\r\n",event,user_bt_state);
    switch(event) {
        case USER_EVT_DSP_OPENED:
            if(user_bt_state == USER_BT_STATE_AUDIO_WAIT_DSP_OPEN){
                dsp_working_label_set(DSP_WORKING_LABEL_VOICE_ALGO);
                user_bt_state = USER_BT_STATE_AUDIO_CONNECTED;
                dsp_nrec_start_flag = true;
                os_timer_init(&audio_codec_timer,audio_codec_timer_func,NULL);
                os_timer_start(&audio_codec_timer,300,0);
                ipc_msg_send((enum ipc_msg_type_t)IPC_MSG_WITHOUT_PAYLOAD, IPC_SUB_MSG_NREC_START, NULL);
            }

            break;
        case USER_EVT_HFP_AUDIO_CONNECTED:
            //save current user action,resume the action when call is hang up
            if(user_bt_state == USER_BT_STATE_NATIVE_PLAYBACK){
                native_playback_action_pause();
                user_bt_saved_state = USER_BT_STATE_NATIVE_PLAYBACK;
            }else if(user_bt_state == USER_BT_STATE_MIC_ONLY){
                audio_codec_func_stop(AUDIO_CODEC_FUNC_MIC_ONLY);
                user_bt_saved_state = USER_BT_STATE_MIC_ONLY;
            }
            user_bt_state = USER_BT_STATE_AUDIO_CONNECTED;
            if(dsp_open(DSP_LOAD_TYPE_VOICE_ALGO) == DSP_OPEN_SUCCESS) {
                dsp_nrec_start_flag = true;
                dsp_working_label_set(DSP_WORKING_LABEL_VOICE_ALGO);
                os_timer_init(&audio_codec_timer,audio_codec_timer_func,NULL);
                os_timer_start(&audio_codec_timer,300,0);
                ipc_msg_send((enum ipc_msg_type_t)IPC_MSG_WITHOUT_PAYLOAD, IPC_SUB_MSG_NREC_START, NULL);
            }
            else {
                // DSP_OPEN_PENDING
                user_bt_state = USER_BT_STATE_AUDIO_WAIT_DSP_OPEN;
            }
            break;

        case USER_EVT_HFP_AUDIO_DISCONNECTED:
            dsp_nrec_start_flag = false;
            user_bt_state = USER_BT_STATE_IDLE;
            dsp_working_label_clear(DSP_WORKING_LABEL_VOICE_ALGO);

            #if 0
            //resume saved function(mic only mode or native palyback,a2dp is auto resumed by phone)
            if(user_bt_saved_state == USER_BT_STATE_NATIVE_PLAYBACK){
                if(true == native_playback_action_play()){
                    uart_putc_noint('$');
                    user_bt_state = USER_BT_STATE_NATIVE_PLAYBACK;
                }
                user_bt_saved_state = USER_BT_STATE_IDLE;
            }else if(user_bt_saved_state == USER_BT_STATE_MIC_ONLY){
                audio_codec_func_start(AUDIO_CODEC_FUNC_MIC_ONLY);
                user_bt_state = USER_BT_STATE_MIC_ONLY;
                user_bt_saved_state = USER_BT_STATE_IDLE;
            }
            #else
            if(user_bt_saved_state != USER_BT_STATE_IDLE){
                os_timer_init(&bt_delay_resume_local_timer,bt_delay_resume_local_timer_func,NULL);
                os_timer_start(&bt_delay_resume_local_timer,300,false);
            }
            #endif
            break;

        case USER_EVT_A2DP_STREAM_STARTED:
            //save current user action,resume the action when music is suspended
            if(user_bt_state == USER_BT_STATE_NATIVE_PLAYBACK){
                native_playback_action_pause();
                user_bt_saved_state = USER_BT_STATE_NATIVE_PLAYBACK;
            }else if(user_bt_state == USER_BT_STATE_MIC_ONLY){
                audio_codec_func_stop(AUDIO_CODEC_FUNC_MIC_ONLY);
                user_bt_saved_state = USER_BT_STATE_MIC_ONLY;
            }
            user_bt_state = USER_BT_STATE_A2DP_STARTED;
            break;

        case USER_EVT_A2DP_STREAM_SUSPENDED:
            user_bt_state = USER_BT_STATE_IDLE;
            //resume saved function(mic only mode or native palyback)
            #if 0
            if(user_bt_saved_state == USER_BT_STATE_NATIVE_PLAYBACK){
                if(true == native_playback_action_play()){
                    user_bt_state = USER_BT_STATE_NATIVE_PLAYBACK;
                }
                user_bt_saved_state = USER_BT_STATE_IDLE;
            }else if(user_bt_saved_state == USER_BT_STATE_MIC_ONLY){
                audio_codec_func_start(AUDIO_CODEC_FUNC_MIC_ONLY);
                user_bt_state = USER_BT_STATE_MIC_ONLY;
                user_bt_saved_state = USER_BT_STATE_IDLE;
            }
            #else
            if(user_bt_saved_state != USER_BT_STATE_IDLE){
                os_timer_init(&bt_delay_resume_local_timer,bt_delay_resume_local_timer_func,NULL);
                os_timer_start(&bt_delay_resume_local_timer,300,false);
            }
            #endif
            break;

        case USER_EVT_MIC_ONLY_START:
            if(user_bt_state == USER_BT_STATE_IDLE){
                user_bt_state = USER_BT_STATE_MIC_ONLY;
                audio_codec_func_start(AUDIO_CODEC_FUNC_MIC_ONLY);
            }else if(user_bt_state == USER_BT_STATE_NATIVE_PLAYBACK){
                user_bt_state = USER_BT_STATE_MIC_ONLY;
                native_playback_action_pause();
                audio_codec_func_start(AUDIO_CODEC_FUNC_MIC_ONLY);
                user_bt_saved_state = USER_BT_STATE_NATIVE_PLAYBACK;
            }else{
                ret = false;
                LOG_INFO("bt statemachine error!!!,event= %d,state=%d.\r\n",event,user_bt_state);
            }
            break;
        case USER_EVT_MIC_ONLY_STOP:
            if(user_bt_state == USER_BT_STATE_MIC_ONLY){
                user_bt_state = USER_BT_STATE_IDLE;

                audio_codec_func_stop(AUDIO_CODEC_FUNC_MIC_ONLY);
                if(user_bt_saved_state == USER_BT_STATE_NATIVE_PLAYBACK){
                    user_bt_saved_state = USER_BT_STATE_IDLE;
                    if(true == native_playback_action_play()){
                        user_bt_state = USER_BT_STATE_NATIVE_PLAYBACK;
                    }
                }
            }else{
                ret = false;
            }
            break;
        case USER_EVT_NATIVE_PLAYBACK_START:
            if(user_bt_state == USER_BT_STATE_IDLE){
                user_bt_state = USER_BT_STATE_NATIVE_PLAYBACK;
                native_playback_action_play();
            }else{
                ret = false;
                LOG_INFO("bt statemachine error!!!,event= %d,state=%d.\r\n",event,user_bt_state);
            }
            break;
        case USER_EVT_NATIVE_PLAYBACK_STOP:
            if(user_bt_state == USER_BT_STATE_NATIVE_PLAYBACK){
                user_bt_state = USER_BT_STATE_IDLE;
                native_playback_action_pause();
            }else{
                ret = false;
            }
            break;
        case USER_EVT_NATIVE_PLAYBACK_NEXT:
            if((user_bt_state == USER_BT_STATE_IDLE)||(user_bt_state == USER_BT_STATE_NATIVE_PLAYBACK)){
                user_bt_state = USER_BT_STATE_NATIVE_PLAYBACK;
                native_playback_action_next();
            }else{
                ret = false;
                LOG_INFO("bt statemachine error!!!,event= %d,state=%d.\r\n",event,user_bt_state);
            }
            break;
        case USER_EVT_NATIVE_PLAYBACK_PREV:
            if((user_bt_state == USER_BT_STATE_IDLE)||(user_bt_state == USER_BT_STATE_NATIVE_PLAYBACK)){
                user_bt_state = USER_BT_STATE_NATIVE_PLAYBACK;
                native_playback_action_prev();
            }else{
                ret = false;
                LOG_INFO("bt statemachine error!!!,event= %d,state=%d.\r\n",event,user_bt_state);
            }
            break;

        case USER_EVT_MIC_LOOP_START:
            if(user_bt_state == USER_BT_STATE_NATIVE_PLAYBACK){
                native_playback_action_pause();
                user_bt_saved_state = USER_BT_STATE_NATIVE_PLAYBACK;
            }
            user_bt_state = USER_BT_STATE_MIC_LOOP;
            audio_codec_func_start(AUDIO_CODEC_FUNC_MIC_LOOP);

            break;
        case USER_EVT_MIC_LOOP_STOP:
            audio_codec_func_stop(AUDIO_CODEC_FUNC_MIC_LOOP);
            if(user_bt_saved_state == USER_BT_STATE_NATIVE_PLAYBACK){
                user_bt_saved_state = USER_BT_STATE_IDLE;
                if(true == native_playback_action_play()){
                    user_bt_state = USER_BT_STATE_NATIVE_PLAYBACK;
                }
            }
            if(user_bt_state == USER_BT_STATE_MIC_LOOP){
                user_bt_state = USER_BT_STATE_IDLE;
            }

            break;

        case USER_EVT_CONN_DISCONNECTED:
            user_bt_state = USER_BT_STATE_IDLE;
            user_bt_saved_state = USER_BT_STATE_IDLE;
            break;
        default:
            break;
    }
    return ret;
}
