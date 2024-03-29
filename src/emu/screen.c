/***************************************************************************

    screen.c

    Core MAME screen device.

****************************************************************************

    Copyright Aaron Giles
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

        * Redistributions of source code must retain the above copyright
          notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
          notice, this list of conditions and the following disclaimer in
          the documentation and/or other materials provided with the
          distribution.
        * Neither the name 'MAME' nor the names of its contributors may be
          used to endorse or promote products derived from this software
          without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY AARON GILES ''AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL AARON GILES BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
    IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/

#include "emu.h"
#include "emuopts.h"
#include "png.h"
#include "rendutil.h"
#ifdef USE_SCALE_EFFECTS
#include "osdscale.h"
#endif /* USE_SCALE_EFFECTS */



//**************************************************************************
//  DEBUGGING
//**************************************************************************

#define VERBOSE						(0)
#define LOG_PARTIAL_UPDATES(x)		do { if (VERBOSE) logerror x; } while (0)



//**************************************************************************
//  GLOBAL VARIABLES
//**************************************************************************

const device_type SCREEN = screen_device_config::static_alloc_device_config;

const attotime screen_device::DEFAULT_FRAME_PERIOD(attotime::from_hz(DEFAULT_FRAME_RATE));



#ifdef USE_SCALE_EFFECTS
//**************************************************************************
//  scaler dimensions
//**************************************************************************
int					use_work_bitmap;
int					scale_depth;
int					scale_xsize;
int					scale_ysize;

int					scale_bank_offset;
#endif /* USE_SCALE_EFFECTS */



//**************************************************************************
//  SCREEN DEVICE CONFIGURATION
//**************************************************************************

//-------------------------------------------------
//  screen_device_config - constructor
//-------------------------------------------------

screen_device_config::screen_device_config(const machine_config &mconfig, const char *tag, const device_config *owner, UINT32 clock)
	: device_config(mconfig, static_alloc_device_config, "Video Screen", tag, owner, clock),
	  m_type(SCREEN_TYPE_RASTER),
	  m_width(0),
	  m_height(0),
	  m_oldstyle_vblank_supplied(false),
	  m_refresh(0),
	  m_vblank(0),
	  m_format(BITMAP_FORMAT_INVALID),
	  m_xoffset(0.0f),
	  m_yoffset(0.0f),
	  m_xscale(1.0f),
	  m_yscale(1.0f),
	  m_screen_update(NULL),
	  m_screen_eof(NULL)
{
}


//-------------------------------------------------
//  static_alloc_device_config - allocate a new
//  configuration object
//-------------------------------------------------

device_config *screen_device_config::static_alloc_device_config(const machine_config &mconfig, const char *tag, const device_config *owner, UINT32 clock)
{
	return global_alloc(screen_device_config(mconfig, tag, owner, clock));
}


//-------------------------------------------------
//  alloc_device - allocate a new device object
//-------------------------------------------------

device_t *screen_device_config::alloc_device(running_machine &machine) const
{
	return auto_alloc(&machine, screen_device(machine, *this));
}


//-------------------------------------------------
//  static_set_format - configuration helper
//  to set the bitmap format
//-------------------------------------------------

void screen_device_config::static_set_format(device_config *device, bitmap_format format)
{
	screen_device_config *screen = downcast<screen_device_config *>(device);
	screen->m_format = format;
}


//-------------------------------------------------
//  static_set_type - configuration helper
//  to set the screen type
//-------------------------------------------------

void screen_device_config::static_set_type(device_config *device, screen_type_enum type)
{
	screen_device_config *screen = downcast<screen_device_config *>(device);
	screen->m_type = type;
}


//-------------------------------------------------
//  static_set_raw - configuration helper
//  to set the raw screen parameters
//-------------------------------------------------

void screen_device_config::static_set_raw(device_config *device, UINT32 pixclock, UINT16 htotal, UINT16 hbend, UINT16 hbstart, UINT16 vtotal, UINT16 vbend, UINT16 vbstart)
{
	screen_device_config *screen = downcast<screen_device_config *>(device);
	screen->m_refresh = HZ_TO_ATTOSECONDS(pixclock) * htotal * vtotal;
	screen->m_vblank = screen->m_refresh / vtotal * (vtotal - (vbstart - vbend));
	screen->m_width = htotal;
	screen->m_height = vtotal;
	screen->m_visarea.min_x = hbend;
	screen->m_visarea.max_x = hbstart - 1;
	screen->m_visarea.min_y = vbend;
	screen->m_visarea.max_y = vbstart - 1;
}


//-------------------------------------------------
//  static_set_refresh - configuration helper
//  to set the refresh rate
//-------------------------------------------------

void screen_device_config::static_set_refresh(device_config *device, attoseconds_t rate)
{
	screen_device_config *screen = downcast<screen_device_config *>(device);
	screen->m_refresh = rate;
}


//-------------------------------------------------
//  static_set_vblank_time - configuration helper
//  to set the VBLANK duration
//-------------------------------------------------

void screen_device_config::static_set_vblank_time(device_config *device, attoseconds_t time)
{
	screen_device_config *screen = downcast<screen_device_config *>(device);
	screen->m_vblank = time;
	screen->m_oldstyle_vblank_supplied = true;
}


//-------------------------------------------------
//  static_set_size - configuration helper to set
//  the width/height of the screen
//-------------------------------------------------

void screen_device_config::static_set_size(device_config *device, UINT16 width, UINT16 height)
{
	screen_device_config *screen = downcast<screen_device_config *>(device);
	screen->m_width = width;
	screen->m_height = height;
}


//-------------------------------------------------
//  static_set_visarea - configuration helper to
//  set the visible area of the screen
//-------------------------------------------------

void screen_device_config::static_set_visarea(device_config *device, INT16 minx, INT16 maxx, INT16 miny, INT16 maxy)
{
	screen_device_config *screen = downcast<screen_device_config *>(device);
	screen->m_visarea.min_x = minx;
	screen->m_visarea.max_x = maxx;
	screen->m_visarea.min_y = miny;
	screen->m_visarea.max_y = maxy;
}


