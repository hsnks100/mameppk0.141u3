//============================================================
//
//  video.c - Win32 video handling
//
//============================================================
//
//  Copyright Aaron Giles
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or
//  without modification, are permitted provided that the
//  following conditions are met:
//
//    * Redistributions of source code must retain the above
//      copyright notice, this list of conditions and the
//      following disclaimer.
//    * Redistributions in binary form must reproduce the
//      above copyright notice, this list of conditions and
//      the following disclaimer in the documentation and/or
//      other materials provided with the distribution.
//    * Neither the name 'MAME' nor the names of its
//      contributors may be used to endorse or promote
//      products derived from this software without specific
//      prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY AARON GILES ''AS IS'' AND
//  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
//  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
//  EVENT SHALL AARON GILES BE LIABLE FOR ANY DIRECT,
//  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//  DAMAGE (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
//  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
//  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//============================================================

// standard windows headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Windows 95/NT4 multimonitor stubs
#ifdef WIN95_MULTIMON
#include "multidef.h"
#endif

// MAME headers
#include "emu.h"
#include "emuopts.h"
#include "osdepend.h"
#include "video/vector.h"
#include "render.h"
#include "rendutil.h"
#include "ui.h"
#include "uiinput.h"
#ifdef USE_SCALE_EFFECTS
#include "osdscale.h"
#endif /* USE_SCALE_EFFECTS */

// MAMEOS headers
#include "winmain.h"
#include "video.h"
#include "window.h"
#include "input.h"
#include "debugwin.h"
#include "strconv.h"
#include "config.h"

#ifdef MAME_AVI
#include "Avi.h"
static struct MAME_AVI_STATUS AviStatus;
static char *avi_filename;
#define win_video_window win_window_list->hwnd
#endif /* MAME_AVI */

#ifdef KAILLERA
#include "ui_temp.h"
extern int kPlay;
#endif /* KAILLERA */


//============================================================
//  GLOBAL VARIABLES
//============================================================

win_video_config video_config;



//============================================================
//  LOCAL VARIABLES
//============================================================

// monitor info
win_monitor_info *win_monitor_list;
static win_monitor_info *primary_monitor;

#ifdef USE_SCALE_EFFECTS
static int cur_scale_xsize;
static int cur_scale_ysize;
#endif /* USE_SCALE_EFFECTS */



//============================================================
//  PROTOTYPES
//============================================================

static void winvideo_exit(running_machine &machine);
static void init_monitors(void);
static BOOL CALLBACK monitor_enum_callback(HMONITOR handle, HDC dc, LPRECT rect, LPARAM data);
static win_monitor_info *pick_monitor(core_options &options, int index);

static void check_osd_inputs(running_machine *machine);

static void extract_video_config(running_machine *machine);
static float get_aspect(core_options &options, const char *name, int report_error);
static void get_resolution(core_options &options, const char *name, win_window_config *config, int report_error);



//============================================================
//  winvideo_init
//============================================================

void winvideo_init(running_machine *machine)
{
	int index;

	// ensure we get called on the way out
	machine->add_notifier(MACHINE_NOTIFY_EXIT, winvideo_exit);

	// extract data from the options
	extract_video_config(machine);

	// set up monitors first
	init_monitors();

	// initialize the window system so we can make windows
	winwindow_init(machine);

#ifdef MAME_AVI
	if (avi_filename)
		AviStartCapture(NULL, avi_filename, &AviStatus);
#endif /* MAME_AVI */

	// create the windows
	for (index = 0; index < video_config.numscreens; index++)
		winwindow_video_window_create(machine, index, pick_monitor(machine->options(), index), &video_config.window[index]);
	if (video_config.mode != VIDEO_MODE_NONE)
		SetForegroundWindow(win_window_list->hwnd);

	// possibly create the debug window, but don't show it yet
	if (machine->debug_flags & DEBUG_FLAG_OSD_ENABLED)
		debugwin_init_windows(*machine);
}


//============================================================
//  winvideo_exit
//============================================================

