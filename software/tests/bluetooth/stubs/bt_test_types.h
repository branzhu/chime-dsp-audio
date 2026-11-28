#pragma once
#include "esp_err.h"
typedef unsigned esp_bt_io_cap_t;
typedef uint8_t esp_bt_pin_code_t[16];
typedef struct {int unused;} esp_bt_controller_config_t;
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() ((esp_bt_controller_config_t){0})
enum {ESP_BT_CONTROLLER_STATUS_IDLE,ESP_BT_CONTROLLER_STATUS_INITED,ESP_BT_CONTROLLER_STATUS_ENABLED};
enum {ESP_BLUEDROID_STATUS_UNINITIALIZED,ESP_BLUEDROID_STATUS_INITIALIZED,ESP_BLUEDROID_STATUS_ENABLED};
enum {ESP_BT_MODE_CLASSIC_BT=1,ESP_BT_IO_CAP_NONE=2,ESP_BT_SP_IOCAP_MODE=3};
enum {ESP_BT_NON_CONNECTABLE,ESP_BT_CONNECTABLE,ESP_BT_NON_DISCOVERABLE,ESP_BT_GENERAL_DISCOVERABLE};
int esp_bt_controller_get_status(void);
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t *c);
esp_err_t esp_bt_controller_enable(int mode);
esp_err_t esp_bt_controller_disable(void);
esp_err_t esp_bt_controller_deinit(void);
int esp_bluedroid_get_status(void);
esp_err_t esp_bluedroid_init(void);
esp_err_t esp_bluedroid_enable(void);
esp_err_t esp_bluedroid_disable(void);
esp_err_t esp_bluedroid_deinit(void);
esp_err_t nvs_flash_init(void);
typedef enum {ESP_BT_GAP_CFM_REQ_EVT,ESP_BT_GAP_PIN_REQ_EVT} esp_bt_gap_cb_event_t;
typedef struct {struct {uint8_t bda[6];} cfm_req,pin_req;} esp_bt_gap_cb_param_t;
esp_err_t esp_bt_gap_register_callback(void (*cb)(esp_bt_gap_cb_event_t,esp_bt_gap_cb_param_t *));
esp_err_t esp_bt_gap_set_device_name(const char *name);
esp_err_t esp_bt_gap_set_security_param(unsigned type,void *p,unsigned n);
esp_err_t esp_bt_gap_set_scan_mode(int connection,int discovery);
esp_err_t esp_bt_gap_ssp_confirm_reply(uint8_t *bda,bool accept);
esp_err_t esp_bt_gap_pin_reply(uint8_t *bda,bool accept,unsigned n,esp_bt_pin_code_t code);
enum {ESP_A2D_INIT_SUCCESS=0,ESP_A2D_DEINIT_SUCCESS=1};
enum {ESP_A2D_CONNECTION_STATE_DISCONNECTED,ESP_A2D_CONNECTION_STATE_CONNECTING,ESP_A2D_CONNECTION_STATE_CONNECTED,ESP_A2D_CONNECTION_STATE_DISCONNECTING};
enum {ESP_A2D_AUDIO_STATE_SUSPEND,ESP_A2D_AUDIO_STATE_STARTED};
#define ESP_A2D_MCT_SBC 0
#define ESP_A2D_SBC_CIE_SF_16K 8
#define ESP_A2D_SBC_CIE_SF_32K 4
#define ESP_A2D_SBC_CIE_SF_44K 2
#define ESP_A2D_SBC_CIE_SF_48K 1
#define ESP_A2D_SBC_CIE_CH_MODE_MONO 8
#define ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL 4
#define ESP_A2D_SBC_CIE_CH_MODE_STEREO 2
#define ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO 1
typedef unsigned esp_a2d_conn_hdl_t;
typedef struct {unsigned obj_type,samp_freq1,samp_freq2,ch,vbr,br1,br2,br3;} esp_a2d_cie_m24_t;
typedef struct {unsigned type;struct {uint8_t vendor[12];struct {unsigned samp_freq,ch_mode,block_len,num_subbands,alloc_mthd,min_bitpool,max_bitpool;} sbc_info;esp_a2d_cie_m24_t m24_info;} cie;} esp_a2d_mcc_t;
typedef struct {uint16_t data_len;uint8_t *data;uint32_t timestamp;uint16_t number_frame,buff_size;} esp_a2d_audio_buff_t;
esp_a2d_audio_buff_t *esp_a2d_audio_buff_alloc(uint16_t size);
#define ESP_A2D_MCT_M24 2
#define ESP_A2D_SEP_REG_SUCCESS 0
void esp_a2d_audio_buff_free(esp_a2d_audio_buff_t *buffer);
esp_err_t esp_a2d_sink_register_audio_data_callback(void (*cb)(esp_a2d_conn_hdl_t,esp_a2d_audio_buff_t *));
esp_err_t esp_a2d_sink_register_stream_endpoint(uint8_t seid,const esp_a2d_mcc_t *mcc);
typedef enum {ESP_A2D_PROF_STATE_EVT,ESP_A2D_CONNECTION_STATE_EVT,ESP_A2D_AUDIO_STATE_EVT,ESP_A2D_AUDIO_CFG_EVT,ESP_A2D_SEP_REG_STATE_EVT,ESP_A2D_SNK_PSC_CFG_EVT,ESP_A2D_SNK_SET_DELAY_VALUE_EVT} esp_a2d_cb_event_t;
typedef struct {struct {unsigned init_state;} a2d_prof_stat;struct {unsigned state;esp_a2d_conn_hdl_t conn_hdl;uint8_t remote_bda[6];} conn_stat,audio_stat;struct {esp_a2d_mcc_t mcc;esp_a2d_conn_hdl_t conn_hdl;} audio_cfg;struct {unsigned seid,reg_state;} a2d_sep_reg_stat;struct {unsigned psc_mask;} a2d_psc_cfg_stat;struct {unsigned set_state;uint16_t delay_value;} a2d_set_delay_value_stat;} esp_a2d_cb_param_t;
esp_err_t esp_a2d_register_callback(void (*cb)(esp_a2d_cb_event_t,esp_a2d_cb_param_t *));
esp_err_t esp_a2d_sink_register_data_callback(void (*cb)(const uint8_t *,uint32_t));
esp_err_t esp_a2d_sink_init(void);
esp_err_t esp_a2d_sink_deinit(void);
typedef enum {ESP_AVRC_TG_CONNECTION_STATE_EVT,ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT,ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT} esp_avrc_tg_cb_event_t;
typedef struct {struct {bool connected;uint8_t remote_bda[6];} conn_stat;struct {uint8_t volume;} set_abs_vol;struct {unsigned event_id;} reg_ntf;} esp_avrc_tg_cb_param_t;
typedef struct {uint8_t volume;} esp_avrc_rn_param_t;
typedef struct {unsigned bits;} esp_avrc_rn_evt_cap_mask_t;
enum {ESP_AVRC_RN_VOLUME_CHANGE=13,ESP_AVRC_RN_RSP_INTERIM=13,ESP_AVRC_RN_RSP_CHANGED=14,ESP_AVRC_BIT_MASK_OP_SET=1};
esp_err_t esp_avrc_tg_register_callback(void (*cb)(esp_avrc_tg_cb_event_t,esp_avrc_tg_cb_param_t *));
esp_err_t esp_avrc_tg_init(void);
esp_err_t esp_avrc_tg_deinit(void);
bool esp_avrc_rn_evt_bit_mask_operation(int op,esp_avrc_rn_evt_cap_mask_t *m,int event);
esp_err_t esp_avrc_tg_set_rn_evt_cap(const esp_avrc_rn_evt_cap_mask_t *m);
esp_err_t esp_avrc_tg_send_rn_rsp(int event,int type,esp_avrc_rn_param_t *value);

