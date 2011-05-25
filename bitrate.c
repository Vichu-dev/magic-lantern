/** \file
 * Bitrate
 */
#include "dryos.h"
#include "bmp.h"
#include "tasks.h"
#include "debug.h"
#include "menu.h"
#include "property.h"
#include "config.h"
#include "gui.h"
#include "lens.h"


//----------------begin qscale-----------------
CONFIG_INT( "h264.qscale.plus16", qscale_plus16, 16-8 );
CONFIG_INT( "h264.bitrate.mode", bitrate_mode, 0 ); // off, CBR, VBR
CONFIG_INT( "h264.bitrate.factor", bitrate_factor, 10 );
CONFIG_INT( "time.indicator", time_indicator, 3); // 0 = off, 1 = current clip length, 2 = time remaining until filling the card, 3 = time remaining until 4GB
int timecode_x = 720 - 160;
int timecode_y = 0;
int timecode_width = 160;
int timecode_height = 20;
int timecode_warning = 120;
static unsigned timecode_font	= FONT(FONT_MED, COLOR_RED, COLOR_BG );

int measured_bitrate = 0; // mbps
int free_space_32k = 0;
int movie_bytes_written_32k = 0;

#define qscale (((int)qscale_plus16) - 16)

int bitrate_dirty = 0;

// don't call those outside vbr_fix / vbr_set
void mvrFixQScale(uint16_t *);    // only safe to call when not recording
void mvrSetDefQScale(int16_t *);  // when recording, only change qscale by 1 at a time
// otherwise ther appears a nice error message which shows the shutter count [quote AlinS] :)

static struct mvr_config mvr_config_copy;
void cbr_init()
{
	memcpy(&mvr_config_copy, &mvr_config, sizeof(mvr_config_copy));
}

void vbr_fix(uint16_t param)
{
	if (!lv_drawn()) return;
	if (shooting_mode != SHOOTMODE_MOVIE) return; 
	if (recording) return; // err70 if you do this while recording

	mvrFixQScale(&param);
}

const int fps[] = {30, 25, 24, 60, 50, 60, 50};
const int res[] = { 0,  0,  0,  1,  1,  2,  2};
extern int video_mode_fps;
extern int video_mode_resolution;
int* get_opt_size_ptr(struct mvr_config * mc, int gop)
{
	int i;
	for (i = 0; i < 7; i++)
		if (fps[i] == video_mode_fps && res[i] == video_mode_resolution)
			if (gop)
				return &(mc->fullhd_30fps_gop_opt_0) + i * 5;
			else
				return &(mc->fullhd_opt_size_I_30fps) + i * 5;
}

void mvrSetOptSize(int* arg)
{
	switch (video_mode_resolution)
	{
		case 0: mvrSetFullHDOptSize(arg); return;
		case 1: mvrSetHDOptSize(arg); return;
		case 2: mvrSetVGAOptSize(arg); return;
	}
}

void mvrSetGopOptSize(int* arg)
{
	switch (video_mode_resolution)
	{
		case 0: mvrSetGopOptSizeFULLHD(arg); return;
		case 1: mvrSetGopOptSizeHD(arg); return;
		case 2: mvrSetGopOptSizeVGA(arg); return;
	}
}

void opt_set(int num, int den)
{
	int opt[2];
	int gop[5];
	int i;
	
	int* opt_size = get_opt_size_ptr(&mvr_config_copy, 0);
	for (i = 0; i < 2; i++) opt[i] = opt_size[i] * num / den;

	int* gop_size = get_opt_size_ptr(&mvr_config_copy, 1);
	for (i = 0; i < 3; i++) gop[i] = gop_size[i] * num / den;

	mvrSetGopOptSize(gop);
	mvrSetOptSize(opt);
}
void bitrate_set()
{
	if (!lv_drawn()) return;
	if (shooting_mode != SHOOTMODE_MOVIE) return; 
	if (gui_menu_shown()) return;
	if (recording) return; 
	
	if (bitrate_mode == 0)
	{
		if (!bitrate_dirty) return;
		vbr_fix(0);
		opt_set(1,1);
	}
	else if (bitrate_mode == 1) // CBR
	{
		vbr_fix(0);
		opt_set(bitrate_factor, 10);
	}
	else if (bitrate_mode == 2) // QScale
	{
		vbr_fix(1);
		opt_set(1,1);
		int q = qscale;
		mvrSetDefQScale(&q);
	}
	bitrate_dirty = 1;
}

