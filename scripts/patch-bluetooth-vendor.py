"""Generate minimal IDF6.1 vendor SINK hooks + delay-report range fix.
--delay-only emits only the range fix for SBC/AAC recovery builds. Never write global SDK files.
Original files retain Apache-2.0 notices; pin mismatch must fail closed.
"""
from pathlib import Path
import hashlib,json,sys

PINS=json.loads(Path(__file__).with_name('bluetooth-sdk-pins.json').read_text())

def replace_once(text,old,new):
    if text.count(old)!=1:
        raise ValueError('patch context mismatch: '+old[:100])
    return text.replace(old,new)

def function_edit(text,name,edit):
    import re
    m=re.search(r'^.*\b'+name+r'\s*\([^;]*?\)\n\{',text,re.M)
    if not m:raise ValueError('function not found: '+name)
    a=m.start();i=text.index('{',m.start());depth=1;j=i+1
    while depth:
        if text[j]=='{':depth+=1
        elif text[j]=='}':depth-=1
        j+=1
    return text[:a]+edit(text[a:j])+text[j:]

def delay_range(text):
    # AVDTP delay field is uint16 in 0.1ms units, not minimum 120ms.
    return replace_once(text,'    if (delay_value <= BTC_DELAY_REPORT_DFT_VALUE) {',
        '    if (delay_value == 0) { /* Chime: accept real delays below 120ms. */')