//-------------------------------------------------
//  static_set_default_position - configuration
//  helper to set the default position and scale
//  factors for the screen
//-------------------------------------------------

void screen_device_config::static_set_default_position(device_config *device, double xscale, double xoffs, double yscale, double yoffs)
{
	screen_device_config *screen = downcast<screen_device_config *>(device);
	screen->m_xscale = xscale;
	screen->m_xoffset = xoffs;
	screen->m_yscale = yscale;
	screen->m_yoffset = yoffs;
}


//-------------------------------------------------
//  device_validity_check - verify device
//  configuration
//-------------------------------------------------

bool screen_device_config::device_validity_check(core_options &options, const game_driver &driver) const
{
	bool error = false;

	// sanity check dimensions
	if (m_width <= 0 || m_height <= 0)
	{
		mame_printf_error(_("%s: %s screen '%s' has invalid display dimensions\n"), driver.source_file, driver.name, tag());
		error = true;
	}

	// sanity check display area
	if (m_type != SCREEN_TYPE_VECTOR)
	{
		if ((m_visarea.max_x < m_visarea.min_x) ||
			(m_visarea.max_y < m_visarea.min_y) ||
			(m_visarea.max_x >= m_width) ||
			(m_visarea.max_y >= m_height))
		{
			mame_printf_error(_("%s: %s screen '%s' has an invalid display area\n"), driver.source_file, driver.name, tag());
			error = true;
		}

		// sanity check screen formats
		if (m_format != BITMAP_FORMAT_INDEXED16 &&
			m_format != BITMAP_FORMAT_RGB15 &&
			m_format != BITMAP_FORMAT_RGB32)
		{
			mame_printf_error(_("%s: %s screen '%s' has unsupported format\n"), driver.source_file, driver.name, tag());
			error = true;
		}
	}

	// check for zero frame rate
	if (m_refresh == 0)
	{
		mame_printf_error(_("%s: %s screen '%s' has a zero refresh rate\n"), driver.source_file, driver.name, tag());
		error = true;
	}
	return error;
}




//-------------------------------------------------
//  static_set_screen_update - set the legacy
//  screen update callback in the device
//  configuration
//-------------------------------------------------

void screen_device_config::static_set_screen_update(device_config *device, screen_update_func callback)
{
	assert(device != NULL);
	downcast<screen_device_config *>(device)->m_screen_update = callback;
}


//-------------------------------------------------
//  static_set_screen_eof - set the legacy
//  screen eof callback in the device
//  configuration
//-------------------------------------------------

void screen_device_config::static_set_screen_eof(device_config *device, screen_eof_func callback)
{
	assert(device != NULL);
	downcast<screen_device_config *>(device)->m_screen_eof = callback;
}



//**************************************************************************
//  SCREEN DEVICE
//**************************************************************************

#ifdef MAME_AVI
void screen_device::avi_record_frame()
{
	extern int avi_write_handler(running_machine *machine, emu_file *dummy, bitmap_t *bitmap);
	bitmap_t *bitmap = m_bitmap[1 - m_curbitmap];

	g_profiler.start(PROFILER_MOVIE_REC);
	avi_write_handler(machine, NULL, bitmap);
	g_profiler.stop();
}


void toggle_record_avi(void)
{
	extern int OnAVIRecord(void);	// Implemented In window.c
	OnAVIRecord();
}
#endif /* MAME_AVI */


#ifdef USE_SCALE_EFFECTS
void screen_device::video_init_scale_effect()
{
	use_work_bitmap = (m_texture_format == TEXFORMAT_PALETTE16);
	scale_depth = (m_texture_format == TEXFORMAT_RGB15) ? 15 : 32;

	if (scale_init())
	{
		logerror("WARNING: scale effect is disabled\n");
		scale_effect.effect = 0;
		return;
	}

	if (scale_check(scale_depth))
	{
		int old_depth = scale_depth;

		use_work_bitmap = 1;
		scale_depth = (scale_depth == 15) ? 32 : 15;
		if (scale_check(scale_depth))
		{
			popmessage(_("scale_effect \"%s\" does not support both depth 15 and 32. scale effect is disabled."),
				scale_desc(scale_effect.effect));

			scale_exit();
			scale_effect.effect = 0;
			scale_init();
			return;
		}
		else
			logerror("WARNING: scale_effect \"%s\" does not support depth %d, use depth %d\n", scale_desc(scale_effect.effect), old_depth, scale_depth);
	}

	logerror("scale effect: %s (depth:%d)\n", scale_effect.name, scale_depth);

	realloc_scale_bitmaps();
}


void screen_device::video_exit_scale_effect()
{
	free_scale_bitmap();
	scale_exit();
}


void screen_device::free_scale_bitmap()
{
	palette_t *palette = (m_texture_format == TEXFORMAT_PALETTE16) ? machine->palette : NULL;
	int bank;
	m_changed &= ~UPDATE_HAS_NOT_CHANGED;

	for (bank = 0; bank < 2; bank++)
	{
		// restore mame screen
		if ((m_texture[bank]) && (m_bitmap[bank]))
			m_texture[bank]->set_bitmap(m_bitmap[bank], &m_visarea, m_texture_format, palette);

		if (scale_bitmap[bank] != NULL)
		{
			global_free(scale_bitmap[bank]);
			scale_bitmap[bank] = NULL;
		}

		if (work_bitmap[bank] != NULL)
		{
			global_free(work_bitmap[bank]);
			work_bitmap[bank] = NULL;
		}
	}

	scale_xsize = 0;
	scale_ysize = 0;
}


void screen_device::convert_palette_to_32(const bitmap_t *src, bitmap_t *dst, const rectangle *visarea, UINT32 palettebase)
{
	const rgb_t *palette = palette_entry_list_adjusted(machine->palette) + palettebase;
	int x, y;

	for (y = visarea->min_y; y < visarea->max_y; y++)
	{
		UINT32 *dst32 = BITMAP_ADDR32(dst, y, visarea->min_x);
		UINT16 *src16 = BITMAP_ADDR16(src, y, visarea->min_x);

		for (x = visarea->min_x; x < visarea->max_x; x++)
			*dst32++ = palette[*src16++];
	}
}


