-- SigmaStudio+ 3.4.0 documented R/W + observed Chime/ADAU1451 annotations.
-- Load once: tshark -X lua_script:tools/wireshark/sigmadsp.lua -r capture.pcapng
-- No network access, files, firmware modifications, or active requests.
-- Protocol evidence/limits: tools/wireshark/README.md.
local proto = Proto("sigmadsp", "SigmaStudio+ TCP / ADAU1451")
local commands = {[0x09]="WRITE", [0x0a]="READ", [0x0b]="REPLY", [0x1b]="Observed INIT"}
local states = {[0]="Stopped", [1]="Running", [2]="Paused", [3]="Sleep (waiting for next sample)", [4]="Stalled (transient)"}
local f = {
    cmd=ProtoField.uint8("sigmadsp.cmd", "Command", base.HEX, commands),
    total=ProtoField.uint32("sigmadsp.total_len", "Total frame bytes", base.DEC),
    length=ProtoField.uint32("sigmadsp.data_len", "Requested / payload bytes", base.DEC),
    chip=ProtoField.uint8("sigmadsp.chip", "Wire chip ID (NOT physical I2C address)", base.HEX),
    flag=ProtoField.uint8("sigmadsp.flag", "Write flag", base.DEC, {[0]="Ordinary write", [1]="Safeload (native method; project-specific execution)"}),
    channel=ProtoField.uint8("sigmadsp.channel", "Write channel", base.DEC),
    addr=ProtoField.uint16("sigmadsp.addr", "DSP word address", base.HEX),
    register=ProtoField.string("sigmadsp.register", "Starting register name"),
    region=ProtoField.string("sigmadsp.region", "Starting region (current bench map)"),
    status=ProtoField.uint8("sigmadsp.status", "Read response status", base.DEC, {[0]="Success", [1]="Failure (failure envelope not native-verified)"}),
    reserved=ProtoField.bytes("sigmadsp.reserved", "Reserved"),
    data=ProtoField.bytes("sigmadsp.data", "Payload (raw wire order)"),
    word_addr=ProtoField.uint16("sigmadsp.word_addr", "Payload word address", base.HEX),
    word_register=ProtoField.string("sigmadsp.word_register", "Payload register name"),
    value16=ProtoField.uint16("sigmadsp.value16", "16-bit word (BE)", base.HEX),
    value32=ProtoField.uint32("sigmadsp.value32", "32-bit word (BE, bench transfer format)", base.HEX),
    core=ProtoField.uint16("sigmadsp.core_status", "CORE_STATUS[2:0]", base.DEC, states, 0x7),
    pll=ProtoField.uint16("sigmadsp.pll_locked", "PLL lock bit", base.DEC, {[0]="Not locked", [1]="Locked"}, 0x1),
    reset=ProtoField.uint16("sigmadsp.soft_reset", "SOFT_RESET (active low)", base.DEC, {[0]="Reset enabled", [1]="Reset disabled / normal operation"}, 0x1),
    init=ProtoField.bool("sigmadsp.observed_init", "Exact observed 3.4.0 INIT sequence"),
    request=ProtoField.framenum("sigmadsp.request_in", "Read request in frame"),
    response=ProtoField.framenum("sigmadsp.response_in", "Read response in frame"),
    elapsed=ProtoField.relative_time("sigmadsp.response_time", "Read response time"),
    matched=ProtoField.bool("sigmadsp.match", "FIFO read address / length / chip match"),
    close=ProtoField.string("sigmadsp.close", "TCP connection event"),
    ack=ProtoField.bool("sigmadsp.tcp_ack_only", "Bridge TCP ACK only (no application response)"),
    last=ProtoField.framenum("sigmadsp.last_command_in", "Last observed request / INIT frame"),
    pending=ProtoField.uint32("sigmadsp.pending_reads", "Unanswered observed reads at close (lower bound if limited)", base.DEC),
    partial=ProtoField.uint32("sigmadsp.partial_bytes_needed", "Additional bytes needed for incomplete PDU at close", base.DEC),
}
proto.fields = f
local ex = {
    malformed=ProtoExpert.new("sigmadsp.malformed", "Invalid frame; no guessed resynchronization", expert.group.MALFORMED, expert.severity.ERROR),
    unknown=ProtoExpert.new("sigmadsp.unknown", "Undocumented command / INIT variation", expert.group.UNDECODED, expert.severity.WARN),
    incomplete=ProtoExpert.new("sigmadsp.incomplete", "Incomplete frame / unavailable TCP reassembly", expert.group.REASSEMBLE, expert.severity.WARN),
    mismatch=ProtoExpert.new("sigmadsp.mismatch", "Read response differs from FIFO request", expert.group.SEQUENCE, expert.severity.WARN),
    orphan=ProtoExpert.new("sigmadsp.orphan", "No observed read request for response", expert.group.SEQUENCE, expert.severity.NOTE),
    note=ProtoExpert.new("sigmadsp.note", "Protocol / bench annotation", expert.group.PROTOCOL, expert.severity.NOTE),
    policy=ProtoExpert.new("sigmadsp.policy", "Outside current Chime bench policy (annotation, not a device result)", expert.group.PROTOCOL, expert.severity.WARN),
    failure=ProtoExpert.new("sigmadsp.failure", "Read response reports failure / unknown status", expert.group.RESPONSE_CODE, expert.severity.WARN),
    limit=ProtoExpert.new("sigmadsp.limit", "Capture correlation resource limit", expert.group.UNDECODED, expert.severity.WARN),
}
proto.experts = ex
proto.prefs.port = Pref.uint("TCP port", 8086, "0 disables automatic port binding; Decode As remains available")
proto.prefs.words = Pref.uint("Decoded payload words", 16, "0..256 words per PDU; full raw payload is always available")
proto.prefs.policy = Pref.bool("Annotate current Chime bench policy", true, "Single DSP wire 00/70, channel0, conservative RAM windows; not general ADI protocol restrictions")

