#include "bench_status.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool ipv4_valid(const char *ip)
{
    if(memchr(ip,0,16)==NULL) return false;
    const char *p=ip;
    for(unsigned part=0;part<4;++part) {
        unsigned value=0,digits=0;
        while(*p>='0' && *p<='9') {
            value=value*10U+(unsigned)(*p++-'0');
            if(++digits>3 || value>255) return false;
        }
        if(!digits) return false;
        if(part<3) {if(*p++!='.') return false;}
        else if(*p!=0) return false;
    }
    return true;
}
static bool hostname_valid(const char *name)
{
    const char *end=memchr(name,0,64);
    if(end==NULL || end==name || name[0]=='-' || end[-1]=='-') return false;
    for(const char *p=name;p<end;++p)
        if(!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='-')) return false;
    return true;
}
bool bench_status_json(const bench_status_t *s,char *out,size_t capacity)
{
    if(out==NULL || capacity==0) return false;
    out[0]=0;
    if(s==NULL || !ipv4_valid(s->ip) || !hostname_valid(s->hostname) || s->tcp_port==0) return false;
    int length=snprintf(out,capacity,
        "{\"ip\":\"%s\",\"hostname\":\"%s.local\",\"web_url\":\"http://%s/\","
        "\"tcp_port\":%u,\"tcp_address\":\"%s:%u\",\"station_connected\":%s,"
        "\"bridge_connected\":%s,\"dsp_default_ready\":%s,\"requests\":%" PRIu32 ",\"last_result\":%d,"
        "\"remaining_seconds\":%" PRIu32 ",\"amplifiers_enabled\":%s,\"sound_attempted\":%s,\"sound_result\":%d,\"sound_failed_reg\":%u,\"sound_completed_command\":%d,\"output_mode\":%u,\"output_pending\":%u,\"output_result\":%d,\"dsp_external\":%s,\"tuning_armed\":%u,\"tuning_pending\":%u,\"tuning_result\":%d,\"selected_source\":\"%s\",\"rendering_source\":\"%s\",\"source_stopped\":%s,\"bt_available\":%s,\"bt_connected\":%s,\"bt_playing\":%s,\"bt_codec\":\"%s\",\"bt_rate\":%u,\"bt_error\":%d,\"bt_queued\":%u,\"bt_drift_ppm\":%d,\"bt_overflows\":%" PRIu32 ",\"bt_underruns\":%" PRIu32 ",\"bt_stream_state\":%u,\"bt_rebuffer_events\":%u,\"bt_rebuffer_resumes\":%u,\"bt_rebuffer_wait_ms\":%u,\"audio_tested\":false}",
        s->ip,s->hostname,s->ip,(unsigned)s->tcp_port,s->ip,(unsigned)s->tcp_port,
        s->station_connected?"true":"false",s->bridge_connected?"true":"false",s->dsp_default_ready?"true":"false",
        s->requests,s->last_result,s->remaining_seconds,
        s->amplifiers_enabled?"true":"false",s->sound_attempted?"true":"false",s->sound_result,(unsigned)s->sound_failed_reg,s->sound_completed_command,s->output_mode,s->output_pending,s->output_result,s->dsp_external?"true":"false",s->tuning_armed,s->tuning_pending,s->tuning_result,
        s->selected_source==3?"mix":s->selected_source==3?"mix":s->selected_source==2?"bluetooth":"usb",s->rendering_source==3?"mix":s->rendering_source==3?"mix":s->rendering_source==2?"bluetooth":s->rendering_source==1?"usb":"none",
        s->source_stopped?"true":"false",s->bt_available?"true":"false",s->bt_connected?"true":"false",s->bt_playing?"true":"false",
        s->bt_codec==7?"LHDC V5":s->bt_codec==6?"LDAC":s->bt_codec==5?"Opus":s->bt_codec==4?"aptX HD":s->bt_codec==3?"aptX":s->bt_codec==2?"AAC":s->bt_codec==1?"SBC":"none",s->bt_rate,s->bt_error,s->bt_queued,s->bt_drift_ppm,s->bt_overflows,s->bt_underruns,s->bt_stream_state,s->bt_rebuffer_events,s->bt_rebuffer_resumes,s->bt_rebuffer_wait_ms);
    if(length<0 || (size_t)length>=capacity) {out[0]=0;return false;}
    return true;
}
