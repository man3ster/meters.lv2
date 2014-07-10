/* signal distribution meter LV2 GUI
 *
 * Copyright 2014 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>


#ifndef MAX
#define MAX(A,B) ( (A) > (B) ? (A) : (B) )
#endif

#define LVGL_RESIZEABLE

#define BORDER_RIGHT (55)
#define BORDER_BOTTOM (16)

#define MTR_URI "http://gareus.org/oss/lv2/meters#"
#define MTR_GUI "sdhmeterui"

#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "src/uris.h"

/*************************/
enum {
	FONT_M08 = 0,
	FONT_S08,
	FONT_LAST,
};

typedef struct {
	LV2_Atom_Forge forge;

	LV2_URID_Map* map;
	EBULV2URIs   uris;

	LV2UI_Write_Function write;
	LV2UI_Controller     controller;

	RobWidget* box;

	RobTkCBtn* btn_start;
	RobTkPBtn* btn_reset;

	RobTkCBtn* cbx_transport;
	RobTkCBtn* cbx_autoreset;

	RobWidget* m0;
	RobWidget* hbox;
	RobTkSep*  sep;

	bool redraw_labels;
	bool fontcache;
	PangoFontDescription *font[2];

	bool disable_signals;
	uint32_t width;
	uint32_t height;

	/* current data */
	uint64_t integration_spl;
	int histS[HIST_LEN];
	int hist_max;
	int hist_peakbin;
	double hist_avg;
	double hist_var;

	float rate;
} SDHui;


/******************************************************************************
 * custom visuals
 */

#define LUFS(V) ((V) < -100 ? -INFINITY : (lufs ? (V) : (V) + 23.0))
#define FONT(A) ui->font[(A)]

static void write_text(
		cairo_t* cr,
		const char *txt,
		PangoFontDescription *font, //const char *font,
		const float x, const float y,
		const float ang, const int align,
		const float * const col) {
	write_text_full(cr, txt, font, x, y, ang, align, col);
}

static void initialize_font_cache(SDHui* ui) {
	ui->fontcache = true;
	ui->font[FONT_M08] = pango_font_description_from_string("Mono 8");
	ui->font[FONT_S08] = pango_font_description_from_string("Sans 8");
	assert(ui->font[FONT_M08]);
	assert(ui->font[FONT_S08]);
}

/******************************************************************************
 * Main drawing function
 */

static void format_num(char *buf, const int num) {
	if (num >= 1000000000) {
		sprintf(buf, "%dM", num / 1000000);
	} else if (num >= 10000000) {
		sprintf(buf, "%.2fM", num / 1000000.f);
	} else if (num >= 10000) {
		sprintf(buf, "%dK", num / 1000);
	} else {
		sprintf(buf, "%d", num);
	}
}

static void format_duration(char *buf, const float sec) {
	if (sec < 60) {
		sprintf(buf, "%.1f\"", sec);
	} else if (sec < 600) {
		int minutes = sec / 60;
		int seconds = ((int)floorf(sec)) % 60;
		int ds = 10*(sec - seconds - 60*minutes);
		sprintf(buf, "%d'%02d\"%d", minutes, seconds, ds);
	} else if (sec < 3600) {
		int minutes = sec / 60;
		int seconds = ((int)floorf(sec)) % 60;
		sprintf(buf, "%d'%02d\"", minutes, seconds);
	} else {
		int hours = sec / 3600;
		int minutes = ((int)floorf(sec / 60)) % 60;
		sprintf(buf, "%dh%02d'", hours, minutes);
	}
}

