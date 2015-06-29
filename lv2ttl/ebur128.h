// generated by lv2ttl2c from
// http://gareus.org/oss/lv2/meters#EBUr128

extern const LV2_Descriptor* lv2_descriptor(uint32_t index);
extern const LV2UI_Descriptor* lv2ui_ebur(uint32_t index);

static const RtkLv2Description _plugin_ebur = {
	&lv2_descriptor,
	&lv2ui_ebur
	, 11 // uint32_t dsp_descriptor_id
	, 0 // uint32_t gui_descriptor_id
	, "EBU R128 Meter" // const char *plugin_human_id
	, (const struct LV2Port[6])
	{
		{ "control", ATOM_IN, nan, nan, nan, "UI to plugin communication"},
		{ "notify", ATOM_OUT, nan, nan, nan, "plugin to UI communication"},
		{ "inL", AUDIO_IN, nan, nan, nan, "InL"},
		{ "outL", AUDIO_OUT, nan, nan, nan, "OutL"},
		{ "inR", AUDIO_IN, nan, nan, nan, "InR"},
		{ "outR", AUDIO_OUT, nan, nan, nan, "OutR"},
	}
	, 6 // uint32_t nports_total
	, 2 // uint32_t nports_audio_in
	, 2 // uint32_t nports_audio_out
	, 0 // uint32_t nports_midi_in
	, 0 // uint32_t nports_midi_out
	, 1 // uint32_t nports_atom_in
	, 1 // uint32_t nports_atom_out
	, 0 // uint32_t nports_ctrl
	, 0 // uint32_t nports_ctrl_in
	, 0 // uint32_t nports_ctrl_out
	, 8192 // uint32_t min_atom_bufsiz
	, true // bool send_time_info
};

#ifdef X42_PLUGIN_STRUCT
#undef X42_PLUGIN_STRUCT
#endif
#define X42_PLUGIN_STRUCT _plugin_ebur