-- BEGIN GENERATED REGISTER MAP: Rev.D overview, names/access only (530 entries).
local registers = {
    [0xf000]={"PLL_CTRL0","RW"},
    [0xf001]={"PLL_CTRL1","RW"},
    [0xf002]={"PLL_CLK_SRC","RW"},
    [0xf003]={"PLL_ENABLE","RW"},
    [0xf004]={"PLL_LOCK","R"},
    [0xf005]={"MCLK_OUT","RW"},
    [0xf006]={"PLL_WATCHDOG","RW"},
    [0xf020]={"CLK_GEN1_M","RW"},
    [0xf021]={"CLK_GEN1_N","RW"},
    [0xf022]={"CLK_GEN2_M","RW"},
    [0xf023]={"CLK_GEN2_N","RW"},
    [0xf024]={"CLK_GEN3_M","RW"},
    [0xf025]={"CLK_GEN3_N","RW"},
    [0xf026]={"CLK_GEN3_SRC","RW"},
    [0xf027]={"CLK_GEN3_LOCK","R"},
    [0xf050]={"POWER_ENABLE0","RW"},
    [0xf051]={"POWER_ENABLE1","RW"},
    [0xf100]={"ASRC_INPUT0","RW"},
    [0xf101]={"ASRC_INPUT1","RW"},
    [0xf102]={"ASRC_INPUT2","RW"},
    [0xf103]={"ASRC_INPUT3","RW"},
    [0xf104]={"ASRC_INPUT4","RW"},
    [0xf105]={"ASRC_INPUT5","RW"},
    [0xf106]={"ASRC_INPUT6","RW"},
    [0xf107]={"ASRC_INPUT7","RW"},
    [0xf140]={"ASRC_OUT_RATE0","RW"},
    [0xf141]={"ASRC_OUT_RATE1","RW"},
    [0xf142]={"ASRC_OUT_RATE2","RW"},
    [0xf143]={"ASRC_OUT_RATE3","RW"},
    [0xf144]={"ASRC_OUT_RATE4","RW"},
    [0xf145]={"ASRC_OUT_RATE5","RW"},
    [0xf146]={"ASRC_OUT_RATE6","RW"},
    [0xf147]={"ASRC_OUT_RATE7","RW"},
    [0xf180]={"SOUT_SOURCE0","RW"},
    [0xf181]={"SOUT_SOURCE1","RW"},
    [0xf182]={"SOUT_SOURCE2","RW"},
    [0xf183]={"SOUT_SOURCE3","RW"},
    [0xf184]={"SOUT_SOURCE4","RW"},
    [0xf185]={"SOUT_SOURCE5","RW"},
    [0xf186]={"SOUT_SOURCE6","RW"},
    [0xf187]={"SOUT_SOURCE7","RW"},
    [0xf188]={"SOUT_SOURCE8","RW"},
    [0xf189]={"SOUT_SOURCE9","RW"},
    [0xf18a]={"SOUT_SOURCE10","RW"},
    [0xf18b]={"SOUT_SOURCE11","RW"},
    [0xf18c]={"SOUT_SOURCE12","RW"},
    [0xf18d]={"SOUT_SOURCE13","RW"},
    [0xf18e]={"SOUT_SOURCE14","RW"},
    [0xf18f]={"SOUT_SOURCE15","RW"},
    [0xf190]={"SOUT_SOURCE16","RW"},
    [0xf191]={"SOUT_SOURCE17","RW"},
    [0xf192]={"SOUT_SOURCE18","RW"},
    [0xf193]={"SOUT_SOURCE19","RW"},
    [0xf194]={"SOUT_SOURCE20","RW"},
    [0xf195]={"SOUT_SOURCE21","RW"},
    [0xf196]={"SOUT_SOURCE22","RW"},
    [0xf197]={"SOUT_SOURCE23","RW"},
    [0xf1c0]={"SPDIFTX_INPUT","RW"},
    [0xf200]={"SERIAL_BYTE_0_0","RW"},
    [0xf201]={"SERIAL_BYTE_0_1","RW"},
    [0xf204]={"SERIAL_BYTE_1_0","RW"},
    [0xf205]={"SERIAL_BYTE_1_1","RW"},
    [0xf208]={"SERIAL_BYTE_2_0","RW"},
    [0xf209]={"SERIAL_BYTE_2_1","RW"},
    [0xf20c]={"SERIAL_BYTE_3_0","RW"},
    [0xf20d]={"SERIAL_BYTE_3_1","RW"},
    [0xf210]={"SERIAL_BYTE_4_0","RW"},
    [0xf211]={"SERIAL_BYTE_4_1","RW"},
    [0xf214]={"SERIAL_BYTE_5_0","RW"},
    [0xf215]={"SERIAL_BYTE_5_1","RW"},
    [0xf218]={"SERIAL_BYTE_6_0","RW"},
    [0xf219]={"SERIAL_BYTE_6_1","RW"},
    [0xf21c]={"SERIAL_BYTE_7_0","RW"},
    [0xf21d]={"SERIAL_BYTE_7_1","RW"},
    [0xf300]={"FTDM_IN0","RW"},
    [0xf301]={"FTDM_IN1","RW"},
    [0xf302]={"FTDM_IN2","RW"},
    [0xf303]={"FTDM_IN3","RW"},
    [0xf304]={"FTDM_IN4","RW"},
    [0xf305]={"FTDM_IN5","RW"},
    [0xf306]={"FTDM_IN6","RW"},
    [0xf307]={"FTDM_IN7","RW"},
    [0xf308]={"FTDM_IN8","RW"},
    [0xf309]={"FTDM_IN9","RW"},
    [0xf30a]={"FTDM_IN10","RW"},
    [0xf30b]={"FTDM_IN11","RW"},
    [0xf30c]={"FTDM_IN12","RW"},
    [0xf30d]={"FTDM_IN13","RW"},
    [0xf30e]={"FTDM_IN14","RW"},
    [0xf30f]={"FTDM_IN15","RW"},
    [0xf310]={"FTDM_IN16","RW"},
    [0xf311]={"FTDM_IN17","RW"},
    [0xf312]={"FTDM_IN18","RW"},
    [0xf313]={"FTDM_IN19","RW"},
    [0xf314]={"FTDM_IN20","RW"},
    [0xf315]={"FTDM_IN21","RW"},
    [0xf316]={"FTDM_IN22","RW"},
    [0xf317]={"FTDM_IN23","RW"},
    [0xf318]={"FTDM_IN24","RW"},
    [0xf319]={"FTDM_IN25","RW"},
    [0xf31a]={"FTDM_IN26","RW"},
    [0xf31b]={"FTDM_IN27","RW"},
    [0xf31c]={"FTDM_IN28","RW"},
    [0xf31d]={"FTDM_IN29","RW"},
    [0xf31e]={"FTDM_IN30","RW"},
    [0xf31f]={"FTDM_IN31","RW"},
    [0xf320]={"FTDM_IN32","RW"},
    [0xf321]={"FTDM_IN33","RW"},
    [0xf322]={"FTDM_IN34","RW"},
    [0xf323]={"FTDM_IN35","RW"},
    [0xf324]={"FTDM_IN36","RW"},
    [0xf325]={"FTDM_IN37","RW"},
    [0xf326]={"FTDM_IN38","RW"},
    [0xf327]={"FTDM_IN39","RW"},
    [0xf328]={"FTDM_IN40","RW"},
    [0xf329]={"FTDM_IN41","RW"},
    [0xf32a]={"FTDM_IN42","RW"},
    [0xf32b]={"FTDM_IN43","RW"},
    [0xf32c]={"FTDM_IN44","RW"},
    [0xf32d]={"FTDM_IN45","RW"},
    [0xf32e]={"FTDM_IN46","RW"},
    [0xf32f]={"FTDM_IN47","RW"},
    [0xf330]={"FTDM_IN48","RW"},
    [0xf331]={"FTDM_IN49","RW"},
    [0xf332]={"FTDM_IN50","RW"},
    [0xf333]={"FTDM_IN51","RW"},
    [0xf334]={"FTDM_IN52","RW"},
    [0xf335]={"FTDM_IN53","RW"},
    [0xf336]={"FTDM_IN54","RW"},
    [0xf337]={"FTDM_IN55","RW"},
    [0xf338]={"FTDM_IN56","RW"},
    [0xf339]={"FTDM_IN57","RW"},
    [0xf33a]={"FTDM_IN58","RW"},
    [0xf33b]={"FTDM_IN59","RW"},
    [0xf33c]={"FTDM_IN60","RW"},
    [0xf33d]={"FTDM_IN61","RW"},
    [0xf33e]={"FTDM_IN62","RW"},
    [0xf33f]={"FTDM_IN63","RW"},
    [0xf380]={"FTDM_OUT0","RW"},
    [0xf381]={"FTDM_OUT1","RW"},
    [0xf382]={"FTDM_OUT2","RW"},
    [0xf383]={"FTDM_OUT3","RW"},
    [0xf384]={"FTDM_OUT4","RW"},
    [0xf385]={"FTDM_OUT5","RW"},
    [0xf386]={"FTDM_OUT6","RW"},
    [0xf387]={"FTDM_OUT7","RW"},
    [0xf388]={"FTDM_OUT8","RW"},
    [0xf389]={"FTDM_OUT9","RW"},
    [0xf38a]={"FTDM_OUT10","RW"},
    [0xf38b]={"FTDM_OUT11","RW"},
    [0xf38c]={"FTDM_OUT12","RW"},
    [0xf38d]={"FTDM_OUT13","RW"},
    [0xf38e]={"FTDM_OUT14","RW"},
    [0xf38f]={"FTDM_OUT15","RW"},
    [0xf390]={"FTDM_OUT16","RW"},
    [0xf391]={"FTDM_OUT17","RW"},
    [0xf392]={"FTDM_OUT18","RW"},
    [0xf393]={"FTDM_OUT19","RW"},
    [0xf394]={"FTDM_OUT20","RW"},
    [0xf395]={"FTDM_OUT21","RW"},
    [0xf396]={"FTDM_OUT22","RW"},
    [0xf397]={"FTDM_OUT23","RW"},
    [0xf398]={"FTDM_OUT24","RW"},
    [0xf399]={"FTDM_OUT25","RW"},
    [0xf39a]={"FTDM_OUT26","RW"},
    [0xf39b]={"FTDM_OUT27","RW"},
    [0xf39c]={"FTDM_OUT28","RW"},
    [0xf39d]={"FTDM_OUT29","RW"},
    [0xf39e]={"FTDM_OUT30","RW"},
    [0xf39f]={"FTDM_OUT31","RW"},
    [0xf3a0]={"FTDM_OUT32","RW"},
    [0xf3a1]={"FTDM_OUT33","RW"},
    [0xf3a2]={"FTDM_OUT34","RW"},
    [0xf3a3]={"FTDM_OUT35","RW"},
    [0xf3a4]={"FTDM_OUT36","RW"},
    [0xf3a5]={"FTDM_OUT37","RW"},
    [0xf3a6]={"FTDM_OUT38","RW"},
    [0xf3a7]={"FTDM_OUT39","RW"},
    [0xf3a8]={"FTDM_OUT40","RW"},
    [0xf3a9]={"FTDM_OUT41","RW"},
    [0xf3aa]={"FTDM_OUT42","RW"},
    [0xf3ab]={"FTDM_OUT43","RW"},
    [0xf3ac]={"FTDM_OUT44","RW"},
    [0xf3ad]={"FTDM_OUT45","RW"},
    [0xf3ae]={"FTDM_OUT46","RW"},
    [0xf3af]={"FTDM_OUT47","RW"},
    [0xf3b0]={"FTDM_OUT48","RW"},
    [0xf3b1]={"FTDM_OUT49","RW"},
    [0xf3b2]={"FTDM_OUT50","RW"},
    [0xf3b3]={"FTDM_OUT51","RW"},
    [0xf3b4]={"FTDM_OUT52","RW"},
    [0xf3b5]={"FTDM_OUT53","RW"},
    [0xf3b6]={"FTDM_OUT54","RW"},
    [0xf3b7]={"FTDM_OUT55","RW"},
    [0xf3b8]={"FTDM_OUT56","RW"},
    [0xf3b9]={"FTDM_OUT57","RW"},
    [0xf3ba]={"FTDM_OUT58","RW"},
    [0xf3bb]={"FTDM_OUT59","RW"},
    [0xf3bc]={"FTDM_OUT60","RW"},
    [0xf3bd]={"FTDM_OUT61","RW"},
    [0xf3be]={"FTDM_OUT62","RW"},
    [0xf3bf]={"FTDM_OUT63","RW"},
    [0xf400]={"HIBERNATE","RW"},
    [0xf401]={"START_PULSE","RW"},
    [0xf402]={"START_CORE","RW"},
    [0xf403]={"KILL_CORE","RW"},
    [0xf404]={"START_ADDRESS","RW"},
    [0xf405]={"CORE_STATUS","R"},
    [0xf421]={"PANIC_CLEAR","RW"},
    [0xf422]={"PANIC_PARITY_MASK","RW"},
    [0xf423]={"PANIC_SOFTWARE_MASK","RW"},
    [0xf424]={"PANIC_WD_MASK","RW"},
    [0xf425]={"PANIC_STACK_MASK","RW"},
    [0xf426]={"PANIC_LOOP_MASK","RW"},
    [0xf427]={"PANIC_FLAG","R"},
    [0xf428]={"PANIC_CODE","R"},
    [0xf432]={"EXECUTE_COUNT","R"},
    [0xf433]={"SOFTWARE_VALUE_0","RW"},
    [0xf434]={"SOFTWARE_VALUE_1","RW"},
    [0xf443]={"WATCHDOG_MAXCOUNT","RW"},
    [0xf444]={"WATCHDOG_PRESCALE","RW"},
    [0xf450]={"BLOCKINT_EN","RW"},
    [0xf451]={"BLOCKINT_VALUE","RW"},
    [0xf460]={"PROG_CNTR0","R"},
    [0xf461]={"PROG_CNTR1","R"},
    [0xf462]={"PROG_CNTR_CLEAR","RW"},
    [0xf463]={"PROG_CNTR_LENGTH0","R"},
    [0xf464]={"PROG_CNTR_LENGTH1","R"},
    [0xf465]={"PROG_CNTR_MAXLENGTH0","R"},
    [0xf466]={"PROG_CNTR_MAXLENGTH1","R"},
    [0xf510]={"MP0_MODE","RW"},
    [0xf511]={"MP1_MODE","RW"},
    [0xf512]={"MP2_MODE","RW"},
    [0xf513]={"MP3_MODE","RW"},
    [0xf514]={"MP4_MODE","RW"},
    [0xf515]={"MP5_MODE","RW"},
    [0xf516]={"MP6_MODE","RW"},
    [0xf517]={"MP7_MODE","RW"},
    [0xf518]={"MP8_MODE","RW"},
    [0xf519]={"MP9_MODE","RW"},
    [0xf51a]={"MP10_MODE","RW"},
    [0xf51b]={"MP11_MODE","RW"},
    [0xf51c]={"MP12_MODE","RW"},
    [0xf51d]={"MP13_MODE","RW"},
    [0xf520]={"MP0_WRITE","RW"},
    [0xf521]={"MP1_WRITE","RW"},
    [0xf522]={"MP2_WRITE","RW"},
    [0xf523]={"MP3_WRITE","RW"},
    [0xf524]={"MP4_WRITE","RW"},
    [0xf525]={"MP5_WRITE","RW"},
    [0xf526]={"MP6_WRITE","RW"},
    [0xf527]={"MP7_WRITE","RW"},
    [0xf528]={"MP8_WRITE","RW"},
    [0xf529]={"MP9_WRITE","RW"},
    [0xf52a]={"MP10_WRITE","RW"},
    [0xf52b]={"MP11_WRITE","RW"},
    [0xf52c]={"MP12_WRITE","RW"},
    [0xf52d]={"MP13_WRITE","RW"},
    [0xf530]={"MP0_READ","R"},
    [0xf531]={"MP1_READ","R"},
    [0xf532]={"MP2_READ","R"},
    [0xf533]={"MP3_READ","R"},
    [0xf534]={"MP4_READ","R"},
    [0xf535]={"MP5_READ","R"},
    [0xf536]={"MP6_READ","R"},
    [0xf537]={"MP7_READ","R"},
    [0xf538]={"MP8_READ","R"},
    [0xf539]={"MP9_READ","R"},
    [0xf53a]={"MP10_READ","R"},
    [0xf53b]={"MP11_READ","R"},
    [0xf53c]={"MP12_READ","R"},
    [0xf53d]={"MP13_READ","R"},
    [0xf560]={"DMIC_CTRL0","RW"},
    [0xf561]={"DMIC_CTRL1","RW"},
    [0xf580]={"ASRC_LOCK","R"},
    [0xf581]={"ASRC_MUTE","RW"},
    [0xf582]={"ASRC0_RATIO","R"},
    [0xf583]={"ASRC1_RATIO","R"},
    [0xf584]={"ASRC2_RATIO","R"},
    [0xf585]={"ASRC3_RATIO","R"},
    [0xf586]={"ASRC4_RATIO","R"},
    [0xf587]={"ASRC5_RATIO","R"},
    [0xf588]={"ASRC6_RATIO","R"},
    [0xf589]={"ASRC7_RATIO","R"},
    [0xf5a0]={"ADC_READ0","R"},
    [0xf5a1]={"ADC_READ1","R"},
    [0xf5a2]={"ADC_READ2","R"},
    [0xf5a3]={"ADC_READ3","R"},
    [0xf5a4]={"ADC_READ4","R"},
    [0xf5a5]={"ADC_READ5","R"},
    [0xf600]={"SPDIF_LOCK_DET","R"},
    [0xf601]={"SPDIF_RX_CTRL","RW"},
    [0xf602]={"SPDIF_RX_DECODE","R"},
    [0xf603]={"SPDIF_RX_COMPRMODE","R"},
    [0xf604]={"SPDIF_RESTART","RW"},
    [0xf605]={"SPDIF_LOSS_OF_LOCK","R"},
    [0xf608]={"SPDIF_AUX_EN","RW"},
    [0xf60f]={"SPDIF_RX_AUXBIT_READY","R"},
    [0xf610]={"SPDIF_RX_CS_LEFT_0","R"},
    [0xf611]={"SPDIF_RX_CS_LEFT_1","R"},
    [0xf612]={"SPDIF_RX_CS_LEFT_2","R"},
    [0xf613]={"SPDIF_RX_CS_LEFT_3","R"},
    [0xf614]={"SPDIF_RX_CS_LEFT_4","R"},
    [0xf615]={"SPDIF_RX_CS_LEFT_5","R"},
    [0xf616]={"SPDIF_RX_CS_LEFT_6","R"},
    [0xf617]={"SPDIF_RX_CS_LEFT_7","R"},
    [0xf618]={"SPDIF_RX_CS_LEFT_8","R"},
    [0xf619]={"SPDIF_RX_CS_LEFT_9","R"},
    [0xf61a]={"SPDIF_RX_CS_LEFT_10","R"},
    [0xf61b]={"SPDIF_RX_CS_LEFT_11","R"},
    [0xf620]={"SPDIF_RX_CS_RIGHT_0","R"},
    [0xf621]={"SPDIF_RX_CS_RIGHT_1","R"},
    [0xf622]={"SPDIF_RX_CS_RIGHT_2","R"},
    [0xf623]={"SPDIF_RX_CS_RIGHT_3","R"},
    [0xf624]={"SPDIF_RX_CS_RIGHT_4","R"},
    [0xf625]={"SPDIF_RX_CS_RIGHT_5","R"},
    [0xf626]={"SPDIF_RX_CS_RIGHT_6","R"},
    [0xf627]={"SPDIF_RX_CS_RIGHT_7","R"},
    [0xf628]={"SPDIF_RX_CS_RIGHT_8","R"},
    [0xf629]={"SPDIF_RX_CS_RIGHT_9","R"},
    [0xf62a]={"SPDIF_RX_CS_RIGHT_10","R"},
    [0xf62b]={"SPDIF_RX_CS_RIGHT_11","R"},
    [0xf630]={"SPDIF_RX_UD_LEFT_0","R"},
    [0xf631]={"SPDIF_RX_UD_LEFT_1","R"},
    [0xf632]={"SPDIF_RX_UD_LEFT_2","R"},
    [0xf633]={"SPDIF_RX_UD_LEFT_3","R"},
    [0xf634]={"SPDIF_RX_UD_LEFT_4","R"},
    [0xf635]={"SPDIF_RX_UD_LEFT_5","R"},
    [0xf636]={"SPDIF_RX_UD_LEFT_6","R"},
    [0xf637]={"SPDIF_RX_UD_LEFT_7","R"},
    [0xf638]={"SPDIF_RX_UD_LEFT_8","R"},
    [0xf639]={"SPDIF_RX_UD_LEFT_9","R"},
    [0xf63a]={"SPDIF_RX_UD_LEFT_10","R"},
    [0xf63b]={"SPDIF_RX_UD_LEFT_11","R"},
    [0xf640]={"SPDIF_RX_UD_RIGHT_0","R"},
    [0xf641]={"SPDIF_RX_UD_RIGHT_1","R"},
    [0xf642]={"SPDIF_RX_UD_RIGHT_2","R"},
    [0xf643]={"SPDIF_RX_UD_RIGHT_3","R"},
    [0xf644]={"SPDIF_RX_UD_RIGHT_4","R"},
    [0xf645]={"SPDIF_RX_UD_RIGHT_5","R"},
    [0xf646]={"SPDIF_RX_UD_RIGHT_6","R"},
    [0xf647]={"SPDIF_RX_UD_RIGHT_7","R"},
    [0xf648]={"SPDIF_RX_UD_RIGHT_8","R"},
    [0xf649]={"SPDIF_RX_UD_RIGHT_9","R"},
    [0xf64a]={"SPDIF_RX_UD_RIGHT_10","R"},
    [0xf64b]={"SPDIF_RX_UD_RIGHT_11","R"},
    [0xf650]={"SPDIF_RX_VB_LEFT_0","R"},
    [0xf651]={"SPDIF_RX_VB_LEFT_1","R"},
    [0xf652]={"SPDIF_RX_VB_LEFT_2","R"},
    [0xf653]={"SPDIF_RX_VB_LEFT_3","R"},
    [0xf654]={"SPDIF_RX_VB_LEFT_4","R"},
    [0xf655]={"SPDIF_RX_VB_LEFT_5","R"},
    [0xf656]={"SPDIF_RX_VB_LEFT_6","R"},
    [0xf657]={"SPDIF_RX_VB_LEFT_7","R"},
    [0xf658]={"SPDIF_RX_VB_LEFT_8","R"},
    [0xf659]={"SPDIF_RX_VB_LEFT_9","R"},
    [0xf65a]={"SPDIF_RX_VB_LEFT_10","R"},
    [0xf65b]={"SPDIF_RX_VB_LEFT_11","R"},
    [0xf660]={"SPDIF_RX_VB_RIGHT_0","R"},
    [0xf661]={"SPDIF_RX_VB_RIGHT_1","R"},
    [0xf662]={"SPDIF_RX_VB_RIGHT_2","R"},
    [0xf663]={"SPDIF_RX_VB_RIGHT_3","R"},
    [0xf664]={"SPDIF_RX_VB_RIGHT_4","R"},
    [0xf665]={"SPDIF_RX_VB_RIGHT_5","R"},
    [0xf666]={"SPDIF_RX_VB_RIGHT_6","R"},
    [0xf667]={"SPDIF_RX_VB_RIGHT_7","R"},
    [0xf668]={"SPDIF_RX_VB_RIGHT_8","R"},
    [0xf669]={"SPDIF_RX_VB_RIGHT_9","R"},
    [0xf66a]={"SPDIF_RX_VB_RIGHT_10","R"},
    [0xf66b]={"SPDIF_RX_VB_RIGHT_11","R"},
    [0xf670]={"SPDIF_RX_PB_LEFT_0","R"},
    [0xf671]={"SPDIF_RX_PB_LEFT_1","R"},
    [0xf672]={"SPDIF_RX_PB_LEFT_2","R"},
    [0xf673]={"SPDIF_RX_PB_LEFT_3","R"},
    [0xf674]={"SPDIF_RX_PB_LEFT_4","R"},
    [0xf675]={"SPDIF_RX_PB_LEFT_5","R"},
    [0xf676]={"SPDIF_RX_PB_LEFT_6","R"},
    [0xf677]={"SPDIF_RX_PB_LEFT_7","R"},
    [0xf678]={"SPDIF_RX_PB_LEFT_8","R"},
    [0xf679]={"SPDIF_RX_PB_LEFT_9","R"},
    [0xf67a]={"SPDIF_RX_PB_LEFT_10","R"},
    [0xf67b]={"SPDIF_RX_PB_LEFT_11","R"},
    [0xf680]={"SPDIF_RX_PB_RIGHT_0","R"},
    [0xf681]={"SPDIF_RX_PB_RIGHT_1","R"},
    [0xf682]={"SPDIF_RX_PB_RIGHT_2","R"},
    [0xf683]={"SPDIF_RX_PB_RIGHT_3","R"},
    [0xf684]={"SPDIF_RX_PB_RIGHT_4","R"},
    [0xf685]={"SPDIF_RX_PB_RIGHT_5","R"},
    [0xf686]={"SPDIF_RX_PB_RIGHT_6","R"},
    [0xf687]={"SPDIF_RX_PB_RIGHT_7","R"},
    [0xf688]={"SPDIF_RX_PB_RIGHT_8","R"},
    [0xf689]={"SPDIF_RX_PB_RIGHT_9","R"},
    [0xf68a]={"SPDIF_RX_PB_RIGHT_10","R"},
    [0xf68b]={"SPDIF_RX_PB_RIGHT_11","R"},
    [0xf690]={"SPDIF_TX_EN","RW"},
    [0xf691]={"SPDIF_TX_CTRL","RW"},
    [0xf69f]={"SPDIF_TX_AUXBIT_SOURCE","RW"},
    [0xf6a0]={"SPDIF_TX_CS_LEFT_0","RW"},
    [0xf6a1]={"SPDIF_TX_CS_LEFT_1","RW"},
    [0xf6a2]={"SPDIF_TX_CS_LEFT_2","RW"},
    [0xf6a3]={"SPDIF_TX_CS_LEFT_3","RW"},
    [0xf6a4]={"SPDIF_TX_CS_LEFT_4","RW"},
    [0xf6a5]={"SPDIF_TX_CS_LEFT_5","RW"},
    [0xf6a6]={"SPDIF_TX_CS_LEFT_6","RW"},
    [0xf6a7]={"SPDIF_TX_CS_LEFT_7","RW"},
    [0xf6a8]={"SPDIF_TX_CS_LEFT_8","RW"},
    [0xf6a9]={"SPDIF_TX_CS_LEFT_9","RW"},
    [0xf6aa]={"SPDIF_TX_CS_LEFT_10","RW"},
    [0xf6ab]={"SPDIF_TX_CS_LEFT_11","RW"},
    [0xf6b0]={"SPDIF_TX_CS_RIGHT_0","RW"},
    [0xf6b1]={"SPDIF_TX_CS_RIGHT_1","RW"},
    [0xf6b2]={"SPDIF_TX_CS_RIGHT_2","RW"},
    [0xf6b3]={"SPDIF_TX_CS_RIGHT_3","RW"},
    [0xf6b4]={"SPDIF_TX_CS_RIGHT_4","RW"},
    [0xf6b5]={"SPDIF_TX_CS_RIGHT_5","RW"},
    [0xf6b6]={"SPDIF_TX_CS_RIGHT_6","RW"},
    [0xf6b7]={"SPDIF_TX_CS_RIGHT_7","RW"},
    [0xf6b8]={"SPDIF_TX_CS_RIGHT_8","RW"},
    [0xf6b9]={"SPDIF_TX_CS_RIGHT_9","RW"},
    [0xf6ba]={"SPDIF_TX_CS_RIGHT_10","RW"},
    [0xf6bb]={"SPDIF_TX_CS_RIGHT_11","RW"},
    [0xf6c0]={"SPDIF_TX_UD_LEFT_0","RW"},
    [0xf6c1]={"SPDIF_TX_UD_LEFT_1","RW"},
    [0xf6c2]={"SPDIF_TX_UD_LEFT_2","RW"},
    [0xf6c3]={"SPDIF_TX_UD_LEFT_3","RW"},
    [0xf6c4]={"SPDIF_TX_UD_LEFT_4","RW"},
    [0xf6c5]={"SPDIF_TX_UD_LEFT_5","RW"},
    [0xf6c6]={"SPDIF_TX_UD_LEFT_6","RW"},
    [0xf6c7]={"SPDIF_TX_UD_LEFT_7","RW"},
    [0xf6c8]={"SPDIF_TX_UD_LEFT_8","RW"},
    [0xf6c9]={"SPDIF_TX_UD_LEFT_9","RW"},
    [0xf6ca]={"SPDIF_TX_UD_LEFT_10","RW"},
    [0xf6cb]={"SPDIF_TX_UD_LEFT_11","RW"},
    [0xf6d0]={"SPDIF_TX_UD_RIGHT_0","RW"},
    [0xf6d1]={"SPDIF_TX_UD_RIGHT_1","RW"},
    [0xf6d2]={"SPDIF_TX_UD_RIGHT_2","RW"},
    [0xf6d3]={"SPDIF_TX_UD_RIGHT_3","RW"},
    [0xf6d4]={"SPDIF_TX_UD_RIGHT_4","RW"},
    [0xf6d5]={"SPDIF_TX_UD_RIGHT_5","RW"},
    [0xf6d6]={"SPDIF_TX_UD_RIGHT_6","RW"},
    [0xf6d7]={"SPDIF_TX_UD_RIGHT_7","RW"},
    [0xf6d8]={"SPDIF_TX_UD_RIGHT_8","RW"},
    [0xf6d9]={"SPDIF_TX_UD_RIGHT_9","RW"},
    [0xf6da]={"SPDIF_TX_UD_RIGHT_10","RW"},
    [0xf6db]={"SPDIF_TX_UD_RIGHT_11","RW"},
    [0xf6e0]={"SPDIF_TX_VB_LEFT_0","RW"},
    [0xf6e1]={"SPDIF_TX_VB_LEFT_1","RW"},
    [0xf6e2]={"SPDIF_TX_VB_LEFT_2","RW"},
    [0xf6e3]={"SPDIF_TX_VB_LEFT_3","RW"},
    [0xf6e4]={"SPDIF_TX_VB_LEFT_4","RW"},
    [0xf6e5]={"SPDIF_TX_VB_LEFT_5","RW"},
    [0xf6e6]={"SPDIF_TX_VB_LEFT_6","RW"},
    [0xf6e7]={"SPDIF_TX_VB_LEFT_7","RW"},
    [0xf6e8]={"SPDIF_TX_VB_LEFT_8","RW"},
    [0xf6e9]={"SPDIF_TX_VB_LEFT_9","RW"},
    [0xf6ea]={"SPDIF_TX_VB_LEFT_10","RW"},
    [0xf6eb]={"SPDIF_TX_VB_LEFT_11","RW"},
    [0xf6f0]={"SPDIF_TX_VB_RIGHT_0","RW"},
    [0xf6f1]={"SPDIF_TX_VB_RIGHT_1","RW"},
    [0xf6f2]={"SPDIF_TX_VB_RIGHT_2","RW"},
    [0xf6f3]={"SPDIF_TX_VB_RIGHT_3","RW"},
    [0xf6f4]={"SPDIF_TX_VB_RIGHT_4","RW"},
    [0xf6f5]={"SPDIF_TX_VB_RIGHT_5","RW"},
    [0xf6f6]={"SPDIF_TX_VB_RIGHT_6","RW"},
    [0xf6f7]={"SPDIF_TX_VB_RIGHT_7","RW"},
    [0xf6f8]={"SPDIF_TX_VB_RIGHT_8","RW"},
    [0xf6f9]={"SPDIF_TX_VB_RIGHT_9","RW"},
    [0xf6fa]={"SPDIF_TX_VB_RIGHT_10","RW"},
    [0xf6fb]={"SPDIF_TX_VB_RIGHT_11","RW"},
    [0xf700]={"SPDIF_TX_PB_LEFT_0","RW"},
    [0xf701]={"SPDIF_TX_PB_LEFT_1","RW"},
    [0xf702]={"SPDIF_TX_PB_LEFT_2","RW"},
    [0xf703]={"SPDIF_TX_PB_LEFT_3","RW"},
    [0xf704]={"SPDIF_TX_PB_LEFT_4","RW"},
    [0xf705]={"SPDIF_TX_PB_LEFT_5","RW"},
    [0xf706]={"SPDIF_TX_PB_LEFT_6","RW"},
    [0xf707]={"SPDIF_TX_PB_LEFT_7","RW"},
    [0xf708]={"SPDIF_TX_PB_LEFT_8","RW"},
    [0xf709]={"SPDIF_TX_PB_LEFT_9","RW"},
    [0xf70a]={"SPDIF_TX_PB_LEFT_10","RW"},
    [0xf70b]={"SPDIF_TX_PB_LEFT_11","RW"},
    [0xf710]={"SPDIF_TX_PB_RIGHT_0","RW"},
    [0xf711]={"SPDIF_TX_PB_RIGHT_1","RW"},
    [0xf712]={"SPDIF_TX_PB_RIGHT_2","RW"},
    [0xf713]={"SPDIF_TX_PB_RIGHT_3","RW"},
    [0xf714]={"SPDIF_TX_PB_RIGHT_4","RW"},
    [0xf715]={"SPDIF_TX_PB_RIGHT_5","RW"},
    [0xf716]={"SPDIF_TX_PB_RIGHT_6","RW"},
    [0xf717]={"SPDIF_TX_PB_RIGHT_7","RW"},
    [0xf718]={"SPDIF_TX_PB_RIGHT_8","RW"},
    [0xf719]={"SPDIF_TX_PB_RIGHT_9","RW"},
    [0xf71a]={"SPDIF_TX_PB_RIGHT_10","RW"},
    [0xf71b]={"SPDIF_TX_PB_RIGHT_11","RW"},
    [0xf780]={"BCLK_IN0_PIN","RW"},
    [0xf781]={"BCLK_IN1_PIN","RW"},
    [0xf782]={"BCLK_IN2_PIN","RW"},
    [0xf783]={"BCLK_IN3_PIN","RW"},
    [0xf784]={"BCLK_OUT0_PIN","RW"},
    [0xf785]={"BCLK_OUT1_PIN","RW"},
    [0xf786]={"BCLK_OUT2_PIN","RW"},
    [0xf787]={"BCLK_OUT3_PIN","RW"},
    [0xf788]={"LRCLK_IN0_PIN","RW"},
    [0xf789]={"LRCLK_IN1_PIN","RW"},
    [0xf78a]={"LRCLK_IN2_PIN","RW"},
    [0xf78b]={"LRCLK_IN3_PIN","RW"},
    [0xf78c]={"LRCLK_OUT0_PIN","RW"},
    [0xf78d]={"LRCLK_OUT1_PIN","RW"},
    [0xf78e]={"LRCLK_OUT2_PIN","RW"},
    [0xf78f]={"LRCLK_OUT3_PIN","RW"},
    [0xf790]={"SDATA_IN0_PIN","RW"},
    [0xf791]={"SDATA_IN1_PIN","RW"},
    [0xf792]={"SDATA_IN2_PIN","RW"},
    [0xf793]={"SDATA_IN3_PIN","RW"},
    [0xf794]={"SDATA_OUT0_PIN","RW"},
    [0xf795]={"SDATA_OUT1_PIN","RW"},
    [0xf796]={"SDATA_OUT2_PIN","RW"},
    [0xf797]={"SDATA_OUT3_PIN","RW"},
    [0xf798]={"SPDIF_TX_PIN","RW"},
    [0xf799]={"SCLK_SCL_PIN","RW"},
    [0xf79a]={"MISO_SDA_PIN","RW"},
    [0xf79b]={"SS_PIN","RW"},
    [0xf79c]={"MOSI_ADDR1_PIN","RW"},
    [0xf79d]={"SCLK_SCL_M_PIN","RW"},
    [0xf79e]={"MISO_SDA_M_PIN","RW"},
    [0xf79f]={"SS_M_PIN","RW"},
    [0xf7a0]={"MOSI_M_PIN","RW"},
    [0xf7a1]={"MP6_PIN","RW"},
    [0xf7a2]={"MP7_PIN","RW"},
    [0xf7a3]={"CLKOUT_PIN","RW"},
    [0xf890]={"SOFT_RESET","RW"},
}
-- END GENERATED REGISTER MAP