static bool expose_event(RobWidget* handle, cairo_t* cr, cairo_rectangle_t *ev) {
	SDHui* ui = (SDHui*)GET_HANDLE(handle);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip (cr);

	const bool active = ui->integration_spl > 1 && ui->hist_max > 0;

	const float da_width  = ui->width  - BORDER_RIGHT;
	const float da_height = ui->height - BORDER_BOTTOM;

	cairo_rectangle (cr, 0, 0, ui->width, ui->height);
	cairo_clip (cr);

	CairoSetSouerceRGBA(c_g10);
	cairo_rectangle (cr, da_width, 0, BORDER_RIGHT, ui->height);
	cairo_fill (cr);
	cairo_rectangle (cr, 0, da_height, da_width, BORDER_BOTTOM);
	cairo_fill (cr);

	cairo_rectangle (cr, 0, 0, da_width, da_height);
	if (active) {
		CairoSetSouerceRGBA(c_g30);
	} else {
		CairoSetSouerceRGBA(c_g60);
	}
	cairo_fill (cr);

	cairo_set_line_width (cr, 1.0);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
	CairoSetSouerceRGBA(c_g80);

	const float xctr = rintf (da_width * DIST_ZERO / DIST_SIZE) - .5;
	const double dsh2 = 2;

	if (active) {
		// center line
		cairo_set_dash(cr, &dsh2, 1, 0);
		cairo_move_to (cr, xctr, 0);
		cairo_line_to (cr, xctr, da_height);
		cairo_stroke(cr);

		// peak line
		const double dsh[2] = {1, 3};
		cairo_set_dash(cr, dsh, 2, 0);

		cairo_move_to (cr, 0, 9.5);
		cairo_line_to (cr, da_width, 9.5);
		cairo_stroke(cr);

		cairo_set_dash(cr, NULL, 0, 0);
	}

	/* Y tick - marks */
	cairo_move_to (cr, da_width, 9.5);
	cairo_line_to (cr, da_width + 6, 9.5);
	cairo_stroke(cr);

	cairo_move_to (cr, da_width, da_height + .5);
	cairo_line_to (cr, da_width + 6, da_height + .5);
	cairo_stroke(cr);

	// TODO 3rd (or more) Y-axis label
	// TODO Y log-scale

	/* X tick - marks */
	for (int i = -10; i <= 10; ++i) {
		const float xtick = rintf((DIST_ZERO + DIST_RANGE * i / 10.f) * da_width / DIST_SIZE ) - .5;
		const float ticlen = ((i + 10) % 5 == 0 ) ? 4.5 : 2.5;
		cairo_move_to (cr, xtick, da_height);
		cairo_line_to (cr, xtick, da_height + ticlen);
		cairo_stroke(cr);
	}

	/* on graph -1, +1 lines */
	cairo_set_dash(cr, &dsh2, 1, 0);

	const float x0 = rintf(da_width * (DIST_ZERO - DIST_RANGE) / DIST_SIZE) - .5;
	cairo_move_to (cr, x0, 0);
	cairo_line_to (cr, x0, da_height);
	cairo_stroke(cr);
	write_text(cr, "-1.0 ", FONT(FONT_M08), x0, ui->height, 0, 5, c_wht);

	const float x1 = rintf(da_width * (DIST_ZERO + DIST_RANGE) / DIST_SIZE) - .5;
	cairo_move_to (cr, x1, 0);
	cairo_line_to (cr, x1, da_height);
	cairo_stroke(cr);
	write_text(cr, "1.0", FONT(FONT_M08), x1, ui->height, 0, 5, c_wht);

	cairo_set_dash(cr, NULL, 0, 0);

	/* unit labels */
	write_text(cr, "[sample]", FONT(FONT_S08), xctr, ui->height, 0, 5, c_g80);
	write_text(cr, "[multiplicity]", FONT(FONT_S08), da_width + 2, da_height * .5, M_PI / -2.f, 8, c_g80);

	if (active) {
		const float lw = MAX (2.0, da_width / DIST_SIZE);
		const float mlt_y = (da_height - lw - 10) / (float)ui->hist_max;
		const float mlt_x = da_width / DIST_SIZE;

		const double avg = ui->hist_avg / (double) ui->integration_spl;
		const double stddev = sqrt(ui->hist_var / ((double)(ui->integration_spl - 1.0)));

		const float avg_x = ((DIST_ZERO + DIST_RANGE * avg) * mlt_x) - .5;
		const float dev_x = ((DIST_RANGE * stddev) * mlt_x);

		if (dev_x > 1) {
			cairo_set_source_rgba (cr, .0, .0, .9, .5);
			cairo_rectangle (cr, avg_x + .5 - dev_x, 0,
					dev_x + dev_x, da_height);
			cairo_fill(cr);
		}

		if (ui->hist_peakbin >= 0) {
			const float peakbinx = rintf(ui->hist_peakbin * mlt_x) - .5;
			CairoSetSouerceRGBA(c_ora);
			cairo_set_line_width(cr, 1.5);
			cairo_move_to (cr, peakbinx, 0);
			cairo_line_to (cr, peakbinx, 10);
			cairo_line_to (cr, peakbinx - 2, 6);
			cairo_line_to (cr, peakbinx, 7);
			cairo_line_to (cr, peakbinx + 2, 6);
			cairo_line_to (cr, peakbinx, 10);
			cairo_stroke(cr);
		}

		CairoSetSouerceRGBA(c_nyl);
		cairo_set_line_width(cr, 1.0);
		cairo_move_to (cr, avg_x, 0);
		cairo_line_to (cr, avg_x, da_height);
		cairo_stroke(cr);

		// Plot Data
		CairoSetSouerceRGBA(c_red);
		cairo_set_line_width(cr, lw);

		const float yoff = da_height;
#if 0 // points
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		for (int i=0; i < DIST_BIN; ++i) {
			cairo_move_to (cr,
					i * mlt_x - .5,
					yoff - ui->histS[i] * mlt_y);
			cairo_close_path(cr);
			cairo_stroke(cr);
		}
		cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
#else // lines
		cairo_move_to (cr, 0, da_height - ui->histS[0] * mlt_y);
		for (int i=1; i < DIST_BIN; ++i) {
			cairo_line_to (cr,
					i * mlt_x - .5,
					yoff - ui->histS[i] * mlt_y);
		}
		cairo_stroke(cr);
#endif

		// Numeric Readout
		char buf[256];
		format_num(buf, ui->hist_max);
		write_text(cr, buf, FONT(FONT_M08), da_width + 8, 10, 0, 3, c_wht);
		write_text(cr, "0", FONT(FONT_M08), da_width + 8, da_height, 0, 3, c_wht);

		/* clock */
		float txty = 12 * 3;

		write_text(cr, "Peak:", FONT(FONT_S08), da_width + 5, txty, 0, 9, c_ora); txty += 12;
		sprintf(buf, "%.3f", (ui->hist_peakbin - DIST_ZERO) / DIST_RANGE);
		write_text(cr, buf, FONT(FONT_M08), ui->width - 3, txty, 0, 7, c_wht); txty += 12;

		write_text(cr, "Avg:", FONT(FONT_S08), da_width + 5, txty, 0, 9, c_nyl); txty += 12;
		sprintf(buf, "%.3f", avg);
		write_text(cr, buf, FONT(FONT_M08), ui->width - 3, txty, 0, 7, c_wht); txty += 12;

		static const float c_blu[4] = {0.2, 0.2, 1.0, 1.0};
		write_text(cr, "StdDev:", FONT(FONT_S08), da_width + 5, txty, 0, 9, c_blu); txty += 12;
		sprintf(buf, "%.3f", stddev);
		write_text(cr, buf, FONT(FONT_M08), ui->width - 3, txty, 0, 7, c_wht); txty += 12;

		txty = da_height - 12 * 7;

		write_text(cr, "Time:", FONT(FONT_S08), da_width + 5, txty, 0, 9, c_wht); txty += 12;
		format_duration(buf, ui->integration_spl / ui->rate);
		write_text(cr, buf, FONT(FONT_M08), ui->width - 3, txty, 0, 7, c_wht); txty += 12;

		write_text(cr, "Samples:", FONT(FONT_S08), da_width + 5, txty, 0, 9, c_wht); txty += 12;
		format_num(buf, ui->integration_spl);
		write_text(cr, buf, FONT(FONT_M08), ui->width - 3, txty, 0, 7, c_wht);

		if (ui->integration_spl >= 2147483647) {
			// show EOC 2^31
			write_text(cr, "The histogram buffer is full.\nData acquisition suspended.", FONT(FONT_S08), xctr, rintf(ui->height * .5f), 0, 2, c_wht);
		}

	} else {
		write_text(cr, "No histogram\ndata available.", FONT(FONT_S08), xctr, rintf(ui->height * .5f), 0, 2, c_blk);
	}

	/* border */
	cairo_set_line_width (cr, 1.0);
	CairoSetSouerceRGBA(c_g80);
	cairo_move_to (cr, 0, da_height + .5);
	cairo_line_to (cr, da_width + .5, da_height + .5);
	cairo_stroke(cr);

	cairo_move_to (cr, da_width + .5, 0);
	cairo_line_to (cr, da_width + .5, da_height + .5);
	cairo_stroke(cr);

	return TRUE;
}