static void
bitrate_print(
	void *			priv,
	int			x,
	int			y,
	int			selected
)
{
	if (bitrate_mode == 0)
	{
		bmp_printf( selected ? MENU_FONT_SEL : MENU_FONT, x, y, "Bit Rate      : FW default%s", bitrate_dirty ? "(reboot)" : "");
		menu_draw_icon(x, y, bitrate_dirty ? MNI_WARNING : MNI_OFF, 0);
	}
	else if (bitrate_mode == 1)
	{
		bmp_printf( selected ? MENU_FONT_SEL : MENU_FONT, x, y, "Bit Rate      : CBR, %d.%dx", bitrate_factor/10, bitrate_factor%10);
		menu_draw_icon(x, y, MNI_PERCENT, bitrate_factor * 100 / 30);
	}
	else if (bitrate_mode == 2)
	{
		bmp_printf( selected ? MENU_FONT_SEL : MENU_FONT, x, y, "Bit Rate      : QScale %d", qscale);
		menu_draw_icon(x, y, MNI_PERCENT, -(qscale-16) * 100 / 32);
	}
}

static void 
bitrate_toggle_forward(void* priv)
{
	if (recording) return;
	if (bitrate_mode == 0)
		return;
	else if (bitrate_mode == 1 && !recording)
		bitrate_factor = mod(bitrate_factor, 30) + 1;
	else if (bitrate_mode == 2)
		qscale_plus16 = mod(qscale_plus16 - 1, 33);
}

static void 
bitrate_toggle_reverse(void* priv)
{
	if (recording) return;
	if (bitrate_mode == 0)
		return;
	else if (bitrate_mode == 1)
		bitrate_factor = mod(bitrate_factor - 2, 30) + 1;
	else if (bitrate_mode == 2)
		qscale_plus16 = mod(qscale_plus16 + 1, 33);
}

static void 
bitrate_toggle_mode(void* priv)
{
	if (recording) return;
	menu_ternary_toggle(priv);
}

int movie_elapsed_time_01s = 0;   // seconds since starting the current movie * 10

PROP_HANDLER(PROP_FREE_SPACE)
{
	free_space_32k = buf[0];
	return prop_cleanup(token, property);
}

void free_space_show()
{
	if (!get_global_draw()) return;
	if (recording && time_indicator) return;
	int fsg = free_space_32k >> 15;
	int fsgr = free_space_32k - (fsg << 15);
	int fsgf = (fsgr * 10) >> 15;

	bmp_printf(
		FONT(FONT_MED, COLOR_WHITE, TOPBAR_BGCOLOR),
		timecode_x + 7 * fontspec_font(timecode_font)->width,
		timecode_y,
		"%d.%dGB",
		fsg,
		fsgf
	);
}

void time_indicator_show()
{
	if (!recording) 
	{
		free_space_show();
		return;
	}
	
	// time until filling the card
	// in "movie_elapsed_time_01s" seconds, the camera saved "movie_bytes_written_32k"x32kbytes, and there are left "free_space_32k"x32kbytes
	int time_cardfill = movie_elapsed_time_01s * free_space_32k / movie_bytes_written_32k / 10;
	
	// time until 4 GB
	int time_4gb = movie_elapsed_time_01s * (4 * 1024 * 1024 / 32 - movie_bytes_written_32k) / movie_bytes_written_32k / 10;

	//~ bmp_printf(FONT_MED, 0, 300, "%d %d %d %d ", movie_elapsed_time_01s, movie_elapsed_ticks, rec_time_card, rec_time_4gb);

	// what to display
	int dispvalue = time_indicator == 1 ? movie_elapsed_time_01s / 10:
					time_indicator == 2 ? time_cardfill :
					time_indicator == 3 ? MIN(time_4gb, time_cardfill)
					: 0;
	
	if (time_indicator)
	{
		bmp_printf(
			time_4gb < timecode_warning ? timecode_font : FONT(FONT_MED, COLOR_WHITE, TOPBAR_BGCOLOR),
			timecode_x + 5 * fontspec_font(timecode_font)->width,
			timecode_y,
			"%4d:%02d",
			dispvalue / 60,
			dispvalue % 60
		);
		bmp_printf( FONT(FONT_MED, COLOR_WHITE, TOPBAR_BGCOLOR), 
			timecode_x + 9 * fontspec_font(timecode_font)->width,
			timecode_y + 38,
			"A%3d",
			movie_bytes_written_32k * 32 * 80 / 1024 / movie_elapsed_time_01s);

		int fnts = FONT(FONT_SMALL, mvr_config.actual_qscale_maybe == -16 ? COLOR_RED : COLOR_WHITE, TOPBAR_BGCOLOR);
		int fntm = FONT(FONT_MED, mvr_config.actual_qscale_maybe == -16 ? COLOR_RED : COLOR_WHITE, TOPBAR_BGCOLOR);
		bmp_printf(fntm,
			timecode_x + 5 * fontspec_font(timecode_font)->width,
			timecode_y + 18,
			"%4d",
			measured_bitrate
		);
		bmp_printf(fnts,
			timecode_x + 11 * fontspec_font(timecode_font)->width + 5,
			timecode_y + 25,
			"%s%d ",
			mvr_config.actual_qscale_maybe < 0 ? "-" : "+",
			ABS(mvr_config.actual_qscale_maybe)
		);
	}
}