static void winvideo_exit(running_machine &machine)
{
#ifdef MAME_AVI
	if (GetAviCapture())
		AviEndCapture();

	if (avi_filename)
	{
		free(avi_filename);
		avi_filename = NULL;
	}
#endif /* MAME_AVI */

	// free all of our monitor information
	while (win_monitor_list != NULL)
	{
		win_monitor_info *temp = win_monitor_list;
		win_monitor_list = temp->next;
		global_free(temp);
	}
}



//============================================================
//  winvideo_monitor_refresh
//============================================================

void winvideo_monitor_refresh(win_monitor_info *monitor)
{
	BOOL result;

	// fetch the latest info about the monitor
	monitor->info.cbSize = sizeof(monitor->info);
	result = GetMonitorInfo(monitor->handle, (LPMONITORINFO)&monitor->info);
	assert(result);
}



//============================================================
//  winvideo_monitor_get_aspect
//============================================================

float winvideo_monitor_get_aspect(win_monitor_info *monitor)
{
	// refresh the monitor information and compute the aspect
	if (video_config.keepaspect)
	{
		int width, height;
		winvideo_monitor_refresh(monitor);
		width = rect_width(&monitor->info.rcMonitor);
		height = rect_height(&monitor->info.rcMonitor);
		return monitor->aspect / ((float)width / (float)height);
	}
	return 0.0f;
}



//============================================================
//  winvideo_monitor_from_handle
//============================================================

win_monitor_info *winvideo_monitor_from_handle(HMONITOR hmonitor)
{
	win_monitor_info *monitor;

	// find the matching monitor
	for (monitor = win_monitor_list; monitor != NULL; monitor = monitor->next)
		if (monitor->handle == hmonitor)
			return monitor;
	return NULL;
}



//============================================================
//  update
//============================================================

void windows_osd_interface::update(bool skip_redraw)
{
	// ping the watchdog on each update
	winmain_watchdog_ping();

	// if we're not skipping this redraw, update all windows
	if (!skip_redraw)
	{
#ifdef USE_SCALE_EFFECTS
		extern int win_scale_res_changed;
		win_scale_res_changed = 0;

		if (scale_effect.xsize != cur_scale_xsize || scale_effect.ysize != cur_scale_ysize)
		{
			win_scale_res_changed = 1;
			cur_scale_xsize = scale_effect.xsize;
			cur_scale_ysize = scale_effect.ysize;
		}
#endif /* USE_SCALE_EFFECTS */
		for (win_window_info *window = win_window_list; window != NULL; window = window->next)
			winwindow_video_window_update(window);
	}

	// poll the joystick values here
	winwindow_process_events(&machine(), TRUE);
	wininput_poll(&machine());
	check_osd_inputs(&machine());
}



//============================================================
//  init_monitors
//============================================================

static void init_monitors(void)
{
	win_monitor_info **tailptr;

	// make a list of monitors
	win_monitor_list = NULL;
	tailptr = &win_monitor_list;
	EnumDisplayMonitors(NULL, NULL, monitor_enum_callback, (LPARAM)&tailptr);

	// if we're verbose, print the list of monitors
	{
		win_monitor_info *monitor;
		for (monitor = win_monitor_list; monitor != NULL; monitor = monitor->next)
		{
			char *utf8_device = utf8_from_tstring(monitor->info.szDevice);
			if (utf8_device != NULL)
			{
				mame_printf_verbose(_WINDOWS("Video: Monitor %p = \"%s\" %s\n"), monitor->handle, utf8_device, (monitor == primary_monitor) ? _WINDOWS("(primary)") : "");
				osd_free(utf8_device);
			}
		}
	}
}



//============================================================
//  monitor_enum_callback
//============================================================