void screen_device::convert_palette_to_15(const bitmap_t *src, bitmap_t *dst, const rectangle *visarea, UINT32 palettebase)
{
	const rgb_t *palette = palette_entry_list_adjusted(machine->palette) + palettebase;
	int x, y;

	for (y = visarea->min_y; y < visarea->max_y; y++)
	{
		UINT16 *dst16 = BITMAP_ADDR16(dst, y, visarea->min_x);
		UINT16 *src16 = BITMAP_ADDR16(src, y, visarea->min_x);

		for (x = visarea->min_x; x < visarea->max_x; x++)
			*dst16++ = rgb_to_rgb15(palette[*src16++]);
	}
}


static void convert_15_to_32(const bitmap_t *src, bitmap_t *dst, const rectangle *visarea)
{
	int x, y;

	for (y = visarea->min_y; y < visarea->max_y; y++)
	{
		UINT32 *dst32 = BITMAP_ADDR32(dst, y, visarea->min_x);
		UINT16 *src16 = BITMAP_ADDR16(src, y, visarea->min_x);

		for (x = visarea->min_x; x < visarea->max_x; x++)
		{
			UINT16 pix = *src16++;
			UINT32 color = ((pix & 0x7c00) << 9) | ((pix & 0x03e0) << 6) | ((pix & 0x001f) << 3);
			*dst32++ = color | ((color >> 5) & 0x070707);
		}
	}
}


static void convert_32_to_15(bitmap_t *src, bitmap_t *dst, const rectangle *visarea)
{
	int x, y;

	for (y = visarea->min_y; y < visarea->max_y; y++)
	{
		UINT16 *dst16 = BITMAP_ADDR16(dst, y, visarea->min_x);
		UINT32 *src32 = BITMAP_ADDR32(src, y, visarea->min_x);

		for (x = visarea->min_x; x < visarea->max_x; x++)
			*dst16++ = rgb_to_rgb15(*src32++);
	}
}


void screen_device::texture_set_scale_bitmap(const rectangle *visarea, UINT32 palettebase)
{
	int curbank = m_curbitmap;
	int scalebank = /* scale_bank_offset + */curbank;
	bitmap_t *target = m_bitmap[curbank];
	bitmap_t *dst;
	rectangle fixedvis;
	int width, height;

	width = visarea->max_x - visarea->min_x;
	height = visarea->max_y - visarea->min_y;

	fixedvis.min_x = 0;
	fixedvis.min_y = 0;
	fixedvis.max_x = width * scale_xsize;
	fixedvis.max_y = height * scale_ysize;

	// convert texture to 15 or 32 bit which scaler is capable of rendering
	switch (m_texture_format)
	{
	case TEXFORMAT_PALETTE16:
		target = work_bitmap[curbank];

		if (scale_depth == 32)
			convert_palette_to_32(m_bitmap[curbank], target, visarea, palettebase);
		else
			convert_palette_to_15(m_bitmap[curbank], target, visarea, palettebase);

		break;

	case TEXFORMAT_RGB15:
		if (scale_depth == 15)
			break;

		target = work_bitmap[curbank];
		convert_15_to_32(m_bitmap[curbank], target, visarea);
		break;

	case TEXFORMAT_RGB32:
		if (scale_depth == 32)
			break;

		target = work_bitmap[curbank];
		convert_32_to_15(m_bitmap[curbank], target, visarea);
		break;

	default:
		logerror("unknown texture format\n");
		return;
	}

	dst = scale_bitmap[curbank];
	if (scale_depth == 32)
	{
		UINT32 *src32 = BITMAP_ADDR32(target, visarea->min_y, visarea->min_x);
		UINT32 *dst32 = BITMAP_ADDR32(dst, 0, 0);
		scale_perform_scale((UINT8 *)src32, (UINT8 *)dst32, target->rowpixels * 4, dst->rowpixels * 4, width, height, 32, scale_dirty[curbank], scalebank);
	}
	else
	{
		UINT16 *src16 = BITMAP_ADDR16(target, visarea->min_y, visarea->min_x);
		UINT16 *dst16 = BITMAP_ADDR16(dst, 0, 0);
		scale_perform_scale((UINT8 *)src16, (UINT8 *)dst16, target->rowpixels * 2, dst->rowpixels * 2, width, height, 15, scale_dirty[curbank], scalebank);
	}
	scale_dirty[curbank] = 0;

	m_texture[curbank]->set_bitmap(dst, &fixedvis, (scale_depth == 32) ? TEXFORMAT_RGB32 : TEXFORMAT_RGB15, NULL);
}
#endif /* USE_SCALE_EFFECTS */


//-------------------------------------------------
//  screen_device - constructor
//-------------------------------------------------

screen_device::screen_device(running_machine &_machine, const screen_device_config &config)
	: device_t(_machine, config),
	  m_config(config),
	  m_container(NULL),
	  m_width(m_config.m_width),
	  m_height(m_config.m_height),
	  m_visarea(m_config.m_visarea),
	  m_burnin(NULL),
	  m_curbitmap(0),
	  m_curtexture(0),
	  m_texture_format(0),
	  m_changed(true),
	  m_last_partial_scan(0),
	  m_screen_overlay_bitmap(NULL),
	  m_frame_period(m_config.m_refresh),
	  m_scantime(1),
	  m_pixeltime(1),
	  m_vblank_period(0),
	  m_vblank_start_time(attotime::zero),
	  m_vblank_end_time(attotime::zero),
	  m_vblank_begin_timer(NULL),
	  m_vblank_end_timer(NULL),
	  m_scanline0_timer(NULL),
	  m_scanline_timer(NULL),
	  m_frame_number(0),
	  m_partial_updates_this_frame(0),
	  m_callback_list(NULL)
{
	memset(m_texture, 0, sizeof(m_texture));
	memset(m_bitmap, 0, sizeof(m_bitmap));
#ifdef USE_SCALE_EFFECTS
	memset(scale_bitmap, 0, sizeof(scale_bitmap));
	memset(work_bitmap, 0, sizeof(work_bitmap));
	memset(scale_dirty, 0, sizeof(scale_dirty));
#endif /* USE_SCALE_EFFECTS */
}