/******************************************************************************
 * LV2 UI -> plugin communication
 */

static void forge_message_kv(SDHui* ui, LV2_URID uri, int key, float value) {
	uint8_t obj_buf[1024];
	if (ui->disable_signals) return;

	lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 1024);
	LV2_Atom* msg = forge_kvcontrolmessage(&ui->forge, &ui->uris, uri, key, value);
	ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

static void invalidate_changed(SDHui* ui, int what) {
	// TODO partial exposure
	queue_draw(ui->m0);
}

/******************************************************************************
 * UI callbacks
 */

static bool btn_start(RobWidget *w, void* handle) {
	SDHui* ui = (SDHui*)handle;
	if (robtk_cbtn_get_active(ui->btn_start)) {
		forge_message_kv(ui, ui->uris.mtr_meters_cfg, CTL_START, 0);
	} else {
		forge_message_kv(ui, ui->uris.mtr_meters_cfg, CTL_PAUSE, 0);
	}
	invalidate_changed(ui, -1);
	return TRUE;
}

static bool btn_reset(RobWidget *w, void* handle) {
	SDHui* ui = (SDHui*)handle;
	forge_message_kv(ui, ui->uris.mtr_meters_cfg, CTL_RESET, 0);
	invalidate_changed(ui, -1);
	return TRUE;
}

