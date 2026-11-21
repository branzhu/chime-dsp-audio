#include "bench_status.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static bench_status_t sample(void)
{
    return (bench_status_t){.ip="192.168.0.1",.hostname="chime",.tcp_port=8086,
        .station_connected=true,.bridge_connected=true,.requests=17,.last_result=-105,
        .remaining_seconds=899};
}
int main(void)
{
    bench_status_t s=sample();char json[1536];
    assert(bench_status_json(&s,json,sizeof(json)));
    assert(strstr(json,"\"ip\":\"192.168.0.1\"")!=NULL);
    assert(strstr(json,"\"tcp_address\":\"192.168.0.1:8086\"")!=NULL);
    assert(strstr(json,"\"amplifiers_enabled\":false")!=NULL);
    assert(strstr(json,"\"audio_tested\":false")!=NULL);
    assert(strstr(json,"\"sound_attempted\":false")!=NULL);
    s.selected_source=2;s.rendering_source=0;s.bt_available=s.bt_connected=true;s.bt_rate=44100;
    assert(bench_status_json(&s,json,sizeof(json)));
    assert(strstr(json,"\"selected_source\":\"bluetooth\"")&&strstr(json,"\"rendering_source\":\"none\""));
    assert(strstr(json,"\"bt_connected\":true")&&strstr(json,"\"bt_rate\":44100"));
    assert(strstr(json,"\"bt_codec\":\"none\""));
    s.bt_codec=1;assert(bench_status_json(&s,json,sizeof(json))&&strstr(json,"\"bt_codec\":\"SBC\""));
    s.bt_codec=2;assert(bench_status_json(&s,json,sizeof(json))&&strstr(json,"\"bt_codec\":\"AAC\""));
    s.bt_codec=3;assert(bench_status_json(&s,json,sizeof(json))&&strstr(json,"\"bt_codec\":\"aptX\""));
    s.bt_codec=4;assert(bench_status_json(&s,json,sizeof(json))&&strstr(json,"\"bt_codec\":\"aptX HD\""));
    s.bt_codec=6;assert(bench_status_json(&s,json,sizeof(json))&&strstr(json,"\"bt_codec\":\"LDAC\""));
    s.bt_codec=7;assert(bench_status_json(&s,json,sizeof(json))&&strstr(json,"\"bt_codec\":\"LHDC V5\""));
    s.bt_codec=5;assert(bench_status_json(&s,json,sizeof(json))&&strstr(json,"\"bt_codec\":\"Opus\""));
    s.bt_codec=UINT_MAX;assert(bench_status_json(&s,json,sizeof(json))&&strstr(json,"\"bt_codec\":\"none\""));
    s.sound_attempted=true;s.sound_result=263;s.sound_failed_reg=0x37;
    assert(bench_status_json(&s,json,sizeof(json)));
    assert(strstr(json,"\"sound_attempted\":true")&&strstr(json,"\"sound_result\":263"));
    assert(strstr(json,"\"sound_failed_reg\":55"));
    assert(!bench_status_json(NULL,json,sizeof(json)) && !json[0]);
    assert(!bench_status_json(&s,NULL,sizeof(json)));
    assert(!bench_status_json(&s,json,0));
    const char *bad_ips[]={"", "192.168.0", "256.1.2.3", "1.2.3.4.5", "1.2.3.x", "1..2.3", "<script>", "1.2.3.4\"", "1.2.3.4 "};
    for(size_t i=0;i<sizeof(bad_ips)/sizeof(bad_ips[0]);++i) {
        s=sample();snprintf(s.ip,sizeof(s.ip),"%s",bad_ips[i]);
        assert(!bench_status_json(&s,json,sizeof(json)) && !json[0]);
    }
    const char *bad_hosts[]={"", "-dsp", "dsp-", "dsp.local", "dsp\"bridge", "<script>", "DSP", "a\nb"};
    for(size_t i=0;i<sizeof(bad_hosts)/sizeof(bad_hosts[0]);++i) {
        s=sample();snprintf(s.hostname,sizeof(s.hostname),"%s",bad_hosts[i]);
        assert(!bench_status_json(&s,json,sizeof(json)) && !json[0]);
    }
    s=sample();memset(s.ip,'1',sizeof(s.ip));assert(!bench_status_json(&s,json,sizeof(json)));
    s=sample();memset(s.hostname,'a',sizeof(s.hostname));assert(!bench_status_json(&s,json,sizeof(json)));
    s=sample();s.tcp_port=0;assert(!bench_status_json(&s,json,sizeof(json)));
    puts("PASS status JSON validation: malformed/unterminated IPv4/hostnames, injection and invalid port fail closed");
    s=sample();assert(bench_status_json(&s,json,sizeof(json)));size_t n=strlen(json);
    for(size_t cap=1;cap<=n;++cap) {memset(json,'X',sizeof(json));assert(!bench_status_json(&s,json,cap) && !json[0]);}
    assert(bench_status_json(&s,json,n+1));
    s=sample();memcpy(s.ip,"255.255.255.255",sizeof("255.255.255.255"));memset(s.hostname,'a',63);s.hostname[63]=0;
    s.tcp_port=65535;s.requests=UINT32_MAX;s.last_result=INT_MIN;s.remaining_seconds=UINT32_MAX;
    assert(bench_status_json(&s,json,sizeof(json)));
    s.station_connected=false;s.bridge_connected=false;
    assert(bench_status_json(&s,json,sizeof(json)) && strstr(json,"\"bridge_connected\":false"));
    puts("PASS status JSON exact buffer bounds, maximum address/hostname/counters, signed errors and disconnected state");
    s=sample();assert(bench_status_json(&s,json,sizeof(json)));puts(json);
    return 0;
}