local tcp_stream = Field.new("tcp.stream")
local tcp_fin = Field.new("tcp.flags.fin")
local tcp_rst = Field.new("tcp.flags.reset")
local tcp_ack = Field.new("tcp.flags.ack")
local tcp_syn = Field.new("tcp.flags.syn")
local tcp_len = Field.new("tcp.len")
local records, streams, closes, record_count, stream_count
local MAX_RECORDS, MAX_STREAMS, MAX_PENDING = 200000, 10000, 4096
local function reset_state()
    records, streams, closes, record_count, stream_count = {}, {}, {}, 0, 0
end
proto.init=reset_state
reset_state()
local function generated(tree, field, value)
    return tree:add(field, value):set_generated()
end
local function stream_id()
    local field = tcp_stream()
    return field and tostring(field) or nil
end
local function endpoints(pinfo)
    return tostring(pinfo.src)..":"..tostring(pinfo.src_port), tostring(pinfo.dst)..":"..tostring(pinfo.dst_port)
end
local function connection(id, create)
    if not id then return nil end
    if not streams[id] and create and stream_count < MAX_STREAMS then
        streams[id] = {directions={}, last=nil}
        stream_count = stream_count + 1
    end
    return streams[id]
end
local function direction(conn, src)
    if not conn.directions[src] then conn.directions[src] = {queue={}, head=1, tail=0} end
    return conn.directions[src]