static void btn_start_sens(SDHui* ui) {
	if (robtk_cbtn_get_active(ui->cbx_transport)
			|| ui->integration_spl >= 2147483647) {
		// NB *_set_sensitive is a NOOP if state remains unchanged.
		robtk_cbtn_set_sensitive(ui->btn_start, false);
	} else {
		robtk_cbtn_set_sensitive(ui->btn_start, true);
	}
}

static bool cbx_transport(RobWidget *w, void* handle) {
	SDHui* ui = (SDHui*)handle;
	btn_start_sens(ui);
	if (robtk_cbtn_get_active(ui->cbx_transport)) {
		forge_message_kv(ui, ui->uris.mtr_meters_cfg, CTL_TRANSPORTSYNC, 1);
	} else {
		forge_message_kv(ui, ui->uris.mtr_meters_cfg, CTL_TRANSPORTSYNC, 0);
	}
	return TRUE;
}

static bool cbx_autoreset(RobWidget *w, void* handle) {
	SDHui* ui = (SDHui*)handle;
	if (robtk_cbtn_get_active(ui->cbx_autoreset)) {
		forge_message_kv(ui, ui->uris.mtr_meters_cfg, CTL_AUTORESET, 1);
	} else {
		forge_message_kv(ui, ui->uris.mtr_meters_cfg, CTL_AUTORESET, 0);
	}
	return TRUE;
}

/******************************************************************************
 * widget hackery
 */

static void
size_request(RobWidget* handle, int *w, int *h) {
	*w = BORDER_RIGHT + DIST_SIZE;
	*h = BORDER_BOTTOM + DIST_SIZE ;
}

static void
m0_size_allocate(RobWidget* rw, int w, int h) {
	SDHui* ui = (SDHui*)GET_HANDLE(rw);
	ui->width = w;
	ui->height = h;
	robwidget_set_size(rw, w, h);
	queue_draw(rw);
}

/******************************************************************************
 * LV2 callbacks
 */

static void ui_enable(LV2UI_Handle handle) {
	SDHui* ui = (SDHui*)handle;
	forge_message_kv(ui, ui->uris.mtr_meters_on, 0, 0); // may be too early
}

static void ui_disable(LV2UI_Handle handle) {
	SDHui* ui = (SDHui*)handle;
	forge_message_kv(ui, ui->uris.mtr_meters_off, 0, 0);
}