static BOOL CALLBACK monitor_enum_callback(HMONITOR handle, HDC dc, LPRECT rect, LPARAM data)
{
	win_monitor_info ***tailptr = (win_monitor_info ***)data;
	win_monitor_info *monitor;
	MONITORINFOEX info;
	BOOL result;

	// get the monitor info
	info.cbSize = sizeof(info);
	result = GetMonitorInfo(handle, (LPMONITORINFO)&info);
	assert(result);

	// allocate a new monitor info
	monitor = global_alloc_clear(win_monitor_info);

	// copy in the data
	monitor->handle = handle;
	monitor->info = info;

	// guess the aspect ratio assuming square pixels
	monitor->aspect = (float)(info.rcMonitor.right - info.rcMonitor.left) / (float)(info.rcMonitor.bottom - info.rcMonitor.top);

	// save the primary monitor handle
	if (monitor->info.dwFlags & MONITORINFOF_PRIMARY)
		primary_monitor = monitor;

	// hook us into the list
	**tailptr = monitor;
	*tailptr = &monitor->next;

	// enumerate all the available monitors so to list their names in verbose mode
	return TRUE;
}



//============================================================
//  pick_monitor
//============================================================

static win_monitor_info *pick_monitor(core_options &options, int index)
{
	const char *scrname, *scrname2;
	win_monitor_info *monitor;
	int moncount = 0;
	char option[20];
	float aspect;

	// get the screen option
	scrname = options_get_string(&options, WINOPTION_SCREEN);
	sprintf(option, "screen%d", index);
	scrname2 = options_get_string(&options, option);

	// decide which one we want to use
	if (strcmp(scrname2, "auto") != 0)
		scrname = scrname2;

	// get the aspect ratio
	sprintf(option, "aspect%d", index);
	aspect = get_aspect(options, option, TRUE);

	// look for a match in the name first
	if (scrname[0] != 0)
		for (monitor = win_monitor_list; monitor != NULL; monitor = monitor->next)
		{
			char *utf8_device;
			int rc = 1;

			moncount++;

			utf8_device = utf8_from_tstring(monitor->info.szDevice);
			if (utf8_device != NULL)
			{
				rc = strcmp(scrname, utf8_device);
				osd_free(utf8_device);
			}
			if (rc == 0)
				goto finishit;
		}

	// didn't find it; alternate monitors until we hit the jackpot
	index %= moncount;
	for (monitor = win_monitor_list; monitor != NULL; monitor = monitor->next)
		if (index-- == 0)
			goto finishit;

	// return the primary just in case all else fails
	monitor = primary_monitor;

finishit:
	if (aspect != 0)
		monitor->aspect = aspect;
	return monitor;
}



//============================================================
//  check_osd_inputs
//============================================================

static void check_osd_inputs(running_machine *machine)
{
	// check for toggling fullscreen mode
	if (ui_input_pressed(machine, IPT_OSD_1))
		winwindow_toggle_full_screen();
}



//============================================================
//  extract_video_config
//============================================================