def patch(name,source):
    if hashlib.sha256(source).hexdigest()!=PINS[name]:raise ValueError('SDK changed; review/rebase '+name)
    s=source.decode('utf-8')
    if name.endswith('.h'):
        s=replace_once(s,'        esp_a2d_cie_sbc_t   sbc_info;', '        uint8_t vendor[12]; /* Chime: [CIE length, up to 11 CIE bytes] */\n        esp_a2d_cie_sbc_t   sbc_info;')
    else:
        s='#include "chime_bt_vendor.h"\n'+s
    if name.endswith('/btc_av.c'):
        s=delay_range(s)
        def reg(t):
            before='    switch (mcc->type) {'
            case='''    switch (mcc->type) {
        case ESP_A2D_MCT_NON_A2DP: {
            uint8_t wire[CHIME_VENDOR_WIRE_MAX]={0};
            unsigned len=mcc->cie.vendor[0];
            if(tsep!=AVDT_TSEP_SNK || (len!=7&&len!=8&&len!=11))break;
            wire[0]=(uint8_t)(len+2);wire[2]=0xff;
            memcpy(wire+3,mcc->cie.vendor+1,len);
            if(!chime_vendor_codec(wire))break;
            codec_type=0xff;btc_av_cb.codec_caps[seid].id=0xff;
            memcpy(btc_av_cb.codec_caps[seid].info,wire,sizeof(wire));
            BTA_AvRegSEP(BTA_AV_CHNL_AUDIO,seid,tsep,codec_type,btc_av_cb.codec_caps[seid].info,p_data_cback);
            return;
        }
'''
            t=replace_once(t,before,case)
            # A malformed vendor case must also produce an async registration failure.
            t=replace_once(t,'    BTA_AvRegSEP(BTA_AV_CHNL_AUDIO, seid, tsep, codec_type, btc_av_cb.codec_caps[seid].info, p_data_cback);','''    if(mcc->type==ESP_A2D_MCT_NON_A2DP){
        param.a2d_sep_reg_stat.reg_state=ESP_A2D_SEP_REG_UNSUPPORTED;
        btc_a2d_cb_to_app(ESP_A2D_SEP_REG_STATE_EVT,&param);return;
    }
    BTA_AvRegSEP(BTA_AV_CHNL_AUDIO, seid, tsep, codec_type, btc_av_cb.codec_caps[seid].info, p_data_cback);''')
            return t
        s=function_edit(s,'btc_av_reg_sep',reg)
        def media(t):
            pos='''        else {
            BTC_TRACE_ERROR("bte_av_media_sink_callback: unsup_codec'''
            insert='''        else if(p_codec[BTA_AV_CODEC_TYPE_IDX]==0xff){
            unsigned rate,channels;
            if(chime_vendor_format(p_codec,&rate,&channels)){
                btc_msg_t msg={.sig=BTC_SIG_API_CB,.pid=BTC_PID_A2DP,.act=BTC_AV_CONFIG_EVT};
                btc_av_args_t arg={0};arg.mcc.type=ESP_A2D_MCT_NON_A2DP;
                unsigned len=p_codec[0]-2;
                arg.mcc.cie.vendor[0]=(uint8_t)len;
                memcpy(arg.mcc.cie.vendor+1,p_codec+3,len);
                btc_transfer_context(&msg,&arg,sizeof(arg),NULL,NULL);
            }
        }
'''
            return replace_once(t,pos,insert+pos)
        s=function_edit(s,'bte_av_media_sink_callback',media)
    if name.endswith('/bta_av_co.c'):
        def vendor_case(t,body):
            return replace_once(t,'    default:',body+'\n    default:')
        s=function_edit(s,'bta_av_build_src_cfg',lambda t:vendor_case(t,'''    case 0xff:
        chime_vendor_select(p_src_cap,p_pref_cfg);break;'''))
        s=function_edit(s,'bta_av_co_audio_codec_build_config',lambda t:vendor_case(t,'''    case 0xff:
        if(!chime_vendor_supports(p_codec_caps,bta_av_co_cb.codec_cfg.info))return FALSE;
        memcpy(p_codec_cfg,bta_av_co_cb.codec_cfg.info,CHIME_VENDOR_WIRE_MAX);break;'''))
        s=function_edit(s,'bta_av_co_audio_codec_cfg_matches_caps',lambda t:vendor_case(t,'''    case 0xff:
        return chime_vendor_supports(p_codec_caps,p_codec_cfg);'''))
        s=function_edit(s,'bta_av_co_audio_sink_supports_config',lambda t:vendor_case(t,'''    case 0xff:
#if BTC_AV_EXT_CODEC == TRUE
        for(index=0;index<BTA_AV_MAX_SEPS;++index){
            if(bta_av_co_cb.codec_caps[index].id==0xff&&chime_vendor_supports(bta_av_co_cb.codec_caps[index].info,p_codec_cfg)){
                bta_av_co_cb.cur_seid=index;bta_av_co_audio_codec_reset();return A2D_SUCCESS;
            }
        }
#endif
        return A2D_NS_CODEC_TYPE;'''))
        s=function_edit(s,'bta_av_co_audio_setconfig',lambda t:vendor_case(t,'''            case 0xff:
                bta_av_co_cb.codec_cfg_setconfig.id=0xff;
                memcpy(bta_av_co_cb.codec_cfg_setconfig.info,p_codec_info,AVDT_CODEC_SIZE);
                memcpy(bta_av_co_cb.codec_cfg.info,p_codec_info,AVDT_CODEC_SIZE);
                recfg_needed=FALSE;break;'''))
        # Incoming phone connection is sufficient, but also support outgoing capability selection.
        s=function_edit(s,'bta_av_audio_sink_getconfig',lambda t:replace_once(t,'    case BTA_AV_CODEC_SBC:','    case 0xff:\n    case BTA_AV_CODEC_SBC:'))
        def peer(t):
            marker='''                default:
                    APPL_TRACE_ERROR("peer_src_supports_codec: unsupported codec id %d",'''
            return replace_once(t,marker,'''                case 0xff: {
                    uint8_t chosen[CHIME_VENDOR_WIRE_MAX];
                    if(chime_vendor_select(p_peer->srcs[index].codec_caps,chosen)&&
                       chime_vendor_supports(bta_av_co_cb.codec_caps[bta_av_co_cb.cur_seid].info,chosen)){
                        if (p_src_index) { *p_src_index=index; }
                        return TRUE;
                    }
                    break;
                }
'''+marker)
        s=function_edit(s,'bta_av_co_audio_peer_src_supports_codec',peer)
    if name.endswith('/avdt_scb_act.c'):
        def raw_aptx(t):
            return replace_once(t,'    /* parse media packet header */','''    /* Classic aptX has NO RTP header. Never inspect compressed bytes as RTP.
     * Reserve private metadata headroom before BTA stores its timestamp and
     * the external callback overlays esp_a2d_audio_buff_t on the allocation. */
    if (chime_vendor_codec(p_scb->curr_cfg.codec_info)==CHIME_VENDOR_APTX) {
        if (!len || !p_scb->cs.p_data_cback) { goto length_error; }
        BT_HDR *raw=osi_malloc(sizeof(BT_HDR)+32U+len);
        if (!raw) { goto length_error; }
        *raw=*p_data->p_pkt;raw->offset=32;raw->layer_specific=0;
        memcpy((UINT8 *)(raw+1)+raw->offset,p_start,len);
        osi_free(p_data->p_pkt);p_data->p_pkt=raw;
        (*p_scb->cs.p_data_cback)(avdt_scb_to_hdl(p_scb),raw,0,0);
        return;
    }
    /* All other enabled codecs retain stock RTP/CSRC/extension/padding parsing. */
    /* parse media packet header */''')
        s=function_edit(s,'avdt_scb_hdl_pkt_no_frag',raw_aptx)
    if name.endswith('/btc_a2dp_sink_ext_coedc.c'):
        s=replace_once(s,'    if (p_msg->layer_specific != a2dp_sink_local_param.media_pkt_seq_num.expected_seq_num) {',
            '    if (chime_vendor_codec(cur_codec_info.info)!=CHIME_VENDOR_APTX && p_msg->layer_specific != a2dp_sink_local_param.media_pkt_seq_num.expected_seq_num) {')
        s=replace_once(s,'    default:\n        APPL_TRACE_WARNING("%s: unsupported codec type', '''    case 0xff: {
        unsigned rate,channels;
        if(!chime_vendor_format(cur_codec_info.info,&rate,&channels)){
            osi_free(p_msg);return;
        }
        /* AVDTP already stripped RTP, CSRC, extension and padding. Classic aptX bypassed RTP parsing with private headroom. Keep the
         * Android Opus payload header for the task-owned assembler. */
        num_frames=1;break;
    }
    default:
        APPL_TRACE_WARNING("%s: unsupported codec type''')
    return s.encode('utf-8')