end
local function region(a)
    if a >= 0xf000 then return "Control register", 2 end
    if a <= 0x1fff then return "DM0 (bench low 8K words)", 4 end
    if a >= 0x6000 and a <= 0x7fff then return "DM1 (bench low 8K words)", 4 end
    if a >= 0xc000 and a <= 0xdfff then return "PM (bench low 8K, 32-bit transfer)", 4 end
    return "Outside current bench windows", nil
end
local function target(a)
    if registers[a] then return registers[a][1]..string.format("[0x%04X]", a) end
    return string.format("0x%04X", a)
end
local function value_description(a, v)
    if a == 0xf405 then return states[v % 8] or "Reserved core state" end
    if a == 0xf004 then return v % 2 == 1 and "PLL locked" or "PLL not locked" end
    if a == 0xf890 then return v % 2 == 1 and "Exit soft reset" or "Enter soft reset" end
    return nil
end
local function add_links(item, rec)
    if not rec then return end
    if rec.request then
        generated(item, f.request, rec.request.frame)
        local elapsed=rec.time - rec.request.time
        local secs=math.floor(elapsed)
        generated(item, f.elapsed, NSTime.new(secs, math.floor((elapsed-secs)*1e9+0.5)))
        generated(item, f.matched, rec.match)
        if not rec.match then
            item:add_proto_expert_info(ex.mismatch, string.format(
                "FIFO request #%d: chip=%02X address=%04X bytes=%d; reply chip=%02X address=%04X bytes=%d. No transaction ID; association is only an ordering hypothesis.",
                rec.request.frame, rec.request.chip, rec.request.addr, rec.request.count, rec.chip, rec.addr, rec.count))
        end
    elseif rec.orphan then
        item:add_proto_expert_info(ex.orphan, "Capture may start mid-session; response has no earlier observed request in this TCP stream/direction")
    end
    if rec.response then generated(item, f.response, rec.response.frame) end
    if rec.limit then item:add_proto_expert_info(ex.limit, "Correlation budget reached; decoding continues without guaranteed links") end