void measure_bitrate() // called once / second
{
	static uint32_t prev_bytes_written = 0;
	uint32_t bytes_written = MVR_BYTES_WRITTEN;
	int bytes_delta = (((int)(bytes_written >> 1)) - ((int)(prev_bytes_written >> 1))) << 1;
	prev_bytes_written = bytes_written;
	movie_bytes_written_32k = bytes_written >> 15;
	measured_bitrate = (ABS(bytes_delta) / 1024) * 8 / 1024;
}

static void
time_indicator_display( void * priv, int x, int y, int selected )
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"Time Indicator: %s",
		time_indicator == 1 ? "Elapsed" :
		time_indicator == 2 ? "Remain.Card" :
		time_indicator == 3 ? "Remain.4GB" : "OFF"
	);
}

CONFIG_INT("buffer.warning.level", buffer_warning_level, 70);
static void
buffer_warning_level_display( void * priv, int x, int y, int selected )
{
	bmp_printf(
		selected ? MENU_FONT_SEL : MENU_FONT,
		x, y,
		"BuffWarnLevel : %d%%",
		buffer_warning_level
	);
	menu_draw_icon(x, y, MNI_PERCENT, buffer_warning_level);
}

static void buffer_warning_level_toggle(int step)
{
	buffer_warning_level += step;
	if (buffer_warning_level > 100) buffer_warning_level = 30;
	if (buffer_warning_level < 30) buffer_warning_level = 100;
}

static void buffer_warning_level_toggle_forward() { buffer_warning_level_toggle(5); }
static void buffer_warning_level_toggle_reverse() { buffer_warning_level_toggle(-5); }

int warning = 0;
int is_mvr_buffer_almost_full() 
{
	if (recording == 0) return 0;
	if (recording == 1) return 1;
	// 2
	
	int ans = MVR_BUFFER_USAGE > buffer_warning_level;
	if (ans) warning = 10;
	return warning;
}

void show_mvr_buffer_status()
{
	int fnt = warning ? FONT(FONT_SMALL, COLOR_WHITE, COLOR_RED) : FONT(FONT_SMALL, COLOR_WHITE, COLOR_GREEN2);
	if (warning) warning--;
	if (recording) bmp_printf(fnt, 680, 60, "%3d%%", MVR_BUFFER_USAGE);
}


static struct menu_entry mov_menus[] = {
	{
		.priv = &bitrate_mode,
		.display	= bitrate_print,
		.select		= bitrate_toggle_mode,
		.select_auto	= bitrate_toggle_forward,
		.select_reverse	= bitrate_toggle_reverse,
		.help = "H.264 bitrate. Use with care! [Q/PLAY]: change value."
	},
	{
		.select		= buffer_warning_level_toggle_forward,
		.select_reverse	= buffer_warning_level_toggle_reverse,
		.display	= buffer_warning_level_display,
		.help = "ML will pause CPU-intensive graphics if buffer gets full."
	},
	{
		.priv		= &time_indicator,
		.select		= menu_quaternary_toggle,
		.select_reverse	= menu_quaternary_toggle_reverse,
		.display	= time_indicator_display,
		.help = "Time indicator during recording"
	},
};

void bitrate_init()
{
	menu_add( "Movie", mov_menus, COUNT(mov_menus) );
}
INIT_FUNC(__FILE__, bitrate_init);

static void
bitrate_task( void )
{
	cbr_init();

	while(1)
	{
		msleep(100);
		if (recording) 
		{
			movie_elapsed_time_01s += 1;
			if (movie_elapsed_time_01s % 10 == 0)
			{
				measure_bitrate();
				BMP_SEM( time_indicator_show(); )
			}
			BMP_SEM( show_mvr_buffer_status(); )
		}
		else
		{
			movie_elapsed_time_01s = 0;
			if (movie_elapsed_time_01s % 10 == 0)
				bitrate_set();
		}
		if (zebra_should_run()) 
			BMP_SEM( free_space_show(); )
	}
}

TASK_CREATE("bitrate_task", bitrate_task, 0, 0x1a, 0x1000 );
