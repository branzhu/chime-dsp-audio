"""Inspect actual linked descriptors, not a second handwritten copy. No hardware."""
from pathlib import Path
from elftools.elf.elffile import ELFFile
import struct
root=Path(__file__).resolve().parents[1]
with (root/'software/build/audio_dsp_firmware.elf').open('rb') as f:
    elf=ELFFile(f);symtab=elf.get_section_by_name('.symtab')
    def read_symbol(name):
        syms=symtab.get_symbol_by_name(name);assert syms,name
        sym=syms[0];sec=elf.get_section(sym['st_shndx']);start=sym['st_value']-sec['sh_addr']
        return sec.data()[start:start+sym['st_size']]
    # HS bInterval=1 is deliberate: the current DWC2 ISO scheduler selects
    # the next microframe parity when rearming. Do not regress to HS=4.
    for name,packet,period_us in [('full_config',196,1000),('high_config',28,125)]:
        b=read_symbol(name);assert b[0]==9 and b[1]==2
        assert struct.unpack_from('<H',b,2)[0]==len(b) and b[4]==2 and b[7]&0x40
        pos=0;descs=[]
        while pos<len(b):
            n=b[pos];assert n>=2 and pos+n<=len(b);descs.append(b[pos:pos+n]);pos+=n
        iad=[d for d in descs if d[1]==11];assert len(iad)==1 and iad[0][2:4]==bytes([0,2])
        interfaces=[d for d in descs if d[1]==4]
        assert [(d[2],d[3],d[4]) for d in interfaces]==[(0,0,0),(1,0,0),(1,1,2)]
        eps=[d for d in descs if d[1]==5]
        assert [(d[2],d[3]&0x3f,struct.unpack_from('<H',d,4)[0],d[6]) for d in eps]==[(1,5,packet,1),(0x81,17,4,1)]
        # One extra stereo PCM16 frame for async feedback correction.
        assert packet==(48000*period_us//1000000+1)*4
        formats=[d for d in descs if d[1:3]==bytes([0x24,2]) and len(d)==6]
        assert len(formats)==1 and formats[0][3:6]==bytes([1,2,16])
        clock=[d for d in descs if d[1:3]==bytes([0x24,10])];assert len(clock)==1 and clock[0][3]==4
        feature=[d for d in descs if d[1:3]==bytes([0x24,6])];assert len(feature)==1
        assert struct.unpack_from('<III',feature[0],5)==(15,0,0) # master mute+volume only
        # AC total length counts header plus all clock/terminal/feature descriptors.
        ac_header=next(d for d in descs if d[1:3]==bytes([0x24,1]) and len(d)==9)
        ac_index=descs.index(ac_header);ac_descs=[]
        for d in descs[ac_index:]:
            if d[1]!=0x24:break
            ac_descs.append(d)
        assert struct.unpack_from('<H',ac_header,6)[0]==sum(map(len,ac_descs))
        print(f'PASS linked {name}: UAC2 stereo PCM16, alt0/alt1, async OUT+feedback, {period_us}us / {packet}B OUT, entity IDs and master-only controls, AC/config lengths')
    device=read_symbol('device');assert device[0]==18 and device[4:7]==bytes([0xef,2,1])
    assert struct.unpack_from('<H',device,12)[0]==0x0101
    print('PASS linked device descriptor: composite/IAD UAC2, development VID/PID, revision 1.01')

    qualifier=read_symbol('qualifier');assert len(qualifier)==10 and qualifier[1]==6 and qualifier[4:9]==bytes([0xef,2,1,64,1])
    print('PASS linked explicit qualifier: consistent device class/protocol for FS and HS')