end
local function correlate(pinfo, offset, cmd, chip, a, count)
    local id = stream_id()
    local src, dst = endpoints(pinfo)
    local key = (id or "?")..":"..src..":"..pinfo.number..":"..offset
    local rec = records[key]
    if rec or pinfo.visited then return rec end
    if record_count >= MAX_RECORDS then return {limit=true} end
    local conn = connection(id, true)
    rec = {frame=pinfo.number, time=pinfo.abs_ts, chip=chip, addr=a, count=count}
    records[key] = rec; record_count = record_count + 1
    if not conn then rec.limit=true; return rec end
    if cmd ~= 0x0b then
        conn.last = rec.frame;conn.requester=src;conn.responder=dst
    end
    if cmd == 0x0a then
        local d = direction(conn, src)
        if not d.unreliable and d.tail-d.head+1 < MAX_PENDING then
            d.tail=d.tail+1; d.queue[d.tail]=rec
        else rec.limit=true;d.unreliable=true end
    elseif cmd == 0x0b then
        local d = direction(conn, dst)
        local request = d.queue[d.head]
        if d.unreliable then rec.limit=true
        elseif request then
            d.queue[d.head]=nil; d.head=d.head+1
            if d.head>d.tail then d.head=1;d.tail=0 end
            rec.request=request;request.response=rec
            rec.match=request.chip==chip and request.addr==a and request.count==count
        else rec.orphan=true end
    end
    return rec