static LV2UI_Handle
instantiate(
		void* const               ui_toplevel,
		const LV2UI_Descriptor*   descriptor,
		const char*               plugin_uri,
		const char*               bundle_path,
		LV2UI_Write_Function      write_function,
		LV2UI_Controller          controller,
		RobWidget**               widget,
		const LV2_Feature* const* features)
{
	SDHui* ui = (SDHui*)calloc(1,sizeof(SDHui));
	ui->write      = write_function;
	ui->controller = controller;

	*widget = NULL;

	for (int i = 0; features[i]; ++i) {
		if (!strcmp(features[i]->URI, LV2_URID_URI "#map")) {
			ui->map = (LV2_URID_Map*)features[i]->data;
		}
	}

	if (!ui->map) {
		fprintf(stderr, "UI: Host does not support urid:map\n");
		free(ui);
		return NULL;
	}

	ui->rate = 48000;
	ui->width  = 400;
	ui->height = 400;
	ui->hist_peakbin = -1;
	ui->integration_spl = 0;

	map_eburlv2_uris(ui->map, &ui->uris);

	lv2_atom_forge_init(&ui->forge, ui->map);

	ui->box = rob_vbox_new(FALSE, 2);
	robwidget_make_toplevel(ui->box, ui_toplevel);
	ROBWIDGET_SETNAME(ui->box, "sigdist");

	ui->hbox = rob_hbox_new(TRUE, 0);
	ui->sep  = robtk_sep_new(true);

	/* main drawing area */
	ui->m0 = robwidget_new(ui);
	ROBWIDGET_SETNAME(ui->m0, "sigco (m0)");
	robwidget_set_alignment(ui->m0, .5, .5);
	robwidget_set_expose_event(ui->m0, expose_event);
	robwidget_set_size_request(ui->m0, size_request);
	robwidget_set_size_allocate(ui->m0, m0_size_allocate);
	rob_vbox_child_pack(ui->box, ui->m0, TRUE, TRUE);

	/* control buttons */
	ui->btn_start = robtk_cbtn_new("Collect Data", GBT_LED_OFF, false);
	ui->btn_reset = robtk_pbtn_new("Reset");

	ui->cbx_transport  = robtk_cbtn_new("Host Transport", GBT_LED_LEFT, true);
	ui->cbx_autoreset  = robtk_cbtn_new("Reset on Start", GBT_LED_LEFT, true);

	robtk_pbtn_set_alignment(ui->btn_reset, 0.5, 0.5);
	robtk_cbtn_set_alignment(ui->btn_start, 0.5, 0.5);

	/* button packing */
	rob_hbox_child_pack(ui->hbox, robtk_cbtn_widget(ui->btn_start), FALSE, FALSE);
	rob_hbox_child_pack(ui->hbox, robtk_pbtn_widget(ui->btn_reset), FALSE, FALSE);
	rob_hbox_child_pack(ui->hbox, robtk_cbtn_widget(ui->cbx_transport), FALSE, FALSE);
	rob_hbox_child_pack(ui->hbox, robtk_cbtn_widget(ui->cbx_autoreset), FALSE, FALSE);

	/* global packing */
	rob_vbox_child_pack(ui->box, robtk_sep_widget(ui->sep), FALSE, TRUE);
	rob_vbox_child_pack(ui->box, ui->hbox, FALSE, TRUE);

	/* signals */
	robtk_cbtn_set_callback(ui->btn_start, btn_start, ui);
	robtk_pbtn_set_callback(ui->btn_reset, btn_reset, ui);
	robtk_cbtn_set_callback(ui->cbx_transport, cbx_transport, ui);
	robtk_cbtn_set_callback(ui->cbx_autoreset, cbx_autoreset, ui);

	*widget = ui->box;

	initialize_font_cache(ui);
	ui->redraw_labels = TRUE;

	ui_enable(ui);
	return ui;
}

static enum LVGLResize
plugin_scale_mode(LV2UI_Handle handle)
{
	return LVGL_LAYOUT_TO_FIT;
}

static void
cleanup(LV2UI_Handle handle)
{
	SDHui* ui = (SDHui*)handle;
	ui_disable(handle);

	if (ui->fontcache) {
		for (int i=0; i < FONT_LAST; ++i) {
			pango_font_description_free(ui->font[i]);
		}
	}

	robtk_cbtn_destroy(ui->cbx_transport);
	robtk_cbtn_destroy(ui->cbx_autoreset);
	robtk_cbtn_destroy(ui->btn_start);
	robtk_pbtn_destroy(ui->btn_reset);

	robtk_sep_destroy(ui->sep);
	robwidget_destroy(ui->m0);
	rob_box_destroy(ui->hbox);
	rob_box_destroy(ui->box);
	free(ui);
}

static const void*
extension_data(const char* uri)
{
	return NULL;
}

/******************************************************************************
 * handle data from backend
 */

#define PARSE_A_DOUBLE(var, dest) \
	if (var && var->type == uris->atom_Double) { \
		dest = ((LV2_Atom_Double*)var)->body; \
	}

#define PARSE_A_INT(var, dest) \
	if (var && var->type == uris->atom_Int) { \
		dest = ((LV2_Atom_Int*)var)->body; \
	}