//-------------------------------------------------
//  ~screen_device - destructor
//-------------------------------------------------

screen_device::~screen_device()
{
#ifdef USE_SCALE_EFFECTS
	if (scale_bitmap[0] != NULL)
		global_free(scale_bitmap[0]);
	if (scale_bitmap[1] != NULL)
		global_free(scale_bitmap[1]);
#endif /* USE_SCALE_EFFECTS */
	m_machine.render().texture_free(m_texture[0]);
	m_machine.render().texture_free(m_texture[1]);
	if (m_burnin != NULL)
		finalize_burnin();
	global_free(m_screen_overlay_bitmap);
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void screen_device::device_start()
{
	// configure the default cliparea
	render_container::user_settings settings;
	m_container->get_user_settings(settings);
	settings.m_xoffset = m_config.m_xoffset;
	settings.m_yoffset = m_config.m_yoffset;
	settings.m_xscale = m_config.m_xscale;
	settings.m_yscale = m_config.m_yscale;
	m_container->set_user_settings(settings);

	// allocate the VBLANK timers
	m_vblank_begin_timer = machine->scheduler().timer_alloc(FUNC(static_vblank_begin_callback), (void *)this);
	m_vblank_end_timer = machine->scheduler().timer_alloc(FUNC(static_vblank_end_callback), (void *)this);

	// allocate a timer to reset partial updates
	m_scanline0_timer = machine->scheduler().timer_alloc(FUNC(static_scanline0_callback), (void *)this);

	// allocate a timer to generate per-scanline updates
	if ((machine->config->m_video_attributes & VIDEO_UPDATE_SCANLINE) != 0)
		m_scanline_timer = machine->scheduler().timer_alloc(FUNC(static_scanline_update_callback), (void *)this);

	// configure the screen with the default parameters
	configure(m_config.m_width, m_config.m_height, m_config.m_visarea, m_config.m_refresh);

	// reset VBLANK timing
	m_vblank_start_time = attotime::zero;
	m_vblank_end_time = attotime(0, m_vblank_period);

	// start the timer to generate per-scanline updates
	if ((machine->config->m_video_attributes & VIDEO_UPDATE_SCANLINE) != 0)
		m_scanline_timer->adjust(time_until_pos(0));

	// create burn-in bitmap
	if (options_get_int(&machine->options(), OPTION_BURNIN) > 0)
	{
		int width, height;
		if (sscanf(options_get_string(&machine->options(), OPTION_SNAPSIZE), "%dx%d", &width, &height) != 2 || width == 0 || height == 0)
			width = height = 300;
		m_burnin = auto_alloc(machine, bitmap_t(width, height, BITMAP_FORMAT_INDEXED64));
		if (m_burnin == NULL)
			fatalerror("Error allocating burn-in bitmap for screen at (%dx%d)\n", width, height);
		bitmap_fill(m_burnin, NULL, 0);
	}

	// load the effect overlay
	const char *overname = options_get_string(&machine->options(), OPTION_EFFECT);
	if (overname != NULL && strcmp(overname, "none") != 0)
		load_effect_overlay(overname);

	// register items for saving
	save_item(NAME(m_width));
	save_item(NAME(m_height));
	save_item(NAME(m_visarea.min_x));
	save_item(NAME(m_visarea.min_y));
	save_item(NAME(m_visarea.max_x));
	save_item(NAME(m_visarea.max_y));
	save_item(NAME(m_last_partial_scan));
	save_item(NAME(m_frame_period));
	save_item(NAME(m_scantime));
	save_item(NAME(m_pixeltime));
	save_item(NAME(m_vblank_period));
	save_item(NAME(m_vblank_start_time));
	save_item(NAME(m_vblank_end_time));
	save_item(NAME(m_frame_number));
}


//-------------------------------------------------
//  device_post_load - device-specific update
//  after a save state is loaded
//-------------------------------------------------

void screen_device::device_post_load()
{
	realloc_screen_bitmaps();
#ifdef USE_SCALE_EFFECTS
	video_init_scale_effect();
#endif /* USE_SCALE_EFFECTS */
}


//-------------------------------------------------
//  configure - configure screen parameters
//-------------------------------------------------

void screen_device::configure(int width, int height, const rectangle &visarea, attoseconds_t frame_period)
{
	// validate arguments
	assert(width > 0);
	assert(height > 0);
	assert(visarea.min_x >= 0);
	assert(visarea.min_y >= 0);
	assert(m_config.m_type == SCREEN_TYPE_VECTOR || visarea.min_x < width);
	assert(m_config.m_type == SCREEN_TYPE_VECTOR || visarea.min_y < height);
	assert(frame_period > 0);

	// fill in the new parameters
	m_width = width;
	m_height = height;
	m_visarea = visarea;

	// reallocate bitmap if necessary
	realloc_screen_bitmaps();

#ifdef USE_SCALE_EFFECTS
	// init scale
	video_init_scale_effect();
#endif /* USE_SCALE_EFFECTS */

	// compute timing parameters
	m_frame_period = frame_period;
	m_scantime = frame_period / height;
	m_pixeltime = frame_period / (height * width);

	// if there has been no VBLANK time specified in the MACHINE_DRIVER, compute it now
    // from the visible area, otherwise just used the supplied value
	if (m_config.m_vblank == 0 && !m_config.m_oldstyle_vblank_supplied)
		m_vblank_period = m_scantime * (height - (visarea.max_y + 1 - visarea.min_y));
	else
		m_vblank_period = m_config.m_vblank;

	// if we are on scanline 0 already, reset the update timer immediately
	// otherwise, defer until the next scanline 0
	if (vpos() == 0)
		m_scanline0_timer->adjust(attotime::zero);
	else
		m_scanline0_timer->adjust(time_until_pos(0));

	// start the VBLANK timer
	m_vblank_begin_timer->adjust(time_until_vblank_start());

	// adjust speed if necessary
	m_machine.video().update_refresh_speed();
}


//-------------------------------------------------
//  reset_origin - reset the timing such that the
//  given (x,y) occurs at the current time
//-------------------------------------------------

void screen_device::reset_origin(int beamy, int beamx)
{
	// compute the effective VBLANK start/end times
	attotime curtime = machine->time();
	m_vblank_end_time = curtime - attotime(0, beamy * m_scantime + beamx * m_pixeltime);
	m_vblank_start_time = m_vblank_end_time - attotime(0, m_vblank_period);

	// if we are resetting relative to (0,0) == VBLANK end, call the
	// scanline 0 timer by hand now; otherwise, adjust it for the future
	if (beamy == 0 && beamx == 0)
		scanline0_callback();
	else
		m_scanline0_timer->adjust(time_until_pos(0));

	// if we are resetting relative to (visarea.max_y + 1, 0) == VBLANK start,
	// call the VBLANK start timer now; otherwise, adjust it for the future
	if (beamy == m_visarea.max_y + 1 && beamx == 0)
		vblank_begin_callback();
	else
		m_vblank_begin_timer->adjust(time_until_vblank_start());
}


//-------------------------------------------------
//  realloc_screen_bitmaps - reallocate bitmaps
//  and textures as necessary
//-------------------------------------------------

void screen_device::realloc_screen_bitmaps()
{
	if (m_config.m_type == SCREEN_TYPE_VECTOR)
		return;

	int curwidth = 0, curheight = 0;

	// extract the current width/height from the bitmap
	if (m_bitmap[0] != NULL)
	{
		curwidth = m_bitmap[0]->width;
		curheight = m_bitmap[0]->height;
	}

	// if we're too small to contain this width/height, reallocate our bitmaps and textures
	if (m_width > curwidth || m_height > curheight)
	{
		// free what we have currently
		m_machine.render().texture_free(m_texture[0]);
		m_machine.render().texture_free(m_texture[1]);
		auto_free(machine, m_bitmap[0]);
		auto_free(machine, m_bitmap[1]);

		// compute new width/height
		curwidth = MAX(m_width, curwidth);
		curheight = MAX(m_height, curheight);

		// choose the texture format - convert the screen format to a texture format
		palette_t *palette = NULL;
		switch (m_config.m_format)
		{
			case BITMAP_FORMAT_INDEXED16:	m_texture_format = TEXFORMAT_PALETTE16;	palette = machine->palette;	break;
			case BITMAP_FORMAT_RGB15:		m_texture_format = TEXFORMAT_RGB15;		palette = NULL;				break;
			case BITMAP_FORMAT_RGB32:		m_texture_format = TEXFORMAT_RGB32;		palette = NULL;				break;
			default:						fatalerror("Invalid bitmap format!");												break;
		}

		// allocate bitmaps
		m_bitmap[0] = auto_alloc(machine, bitmap_t(curwidth, curheight, m_config.m_format));
		bitmap_set_palette(m_bitmap[0], machine->palette);
		m_bitmap[1] = auto_alloc(machine, bitmap_t(curwidth, curheight, m_config.m_format));
		bitmap_set_palette(m_bitmap[1], machine->palette);

		// allocate textures
		m_texture[0] = m_machine.render().texture_alloc();
		m_texture[0]->set_bitmap(m_bitmap[0], &m_visarea, m_texture_format, palette);
		m_texture[1] = m_machine.render().texture_alloc();
		m_texture[1]->set_bitmap(m_bitmap[1], &m_visarea, m_texture_format, palette);
	}
}


#ifdef USE_SCALE_EFFECTS
//-------------------------------------------------
//  realloc_scale_bitmaps - reallocate scale
//  bitmaps as necessary
//-------------------------------------------------

void screen_device::realloc_scale_bitmaps()
{
	mame_printf_verbose("realloc_scale_bitmaps()\n");

	if (m_config.m_type == SCREEN_TYPE_VECTOR)
		return;

	int curwidth = 0, curheight = 0;
	int cur_scalewidth = 0, cur_scaleheight = 0, cur_xsize = 0, cur_ysize = 0;

	// bitmap has been alloc'd
	curwidth = m_bitmap[0]->width;
	curheight = m_bitmap[0]->height;

	// extract the current width/height from the scale_bitmap
	if (scale_bitmap[0] != NULL)
	{
		cur_scalewidth = scale_bitmap[0]->width;
		cur_scaleheight = scale_bitmap[0]->height;
	}

	// assign new x/y size
	scale_xsize = scale_effect.xsize;
	scale_ysize = scale_effect.ysize;

	scale_bank_offset = 0;

	// reallocate our bitmaps and textures
	if (cur_scalewidth != curwidth * scale_xsize || cur_scaleheight != curheight * scale_ysize)
	{
		int bank;
		bitmap_format screen_format = (scale_depth == 15) ? BITMAP_FORMAT_RGB15 : BITMAP_FORMAT_RGB32;

		for (bank = 0; bank < 2; bank++)
		{
			// free what we have currently
			if (scale_bitmap[bank] != NULL)
				global_free(scale_bitmap[bank]);

			scale_dirty[bank] = 1;

			// compute new width/height
			cur_xsize = MAX(scale_xsize, cur_xsize);
			cur_ysize = MAX(scale_ysize, cur_ysize);

			// allocate scale_bitmaps
			scale_bitmap[bank] = auto_alloc(machine, bitmap_t(curwidth * scale_xsize, curheight * scale_ysize, screen_format));
			if (use_work_bitmap)
				work_bitmap[bank] = auto_alloc(machine, bitmap_t(curwidth, curheight, screen_format));

			mame_printf_verbose("realloc_scale_bitmaps: %dx%d@%dbpp, workerbmp: %d \n", 
								curwidth * scale_xsize, 
								curheight * scale_ysize,
								scale_depth,
								use_work_bitmap
								);
		}
	}
	scale_bank_offset = 1;
}
#endif /* USE_SCALE_EFFECTS */


//-------------------------------------------------
//  set_visible_area - just set the visible area
//-------------------------------------------------

void screen_device::set_visible_area(int min_x, int max_x, int min_y, int max_y)
{
	// validate arguments
	assert(min_x >= 0);
	assert(min_y >= 0);
	assert(min_x < max_x);
	assert(min_y < max_y);

	rectangle visarea;
	visarea.min_x = min_x;
	visarea.max_x = max_x;
	visarea.min_y = min_y;
	visarea.max_y = max_y;

	configure(m_width, m_height, visarea, m_frame_period);
}


//-------------------------------------------------
//  update_partial - perform a partial update from
//  the last scanline up to and including the
//  specified scanline
//-----------------------------------------------*/

bool screen_device::update_partial(int scanline)
{
	// validate arguments
	assert(scanline >= 0);

	LOG_PARTIAL_UPDATES(("Partial: update_partial(%s, %d): ", tag(), scanline));

	// these two checks only apply if we're allowed to skip frames
	if (!(machine->config->m_video_attributes & VIDEO_ALWAYS_UPDATE))
	{
		// if skipping this frame, bail
		if (m_machine.video().skip_this_frame())
		{
			LOG_PARTIAL_UPDATES(("skipped due to frameskipping\n"));
			return FALSE;
		}

		// skip if this screen is not visible anywhere
		if (!m_machine.render().is_live(*this))
		{
			LOG_PARTIAL_UPDATES(("skipped because screen not live\n"));
			return FALSE;
		}
	}

	// skip if less than the lowest so far
	if (scanline < m_last_partial_scan)
	{
		LOG_PARTIAL_UPDATES(("skipped because less than previous\n"));
		return FALSE;
	}

	// set the start/end scanlines
	rectangle clip = m_visarea;
	if (m_last_partial_scan > clip.min_y)
		clip.min_y = m_last_partial_scan;
	if (scanline < clip.max_y)
		clip.max_y = scanline;

	// render if necessary
	bool result = false;
	if (clip.min_y <= clip.max_y)
	{
		UINT32 flags = UPDATE_HAS_NOT_CHANGED;

		g_profiler.start(PROFILER_VIDEO);
		LOG_PARTIAL_UPDATES(("updating %d-%d\n", clip.min_y, clip.max_y));

		flags = screen_update(*m_bitmap[m_curbitmap], clip);
		m_partial_updates_this_frame++;
		g_profiler.stop();

		// if we modified the bitmap, we have to commit
		m_changed |= ~flags & UPDATE_HAS_NOT_CHANGED;
		result = true;
	}

	// remember where we left off
	m_last_partial_scan = scanline + 1;
	return result;
}


//-------------------------------------------------
//  update_now - perform an update from the last
//  beam position up to the current beam position
//-------------------------------------------------

void screen_device::update_now()
{
	int current_vpos = vpos();
	int current_hpos = hpos();

	// since we can currently update only at the scanline
    // level, we are trying to do the right thing by
    // updating including the current scanline, only if the
    // beam is past the halfway point horizontally.
    // If the beam is in the first half of the scanline,
    // we only update up to the previous scanline.
    // This minimizes the number of pixels that might be drawn
    // incorrectly until we support a pixel level granularity
	if (current_hpos < (m_width / 2) && current_vpos > 0)
		current_vpos = current_vpos - 1;

	update_partial(current_vpos);
}


//-------------------------------------------------
//  vpos - returns the current vertical position
//  of the beam
//-------------------------------------------------

int screen_device::vpos() const
{
	attoseconds_t delta = (machine->time() - m_vblank_start_time).as_attoseconds();
	int vpos;

	// round to the nearest pixel
	delta += m_pixeltime / 2;

	// compute the v position relative to the start of VBLANK
	vpos = delta / m_scantime;

	// adjust for the fact that VBLANK starts at the bottom of the visible area
	return (m_visarea.max_y + 1 + vpos) % m_height;
}


//-------------------------------------------------
//  hpos - returns the current horizontal position
//  of the beam
//-------------------------------------------------

int screen_device::hpos() const
{
	attoseconds_t delta = (machine->time() - m_vblank_start_time).as_attoseconds();

	// round to the nearest pixel
	delta += m_pixeltime / 2;

	// compute the v position relative to the start of VBLANK
	int vpos = delta / m_scantime;

	// subtract that from the total time
	delta -= vpos * m_scantime;

	// return the pixel offset from the start of this scanline
	return delta / m_pixeltime;
}


//-------------------------------------------------
//  time_until_pos - returns the amount of time
//  remaining until the beam is at the given
//  hpos,vpos
//-------------------------------------------------

attotime screen_device::time_until_pos(int vpos, int hpos) const
{
	// validate arguments
	assert(vpos >= 0);
	assert(hpos >= 0);

	// since we measure time relative to VBLANK, compute the scanline offset from VBLANK
	vpos += m_height - (m_visarea.max_y + 1);
	vpos %= m_height;

	// compute the delta for the given X,Y position
	attoseconds_t targetdelta = (attoseconds_t)vpos * m_scantime + (attoseconds_t)hpos * m_pixeltime;

	// if we're past that time (within 1/2 of a pixel), head to the next frame
	attoseconds_t curdelta = (machine->time() - m_vblank_start_time).as_attoseconds();
	if (targetdelta <= curdelta + m_pixeltime / 2)
		targetdelta += m_frame_period;
	while (targetdelta <= curdelta)
		targetdelta += m_frame_period;

	// return the difference
	return attotime(0, targetdelta - curdelta);
}


//-------------------------------------------------
//  time_until_vblank_end - returns the amount of
//  time remaining until the end of the current
//  VBLANK (if in progress) or the end of the next
//  VBLANK
//-------------------------------------------------

attotime screen_device::time_until_vblank_end() const
{
	// if we are in the VBLANK region, compute the time until the end of the current VBLANK period
	attotime target_time = m_vblank_end_time;
	if (!vblank())
		target_time += attotime(0, m_frame_period);
	return target_time - machine->time();
}


//-------------------------------------------------
//  register_vblank_callback - registers a VBLANK
//  callback
//-------------------------------------------------

void screen_device::register_vblank_callback(vblank_state_changed_func vblank_callback, void *param)
{
	// validate arguments
	assert(vblank_callback != NULL);

	// check if we already have this callback registered
	callback_item **itemptr;
	for (itemptr = &m_callback_list; *itemptr != NULL; itemptr = &(*itemptr)->m_next)
		if ((*itemptr)->m_callback == vblank_callback)
			break;

	// if not found, register
	if (*itemptr == NULL)
	{
		*itemptr = auto_alloc(machine, callback_item);
		(*itemptr)->m_next = NULL;
		(*itemptr)->m_callback = vblank_callback;
		(*itemptr)->m_param = param;
	}
}


//-------------------------------------------------
//  vblank_begin_callback - call any external
//  callbacks to signal the VBLANK period has begun
//-------------------------------------------------

void screen_device::vblank_begin_callback()
{
	// reset the starting VBLANK time
	m_vblank_start_time = machine->time();
	m_vblank_end_time = m_vblank_start_time + attotime(0, m_vblank_period);

	// call the screen specific callbacks
	for (callback_item *item = m_callback_list; item != NULL; item = item->m_next)
		(*item->m_callback)(*this, item->m_param, true);

	// if this is the primary screen and we need to update now
	if (this == machine->primary_screen && !(machine->config->m_video_attributes & VIDEO_UPDATE_AFTER_VBLANK))
		machine->video().frame_update();

	// reset the VBLANK start timer for the next frame
	m_vblank_begin_timer->adjust(time_until_vblank_start());

	// if no VBLANK period, call the VBLANK end callback immedietely, otherwise reset the timer
	if (m_vblank_period == 0)
		vblank_end_callback();
	else
		m_vblank_end_timer->adjust(time_until_vblank_end());
}


//-------------------------------------------------
//  vblank_end_callback - call any external
//  callbacks to signal the VBLANK period has ended
//-------------------------------------------------

void screen_device::vblank_end_callback()
{
	// call the screen specific callbacks
	for (callback_item *item = m_callback_list; item != NULL; item = item->m_next)
		(*item->m_callback)(*this, item->m_param, false);

	// if this is the primary screen and we need to update now
	if (this == machine->primary_screen && (machine->config->m_video_attributes & VIDEO_UPDATE_AFTER_VBLANK))
		machine->video().frame_update();

	// increment the frame number counter
	m_frame_number++;
}


//-------------------------------------------------
//  scanline0_callback - reset partial updates
//  for a screen
//-------------------------------------------------

void screen_device::scanline0_callback()
{
	// reset partial updates
	m_last_partial_scan = 0;
	m_partial_updates_this_frame = 0;

	m_scanline0_timer->adjust(time_until_pos(0));
}


//-------------------------------------------------
//  scanline_update_callback - perform partial
//  updates on each scanline
//-------------------------------------------------

void screen_device::scanline_update_callback(int scanline)
{
	// force a partial update to the current scanline
	update_partial(scanline);

	// compute the next visible scanline
	scanline++;
	if (scanline > m_visarea.max_y)
		scanline = m_visarea.min_y;
	m_scanline_timer->adjust(time_until_pos(scanline), scanline);
}


//-------------------------------------------------
//  update_quads - set up the quads for this
//  screen
//-------------------------------------------------

bool screen_device::update_quads()
{
	// only update if live
	if (m_machine.render().is_live(*this))
	{
		// only update if empty and not a vector game; otherwise assume the driver did it directly
		if (m_config.m_type != SCREEN_TYPE_VECTOR && (machine->config->m_video_attributes & VIDEO_SELF_RENDER) == 0)
		{
			// if we're not skipping the frame and if the screen actually changed, then update the texture
			if (!m_machine.video().skip_this_frame() && m_changed)
			{
				rectangle fixedvis = m_visarea;
				fixedvis.max_x++;
				fixedvis.max_y++;

				palette_t *palette = (m_texture_format == TEXFORMAT_PALETTE16) ? machine->palette : NULL;
#ifdef USE_SCALE_EFFECTS
				if (scale_effect.effect > 0)
					texture_set_scale_bitmap(&fixedvis, 0);
				else
#endif /* USE_SCALE_EFFECTS */
				m_texture[m_curbitmap]->set_bitmap(m_bitmap[m_curbitmap], &fixedvis, m_texture_format, palette);

				m_curtexture = m_curbitmap;
				m_curbitmap = 1 - m_curbitmap;
			}

			// create an empty container with a single quad
			m_container->empty();
			m_container->add_quad(0.0f, 0.0f, 1.0f, 1.0f, MAKE_ARGB(0xff,0xff,0xff,0xff), m_texture[m_curtexture], PRIMFLAG_BLENDMODE(BLENDMODE_NONE) | PRIMFLAG_SCREENTEX(1));
		}
	}

	// reset the screen changed flags
	bool result = m_changed;
	m_changed = false;
	return result;
}


//-------------------------------------------------
//  update_burnin - update the burnin bitmap
//-------------------------------------------------

void screen_device::update_burnin()
{
#undef rand
	if (m_burnin == NULL)
		return;

	bitmap_t *srcbitmap = m_bitmap[m_curtexture];
	if (srcbitmap == NULL)
		return;

	int srcwidth = srcbitmap->width;
	int srcheight = srcbitmap->height;
	int dstwidth = m_burnin->width;
	int dstheight = m_burnin->height;
	int xstep = (srcwidth << 16) / dstwidth;
	int ystep = (srcheight << 16) / dstheight;
	int xstart = ((UINT32)rand() % 32767) * xstep / 32767;
	int ystart = ((UINT32)rand() % 32767) * ystep / 32767;
	int srcx, srcy;
	int x, y;

	// iterate over rows in the destination
	for (y = 0, srcy = ystart; y < dstheight; y++, srcy += ystep)
	{
		UINT64 *dst = BITMAP_ADDR64(m_burnin, y, 0);

		// handle the 16-bit palettized case
		if (srcbitmap->format == BITMAP_FORMAT_INDEXED16)
		{
			const UINT16 *src = BITMAP_ADDR16(srcbitmap, srcy >> 16, 0);
			const rgb_t *palette = palette_entry_list_adjusted(machine->palette);
			for (x = 0, srcx = xstart; x < dstwidth; x++, srcx += xstep)
			{
				rgb_t pixel = palette[src[srcx >> 16]];
				dst[x] += RGB_GREEN(pixel) + RGB_RED(pixel) + RGB_BLUE(pixel);
			}
		}

		// handle the 15-bit RGB case
		else if (srcbitmap->format == BITMAP_FORMAT_RGB15)
		{
			const UINT16 *src = BITMAP_ADDR16(srcbitmap, srcy >> 16, 0);
			for (x = 0, srcx = xstart; x < dstwidth; x++, srcx += xstep)
			{
				rgb15_t pixel = src[srcx >> 16];
				dst[x] += ((pixel >> 10) & 0x1f) + ((pixel >> 5) & 0x1f) + ((pixel >> 0) & 0x1f);
			}
		}

		// handle the 32-bit RGB case
		else if (srcbitmap->format == BITMAP_FORMAT_RGB32)
		{
			const UINT32 *src = BITMAP_ADDR32(srcbitmap, srcy >> 16, 0);
			for (x = 0, srcx = xstart; x < dstwidth; x++, srcx += xstep)
			{
				rgb_t pixel = src[srcx >> 16];
				dst[x] += RGB_GREEN(pixel) + RGB_RED(pixel) + RGB_BLUE(pixel);
			}
		}
	}
}


//-------------------------------------------------
//  finalize_burnin - finalize the burnin bitmap
//-------------------------------------------------

void screen_device::finalize_burnin()
{
	if (m_burnin == NULL)
		return;

	// compute the scaled visible region
	rectangle scaledvis;
	scaledvis.min_x = m_visarea.min_x * m_burnin->width / m_width;
	scaledvis.max_x = m_visarea.max_x * m_burnin->width / m_width;
	scaledvis.min_y = m_visarea.min_y * m_burnin->height / m_height;
	scaledvis.max_y = m_visarea.max_y * m_burnin->height / m_height;

	// wrap a bitmap around the subregion we care about
	bitmap_t *finalmap = auto_alloc(machine, bitmap_t(scaledvis.max_x + 1 - scaledvis.min_x,
				                        scaledvis.max_y + 1 - scaledvis.min_y,
				                        BITMAP_FORMAT_ARGB32));

	int srcwidth = m_burnin->width;
	int srcheight = m_burnin->height;
	int dstwidth = finalmap->width;
	int dstheight = finalmap->height;
	int xstep = (srcwidth << 16) / dstwidth;
	int ystep = (srcheight << 16) / dstheight;

	// find the maximum value
	UINT64 minval = ~(UINT64)0;
	UINT64 maxval = 0;
	for (int y = 0; y < srcheight; y++)
	{
		UINT64 *src = BITMAP_ADDR64(m_burnin, y, 0);
		for (int x = 0; x < srcwidth; x++)
		{
			minval = MIN(minval, src[x]);
			maxval = MAX(maxval, src[x]);
		}
	}

	if (minval == maxval)
		return;

	// now normalize and convert to RGB
	for (int y = 0, srcy = 0; y < dstheight; y++, srcy += ystep)
	{
		UINT64 *src = BITMAP_ADDR64(m_burnin, srcy >> 16, 0);
		UINT32 *dst = BITMAP_ADDR32(finalmap, y, 0);
		for (int x = 0, srcx = 0; x < dstwidth; x++, srcx += xstep)
		{
			int brightness = (UINT64)(maxval - src[srcx >> 16]) * 255 / (maxval - minval);
			dst[x] = MAKE_ARGB(0xff, brightness, brightness, brightness);
		}
	}

	// write the final PNG

	// compute the name and create the file
	emu_file file(m_machine.options(), SEARCHPATH_SCREENSHOT, OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS);
	file_error filerr = file.open(machine->basename(), PATH_SEPARATOR "burnin-", tag());
	if (filerr == FILERR_NONE)
	{
		png_info pnginfo = { 0 };
		png_error pngerr;
		char text[256];

		// add two text entries describing the image
		sprintf(text, APPNAME " %s", build_version);
		png_add_text(&pnginfo, "Software", text);
		sprintf(text, "%s %s", machine->m_game.manufacturer, machine->m_game.description);
		png_add_text(&pnginfo, "System", text);

		// now do the actual work
		pngerr = png_write_bitmap(file, &pnginfo, finalmap, 0, NULL);

		// free any data allocated
		png_free(&pnginfo);
	}
}


//-------------------------------------------------
//  finalize_burnin - finalize the burnin bitmap
//-------------------------------------------------

void screen_device::load_effect_overlay(const char *filename)
{
	// ensure that there is a .png extension
	astring fullname(filename);
	int extension = fullname.rchr(0, '.');
	if (extension != -1)
		fullname.del(extension, -1);
	fullname.cat(".png");

	// load the file
	emu_file file(m_machine.options(), OPTION_ARTPATH, OPEN_FLAG_READ);
	m_screen_overlay_bitmap = render_load_png(file, NULL, fullname, NULL, NULL);
	if (m_screen_overlay_bitmap != NULL)
		m_container->set_overlay(m_screen_overlay_bitmap);
	else
		mame_printf_warning(_("Unable to load effect PNG file '%s'\n"), fullname.cstr());
}

//-------------------------------------------------
//  screen_update - default implementation which
//  calls to the legacy screen_update function
//-------------------------------------------------

bool screen_device::screen_update(bitmap_t &bitmap, const rectangle &cliprect)
{
	if (m_config.m_screen_update != NULL) {
		return (*m_config.m_screen_update)(this, &bitmap, &cliprect);
	} else {
		m_machine.driver_data<driver_device>()->screen_update(*this, bitmap, cliprect);
	}
	return 0;
}

//-------------------------------------------------
//  screen_eof - default implementation which
//  calls to the legacy screen_update function
//-------------------------------------------------

void screen_device::screen_eof()
{
	if (m_config.m_screen_eof != NULL) {
		return (*m_config.m_screen_eof)(this, machine);
	} else {
		m_machine.driver_data<driver_device>()->screen_eof();
	}
}