end
local function add_payload(item, tvb, offset, count, a, width, summary)
    item:add(f.data, tvb(offset, count))
    if not width then return summary end
    local max_words = math.min(256, tonumber(proto.prefs.words))
    local words = math.min(math.floor(count / width), max_words)
    for i=0,words-1 do
        local address=a+i
        if address>0xffff then break end
        local bytes=tvb(offset+i*width,width)
        local value=bytes:uint()
        local label=target(address)..string.format(" = %0"..(width*2).."X",value)
        local desc=width==2 and value_description(address,value) or nil
        if desc then label=label.." ("..desc..")" end
        local word=item:add(bytes,label)
        generated(word,f.word_addr,address)
        if registers[address] then generated(word,f.word_register,registers[address][1]) end
        word:add(width==2 and f.value16 or f.value32,bytes)
        if width==2 then
            if address==0xf405 then word:add(f.core,bytes)
            elseif address==0xf004 then word:add(f.pll,bytes)
            elseif address==0xf890 then word:add(f.reset,bytes) end
        end
    end
    if math.floor(count/width)>words then
        item:append_text(string.format("; showing first %d/%d words, full bytes in Payload",words,math.floor(count/width)))
    end
    -- The Info preview is independent of the tree word expansion preference.
    if count>=width then
        local v=tvb(offset,width):uint()
        summary=summary..string.format(" = %0"..(width*2).."X",v)
        local desc=width==2 and value_description(a,v) or nil
        if desc then summary=summary.." ("..desc..")" end
        if count>width then summary=summary.." ..." end
    end
    return summary