static void extract_video_config(running_machine *machine)
{
	const char *stemp;

#ifdef USE_SCALE_EFFECTS
	stemp = options_get_string(&machine->options(), OPTION_SCALE_EFFECT);

	if (stemp)
	{
		scale_decode(stemp);

		if (scale_effect.effect)
			mame_printf_verbose(_WINDOWS("Using %s scale effect\n"), scale_desc(scale_effect.effect));
	}
#endif /* USE_SCALE_EFFECTS */

	// global options: extract the data
	video_config.windowed      = options_get_bool(&machine->options(), WINOPTION_WINDOW);
	video_config.prescale      = options_get_int(&machine->options(), WINOPTION_PRESCALE);
	video_config.keepaspect    = options_get_bool(&machine->options(), WINOPTION_KEEPASPECT);
	video_config.numscreens    = options_get_int(&machine->options(), WINOPTION_NUMSCREENS);

	// if we are in debug mode, never go full screen
	if (machine->debug_flags & DEBUG_FLAG_OSD_ENABLED)
		video_config.windowed = TRUE;

	// per-window options: extract the data
	get_resolution(machine->options(), WINOPTION_RESOLUTION0, &video_config.window[0], TRUE);
	get_resolution(machine->options(), WINOPTION_RESOLUTION1, &video_config.window[1], TRUE);
	get_resolution(machine->options(), WINOPTION_RESOLUTION2, &video_config.window[2], TRUE);
	get_resolution(machine->options(), WINOPTION_RESOLUTION3, &video_config.window[3], TRUE);

	// video options: extract the data
	stemp = options_get_string(&machine->options(), WINOPTION_VIDEO);
	if (strcmp(stemp, "d3d") == 0)
		video_config.mode = VIDEO_MODE_D3D;
	else if (strcmp(stemp, "ddraw") == 0)
		video_config.mode = VIDEO_MODE_DDRAW;
	else if (strcmp(stemp, "gdi") == 0)
		video_config.mode = VIDEO_MODE_GDI;
	else if (strcmp(stemp, "none") == 0)
	{
		video_config.mode = VIDEO_MODE_NONE;
		if (options_get_int(&machine->options(), OPTION_SECONDS_TO_RUN) == 0)
			mame_printf_warning(_WINDOWS("Warning: -video none doesn't make much sense without -seconds_to_run\n"));
	}
	else
	{
		mame_printf_warning(_WINDOWS("Invalid video value %s; reverting to gdi\n"), stemp);
		video_config.mode = VIDEO_MODE_GDI;
	}
	video_config.waitvsync     = options_get_bool(&machine->options(), WINOPTION_WAITVSYNC);
	video_config.syncrefresh   = options_get_bool(&machine->options(), WINOPTION_SYNCREFRESH);
	video_config.triplebuf     = options_get_bool(&machine->options(), WINOPTION_TRIPLEBUFFER);
	video_config.switchres     = options_get_bool(&machine->options(), WINOPTION_SWITCHRES);

	// ddraw options: extract the data
	video_config.hwstretch     = options_get_bool(&machine->options(), WINOPTION_HWSTRETCH);

	// d3d options: extract the data
	video_config.filter        = options_get_bool(&machine->options(), WINOPTION_FILTER);
	if (video_config.prescale == 0)
		video_config.prescale = 1;

	// misc options: sanity check values

	// per-window options: sanity check values

	// d3d options: sanity check values
	options_get_int(&machine->options(), WINOPTION_D3DVERSION);

	options_get_float(&machine->options(), WINOPTION_FULLSCREENBRIGHTNESS);
	options_get_float(&machine->options(), WINOPTION_FULLLSCREENCONTRAST);
	options_get_float(&machine->options(), WINOPTION_FULLSCREENGAMMA);

#ifdef MAME_AVI
	memset(&AviStatus, 0, sizeof(AviStatus));
	avi_filename = NULL;

	if (strlen(options_get_string(&machine->options(), "avi_avi_filename")) > 0)
	{
		avi_filename                        = astring_from_utf8(options_get_string(&machine->options(), "avi_avi_filename"));
		AviStatus.def_fps                   = options_get_float (&machine->options(), "avi_def_fps");
		AviStatus.fps                       = options_get_float (&machine->options(), "avi_fps");
		AviStatus.frame_skip                = options_get_int   (&machine->options(), "avi_frame_skip");
		AviStatus.frame_cmp                 = options_get_bool  (&machine->options(), "avi_frame_cmp");
		AviStatus.frame_cmp_pre15           = options_get_bool  (&machine->options(), "avi_frame_cmp_pre15");
		AviStatus.frame_cmp_few             = options_get_bool  (&machine->options(), "avi_frame_cmp_few");
		AviStatus.width                     = options_get_int   (&machine->options(), "avi_width");
		AviStatus.height                    = options_get_int   (&machine->options(), "avi_height");
		AviStatus.depth                     = options_get_int   (&machine->options(), "avi_depth");
		AviStatus.orientation               = options_get_int   (&machine->options(), "avi_orientation");
		AviStatus.rect.m_Top                = options_get_int   (&machine->options(), "avi_rect_top");
		AviStatus.rect.m_Left               = options_get_int   (&machine->options(), "avi_rect_left");
		AviStatus.rect.m_Width              = options_get_int   (&machine->options(), "avi_rect_width");
		AviStatus.rect.m_Height             = options_get_int   (&machine->options(), "avi_rect_height");
		AviStatus.interlace                 = options_get_bool  (&machine->options(), "avi_interlace");
		AviStatus.interlace_odd_number_field  = options_get_bool  (&machine->options(), "avi_interlace_odd_field");
		AviStatus.avi_filesize              = options_get_int   (&machine->options(), "avi_avi_filesize");
		AviStatus.avi_savefile_pause        = options_get_bool  (&machine->options(), "avi_avi_savefile_pause");
		AviStatus.avi_width                 = options_get_int   (&machine->options(), "avi_avi_width");
		AviStatus.avi_height                = options_get_int   (&machine->options(), "avi_avi_height");
		AviStatus.avi_depth                 = options_get_int   (&machine->options(), "avi_avi_depth");
		AviStatus.avi_rect.m_Top            = options_get_int   (&machine->options(), "avi_avi_rect_top");
		AviStatus.avi_rect.m_Left           = options_get_int   (&machine->options(), "avi_avi_rect_left");
		AviStatus.avi_rect.m_Width          = options_get_int   (&machine->options(), "avi_avi_rect_width");
		AviStatus.avi_rect.m_Height         = options_get_int   (&machine->options(), "avi_avi_rect_height");
		AviStatus.avi_smooth_resize_x       = options_get_bool  (&machine->options(), "avi_avi_smooth_resize_x");
		AviStatus.avi_smooth_resize_y       = options_get_bool  (&machine->options(), "avi_avi_smooth_resize_y");

		AviStatus.wav_filename              = (char *)options_get_string(&machine->options(), "avi_wav_filename");
		AviStatus.audio_type                = options_get_int   (&machine->options(), "avi_audio_type");
		AviStatus.audio_channel             = options_get_int   (&machine->options(), "avi_audio_channel");
		AviStatus.audio_samples_per_sec     = options_get_int   (&machine->options(), "avi_audio_samples_per_sec");
		AviStatus.audio_bitrate             = options_get_int   (&machine->options(), "avi_audio_bitrate");
		AviStatus.avi_audio_record_type     = options_get_int   (&machine->options(), "avi_audio_record_type");
		AviStatus.avi_audio_channel         = options_get_int   (&machine->options(), "avi_avi_audio_channel");
		AviStatus.avi_audio_samples_per_sec = options_get_int   (&machine->options(), "avi_avi_audio_samples_per_sec");
		AviStatus.avi_audio_bitrate         = options_get_int   (&machine->options(), "avi_avi_audio_bitrate");
		AviStatus.avi_audio_cmp             = options_get_bool  (&machine->options(), "avi_audio_cmp");
		
		AviStatus.hour                      = options_get_int   (&machine->options(), "avi_hour");
		AviStatus.minute                    = options_get_int   (&machine->options(), "avi_minute");
		AviStatus.second                    = options_get_int   (&machine->options(), "avi_second");
	}
#endif /* MAME_AVI */
}