static void
port_event(LV2UI_Handle handle,
           uint32_t     port_index,
           uint32_t     buffer_size,
           uint32_t     format,
           const void*  buffer)
{
	SDHui* ui = (SDHui*)handle;
	const EBULV2URIs* uris = &ui->uris;

	if (format == uris->atom_eventTransfer) {
		LV2_Atom* atom = (LV2_Atom*)buffer;

		if (atom->type == uris->atom_Blank || atom->type == uris->atom_Object) {
			LV2_Atom_Object* obj = (LV2_Atom_Object*)atom;
			if (obj->body.otype == uris->mtr_control) {
				int k; float v;
				get_cc_key_value(&ui->uris, obj, &k, &v);
				if (k == CTL_LV2_FTM) {
					int vv = v;
					ui->disable_signals = true;
					robtk_cbtn_set_active(ui->cbx_autoreset, (vv&2)==2);
					robtk_cbtn_set_active(ui->cbx_transport, (vv&1)==1);
					ui->disable_signals = false;
				} else if (k == CTL_LV2_RESETRADAR) {
					ui->hist_max = 0;
					ui->hist_var = 0;
					ui->hist_avg = 0;
					ui->hist_peakbin = -1;
					for (int i=0; i < HIST_LEN; ++i) {
						ui->histS[i] = 0;
					}
					invalidate_changed(ui, -1);
					btn_start_sens (ui); // maybe reset 2^31 limit.
				} else if (k == CTL_SAMPLERATE) {
					if (v > 0) {
						ui->rate = v;
					}
					invalidate_changed(ui, 0);
				} else if (k == CTL_LV2_RESYNCDONE) {
					invalidate_changed(ui, -1);
				}
			} else if (obj->body.otype == uris->sdh_histogram) {
				LV2_Atom *hm = NULL;
				LV2_Atom *hd = NULL;
				LV2_Atom *ha = NULL;
				LV2_Atom *hv = NULL;
				LV2_Atom *hp = NULL;
				if (5 == lv2_atom_object_get(obj,
							uris->sdh_hist_max, &hm,
							uris->sdh_hist_avg, &ha,
							uris->sdh_hist_var, &hv,
							uris->sdh_hist_peak, &hp,
							uris->sdh_hist_data, &hd,
							NULL)
						&& hm && hd && ha && hv && hp
						&& hm->type == uris->atom_Int
						&& ha->type == uris->atom_Double
						&& hv->type == uris->atom_Double
						&& hp->type == uris->atom_Int
						&& hd->type == uris->atom_Vector
					 )
				{
					PARSE_A_INT(hm, ui->hist_max);
					PARSE_A_INT(hp, ui->hist_peakbin);
					PARSE_A_DOUBLE(ha, ui->hist_avg);
					PARSE_A_DOUBLE(hv, ui->hist_var);

					LV2_Atom_Vector* data = (LV2_Atom_Vector*)LV2_ATOM_BODY(hd);

					if (data->atom.type == uris->atom_Int) {
						const size_t n_elem = (hd->size - sizeof(LV2_Atom_Vector_Body)) / data->atom.size;
						const int *d = (int*) LV2_ATOM_BODY(&data->atom);
						memcpy(ui->histS, d, sizeof(int) * n_elem);
					}
					invalidate_changed(ui, 0);
				}
			} else if (obj->body.otype == uris->sdh_information) {
				LV2_Atom *ii = NULL;
				LV2_Atom *it = NULL;
				lv2_atom_object_get(obj,
						uris->ebu_integrating, &ii,
						uris->ebu_integr_time, &it,
						NULL
						);

				if (it && it->type == uris->atom_Long) {
					ui->integration_spl = (uint64_t)(((LV2_Atom_Long*)it)->body);
					btn_start_sens (ui); // maybe set 2^31 limit.
				}

				if (ii && ii->type == uris->atom_Bool) {
					bool ix = ((LV2_Atom_Bool*)ii)->body;
					bool bx = robtk_cbtn_get_active(ui->btn_start);
					if (ix != bx) {
						ui->disable_signals = true;
						robtk_cbtn_set_active(ui->btn_start, ix);
						ui->disable_signals = false;
					}
				}

			} else {
				fprintf(stderr, "UI: Unknown control message.\n");
			}
		} else {
			fprintf(stderr, "UI: Unknown message type.\n");
		}
	}
}