end
local function annotate_policy(item, cmd, chip, channel, flag, a, count, width)
    if not proto.prefs.policy then return end
    if chip~=0 and chip~=0x70 then item:add_proto_expert_info(ex.policy,"Current bench binds only wire 00/70 to I2C 7-bit38; no EEPROM routing") end
    if cmd==9 and channel~=0 then item:add_proto_expert_info(ex.policy,"Current bench only accepts channel0") end
    if flag and flag>1 then item:add_proto_expert_info(ex.policy,"Current bench only accepts write flag0/1") end
    if not width then item:add_proto_expert_info(ex.policy,"Starting address is outside current bench memory windows") end
    if width then
        if count%width~=0 or a+math.ceil(count/width)>0x10000 then
            item:add_proto_expert_info(ex.policy,"Word width / address overflow violates current bench transfer policy")
        end
        -- Check the whole byte interval, not only its first register/word.
        local words=math.ceil(count/width)
        for i=0,words-1 do
            local address=a+i
            local _, w=region(address)
            local reg=registers[address]
            if address>0xffff or w~=width or (address>=0xf000 and (not reg or (cmd==9 and reg[2]~="RW"))) then
                item:add_proto_expert_info(ex.policy,string.format("Includes reserved, read-only-on-write or out-of-window address 0x%04X",address));break
            end
        end
    end
    if flag==1 then
        item:add_proto_expert_info(ex.note,"Flag1 is the native Safeload method; bridge maps it only to this project's 0014..0018 software Safeload. Clock/core checks are not inferred from this packet.")
        if a<0x14 or a>0x18 or count%4~=0 or count>20 or a+count/4>0x19 then
            item:add_proto_expert_info(ex.policy,"Safeload target/count is outside this minimal project's allowed parameters")
        end
    end
end
local function mark_invalid(conn, src, pinfo)
    if conn and not pinfo.visited then
        local d=direction(conn,src)
        if not d.invalid or pinfo.number<d.invalid then d.invalid=pinfo.number end
    end