def generate(sdk,out):
    root=Path(sdk)/'host/bluedroid';out=Path(out)
    # Validate ALL inputs before changing any outputs.
    results={n:patch(n,(root/n).read_bytes()) for n in PINS}
    for n,b in results.items():
        dst=out/('esp_a2dp_api.h' if n.endswith('.h') else Path(n).name)
        dst.parent.mkdir(parents=True,exist_ok=True)
        if not dst.exists() or dst.read_bytes()!=b:dst.write_bytes(b)
    print('PASS pinned IDF vendor sink hooks: CIE, SEP, selection, setconfig, raw media; SDK untouched')

def generate_delay(sdk,out):
    name='btc/profile/std/a2dp/btc_av.c'
    source=(Path(sdk)/'host/bluedroid'/name).read_bytes()
    if hashlib.sha256(source).hexdigest()!=PINS[name]:raise ValueError('SDK changed; review/rebase '+name)
    result=delay_range(source.decode('utf-8')).encode('utf-8')
    dst=Path(out)/'btc_av.c';dst.parent.mkdir(parents=True,exist_ok=True)
    if not dst.exists() or dst.read_bytes()!=result:dst.write_bytes(result)
    print('PASS pinned IDF delay-only range: 0.1..6553.5ms; default retained; SDK untouched')

if __name__=='__main__':
    if sys.argv[1]=='--delay-only':generate_delay(*sys.argv[2:])
    else:generate(*sys.argv[1:])