//============================================================
//  get_aspect
//============================================================

static float get_aspect(core_options &options, const char *name, int report_error)
{
	const char *defdata = options_get_string(&options, WINOPTION_ASPECT);
	const char *data = options_get_string(&options, name);
	int num = 0, den = 1;

	if (strcmp(data, "auto") == 0)
	{
		if (strcmp(defdata, "auto") == 0)
			return 0;
		data = defdata;
	}
	if (sscanf(data, "%d:%d", &num, &den) != 2 && report_error)
		mame_printf_error(_WINDOWS("Illegal aspect ratio value for %s = %s\n"), name, data);
	return (float)num / (float)den;
}



//============================================================
//  get_resolution
//============================================================

static void get_resolution(core_options &options, const char *name, win_window_config *config, int report_error)
{
	const char *defdata = options_get_string(&options, WINOPTION_RESOLUTION);
	const char *data = options_get_string(&options, name);

	config->width = config->height = config->refresh = 0;
	if (strcmp(data, "auto") == 0)
	{
		if (strcmp(defdata, "auto") == 0)
			return;
		data = defdata;
	}
	if (sscanf(data, "%dx%d@%d", &config->width, &config->height, &config->refresh) < 2 && report_error)
		mame_printf_error(_WINDOWS("Illegal resolution value for %s = %s\n"), name, data);
}