#define ESP_A2D_SBC_CIE_BLOCK_LEN_4             (0x8)

#define ESP_A2D_SBC_CIE_BLOCK_LEN_8             (0x4)

#define ESP_A2D_SBC_CIE_BLOCK_LEN_12            (0x2)

#define ESP_A2D_SBC_CIE_BLOCK_LEN_16            (0x1)

#define ESP_A2D_SBC_CIE_NUM_SUBBANDS_4          (0x2)

#define ESP_A2D_SBC_CIE_NUM_SUBBANDS_8          (0x1)

#define ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR          (0x2)

#define ESP_A2D_SBC_CIE_ALLOC_MTHD_SRN          _Pragma("GCC warning \"'ESP_A2D_SBC_CIE_ALLOC_MTHD_SRN' macro is deprecated, use 'ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR'\"") ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR

#define ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS     (0x1)

#define ESP_A2D_M24_CIE_DRC_SUPPORT             (0x1)

#define ESP_A2D_M24_CIE_DRC_NS                  (0x0)

#define ESP_A2D_M24_CIE_OBJ_TYPE_2_AAC_LC       (0x40)

#define ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC       (0x20)

#define ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LTP      (0x10)

#define ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_SCALABLE (0x08)

#define ESP_A2D_M24_CIE_OBJ_TYPE_4_HE_AAC       (0x04)

#define ESP_A2D_M24_CIE_OBJ_TYPE_4_HE_AAC_V2    (0x02)

#define ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_ELD_V2   (0x01)

#define ESP_A2D_M24_CIE_SF1_8K                  (0x80)

#define ESP_A2D_M24_CIE_SF1_11K                 (0x40)

#define ESP_A2D_M24_CIE_SF1_12K                 (0x20)

#define ESP_A2D_M24_CIE_SF1_16K                 (0x10)

#define ESP_A2D_M24_CIE_SF1_22K                 (0x08)

#define ESP_A2D_M24_CIE_SF1_24K                 (0x04)

#define ESP_A2D_M24_CIE_SF1_32K                 (0x02)

#define ESP_A2D_M24_CIE_SF1_44K                 (0x01)

#define ESP_A2D_M24_CIE_SF2_48K                 (0x08)

#define ESP_A2D_M24_CIE_SF2_64K                 (0x04)

#define ESP_A2D_M24_CIE_SF2_88K                 (0x02)

#define ESP_A2D_M24_CIE_SF2_96K                 (0x01)

#define ESP_A2D_M24_CIE_CH_1                    (0x08)

#define ESP_A2D_M24_CIE_CH_2                    (0x04)

#define ESP_A2D_M24_CIE_CH_6                    (0x02)

#define ESP_A2D_M24_CIE_CH_8                    (0x01)

#define ESP_A2D_M24_CIE_VBR_SUPPORT             (0x1)

#define ESP_A2D_M24_CIE_VBR_NS                  (0x0)

#define ESP_A2D_M24_CIE_BR1_MSK                 (0x7F)

#define ESP_A2D_M24_CIE_BR2_MSK                 (0xFF)

#define ESP_A2D_M24_CIE_BR3_MSK                 (0xFF)

#define ESP_A2D_MCT_NON_A2DP 255

#define ESP_A2D_PSC_DELAY_RPT 1U
#define ESP_A2D_SET_SUCCESS 0U
esp_err_t esp_a2d_sink_set_delay_value(uint16_t value);