end
local observed_init="1b00400600010101000001"
function proto.dissector(tvb, pinfo, tree)
    if tvb:len()==0 then return 0 end
    pinfo.cols.protocol="SIGMADSP"
    local offset, summaries=0,{}
    local src=endpoints(pinfo)
    local conn=connection(stream_id(),not pinfo.visited)
    local d=conn and direction(conn,src) or nil
    if d and d.invalid and pinfo.number>d.invalid then
        local item=tree:add(proto,tvb(),"SigmaStudio+: undecoded after invalid stream boundary")
        item:add_proto_expert_info(ex.unknown,"Earlier malformed/unknown PDU; no scan for guessed command boundaries. Decode a fresh TCP connection or a capture beginning at a known boundary.")
        pinfo.cols.info="Undecoded after invalid SigmaStudio+ frame"
        return tvb:len()
    end
    local function incomplete(needed)
        if d and not pinfo.visited then d.partial=needed end
        if tvb:reported_len()>tvb:len() or not pinfo.can_desegment or pinfo.can_desegment==0 then
            local item=tree:add(proto,tvb(offset),"SigmaStudio+: incomplete PDU")
            item:add_proto_expert_info(ex.incomplete,"Need "..needed.." additional bytes; capture truncated or TCP reassembly unavailable")
            mark_invalid(conn,src,pinfo)
        else
            pinfo.desegment_offset=offset
            pinfo.desegment_len=needed
        end
        summaries[#summaries+1]="Incomplete PDU (need "..needed.." bytes)"
        pinfo.cols.info=table.concat(summaries," | ")
    end
    while offset<tvb:len() do
        local available=tvb:len()-offset
        local cmd=tvb(offset,1):uint()
        if not commands[cmd] then
            local item=tree:add(proto,tvb(offset),"SigmaStudio+: unknown command")
            item:add(f.cmd,tvb(offset,1))
            item:add_proto_expert_info(ex.unknown,string.format("Unknown opcode 0x%02X; remaining bytes not resynchronized",cmd))
            summaries[#summaries+1]=string.format("UNKNOWN 0x%02X",cmd)
            mark_invalid(conn,src,pinfo);break
        end
        local min=cmd==0x1b and 11 or 14
        if available<min then incomplete(min-available);return tvb:len() end
        if cmd==0x1b then
            local item=tree:add(proto,tvb(offset,11),"SigmaStudio+: observed INIT (undocumented semantics)")
            item:add(f.cmd,tvb(offset,1))
            local exact=tvb(offset,11):bytes():tohex(false,""):lower()==observed_init
            generated(item,f.init,exact)
            if not exact then
                item:add_proto_expert_info(ex.unknown,"Not the exact observed 11-byte INIT; its length/fields cannot be assumed")
                summaries[#summaries+1]="Unknown INIT variation";mark_invalid(conn,src,pinfo);break
            end
            item:add_proto_expert_info(ex.note,"Exact 3.4.0 captured sequence; current bench consumes it as a no-op without ACK. Fields are not reverse-guessed.")
            if d and not pinfo.visited then d.partial=nil end
            correlate(pinfo,offset,cmd,0,0,0)
            summaries[#summaries+1]="Observed INIT (exact capture, no ACK)"
            offset=offset+11
        else
            local write=cmd==9
            local total=tvb(offset+(write and 3 or 1),4):uint()
            local count=tvb(offset+(write and 8 or 6),4):uint()
            local expected=cmd==0x0a and 14 or 14+count
            if total~=expected or count>65536 or (count==0 and cmd~=0x0b) then
                local item=tree:add(proto,tvb(offset,math.min(available,14)),"SigmaStudio+: invalid length header")
                item:add(f.cmd,tvb(offset,1));item:add(f.total,tvb(offset+(write and 3 or 1),4))
                item:add(f.length,tvb(offset+(write and 8 or 6),4))
                item:add_proto_expert_info(ex.malformed,"Length must fit the bounded 14-byte-header R/W envelope (payload <=65536); read requests have NO payload")
                summaries[#summaries+1]="INVALID LENGTH";mark_invalid(conn,src,pinfo);break
            end
            if available<total then incomplete(total-available);return tvb:len() end
            local chip=tvb(offset+(write and 7 or 5),1):uint()
            local a=tvb(offset+(write and 12 or 10),2):uint()
            local flag=write and tvb(offset+1,1):uint() or nil
            local channel=write and tvb(offset+2,1):uint() or nil
            local name,width=region(a)
            local payload_width=width
            if d and not pinfo.visited then d.partial=nil end
            local summary=string.format("%s%s chip=%02X %s %d bytes",commands[cmd],flag==1 and "/SAFELOAD" or "",chip,target(a),count)
            local item=tree:add(proto,tvb(offset,total),"SigmaStudio+: "..summary)
            item:add(f.cmd,tvb(offset,1));item:add(f.total,tvb(offset+(write and 3 or 1),4))
            item:add(f.chip,tvb(offset+(write and 7 or 5),1));item:add(f.length,tvb(offset+(write and 8 or 6),4))
            item:add(f.addr,tvb(offset+(write and 12 or 10),2))
            generated(item,f.region,name)
            if registers[a] then generated(item,f.register,registers[a][1]) end
            if write then
                item:add(f.flag,tvb(offset+1,1));item:add(f.channel,tvb(offset+2,1))
            elseif cmd==0x0a then
                item:add(f.reserved,tvb(offset+12,2))
                if tvb(offset+12,2):uint()~=0 then item:add_proto_expert_info(ex.malformed,"Nonzero reserved read-request bytes; current bench rejects this") end
            else
                local status=tvb(offset+12,1):uint()
                item:add(f.status,tvb(offset+12,1));item:add(f.reserved,tvb(offset+13,1))
                if status~=0 then
                    payload_width=nil -- Failed response bytes are not valid register contents.
                    summary=summary.." STATUS="..status
                    item:add_proto_expert_info(ex.failure,"Failure/unknown response status; failure lengths/driver acceptance have not been native-verified")
                elseif count==0 then item:add_proto_expert_info(ex.malformed,"Zero-byte successful response is not part of the verified R/W subset") end
                if tvb(offset+13,1):uint()~=0 then item:add_proto_expert_info(ex.malformed,"Nonzero reserved response byte") end
            end
            if count>0 and cmd~=0x0a then summary=add_payload(item,tvb,offset+14,count,a,payload_width,summary) end
            annotate_policy(item,cmd,chip,channel,flag,a,count,width)
            add_links(item,correlate(pinfo,offset,cmd,chip,a,count))
            item:set_text("SigmaStudio+: "..summary)
            summaries[#summaries+1]=summary
            offset=offset+total
        end
    end
    pinfo.cols.info=table.concat(summaries," | ")
    return tvb:len()
end

-- TCP port dissectors are not normally called on empty ACK/FIN/RST packets.
-- A passive postdissector adds context, never interprets an RST as an I2C error.
local close_proto=Proto("sigmadsp_close","SigmaStudio+ TCP transport context")
local function flag_set(field)
    return field and (field.value==true or tonumber(field.value)==1)
end
function close_proto.dissector(tvb,pinfo,tree)
    local rst,fin=tcp_rst(),tcp_fin()
    local is_rst=flag_set(rst)
    local is_fin=flag_set(fin)
    local id=stream_id();local conn=connection(id,false)
    if not conn then return end
    if not is_rst and not is_fin then
        local length=tcp_len()
        local src=endpoints(pinfo)
        if not length or tonumber(length.value)~=0 or not flag_set(tcp_ack()) or flag_set(tcp_syn()) or src~=conn.responder then return end
        local item=tree:add(proto,tvb(),"SigmaStudio+ bridge TCP ACK only (0 application bytes)")
        generated(item,f.ack,true)
        -- Keep Protocol=TCP: this is transport receipt, NOT a SigmaStudio reply
        -- or proof that the most recent DSP operation completed successfully.
        pinfo.cols.info:append(" [Bridge TCP ACK only; no application response]")
        return
    end
    local key=id..":"..pinfo.number
    local rec=closes[key]
    if not rec and not pinfo.visited and record_count<MAX_RECORDS then
        local pending,partial,limited=0,0,false
        for _,dir in pairs(conn.directions) do
            pending=pending+dir.tail-dir.head+1;partial=partial+(dir.partial or 0)
            limited=limited or dir.unreliable
        end
        rec={kind=is_rst and "RST" or "FIN",last=conn.last,pending=pending,partial=partial,limited=limited}
        closes[key]=rec;record_count=record_count+1
    end
    if not rec then return end
    local item=tree:add(proto,tvb(),"SigmaStudio+ connection "..rec.kind.." (TCP event, not a DSP diagnosis)")
    generated(item,f.close,rec.kind);generated(item,f.pending,rec.pending)
    if rec.last then generated(item,f.last,rec.last) end
    if rec.partial>0 then
        generated(item,f.partial,rec.partial)
        item:add_proto_expert_info(ex.incomplete,"TCP closed with an incomplete observed PDU; capture loss/truncation is also possible")
    end
    if rec.limited then item:add_proto_expert_info(ex.limit,"Pending-read queue overflowed; matching disabled for that request direction until a fresh TCP stream") end
    item:add_proto_expert_info(ex.note,"TCP "..rec.kind.." does not encode a bridge operation_error; inspect serial logs. Ordinary writes require no application ACK. Current bridge resets DSP on disconnect.")
end
register_postdissector(close_proto,true)
local tcp_ports=DissectorTable.get("tcp.port")
local current_port=tonumber(proto.prefs.port)
if current_port>0 and current_port<=65535 then tcp_ports:add(current_port,proto) end
tcp_ports:add_for_decode_as(proto)
function proto.prefs_changed()
    if current_port>0 and current_port<=65535 then tcp_ports:remove(current_port,proto) end
    current_port=tonumber(proto.prefs.port)
    if current_port>0 and current_port<=65535 then tcp_ports:add(current_port,proto) end
    reset_state()
end
