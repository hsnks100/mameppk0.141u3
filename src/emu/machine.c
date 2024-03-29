/***************************************************************************

    machine.c

    Controls execution of the core MAME system.

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

****************************************************************************

    Since there has been confusion in the past over the order of
    initialization and other such things, here it is, all spelled out
    as of January, 2008:

    main()
        - does platform-specific init
        - calls mame_execute() [mame.c]

        mame_execute() [mame.c]
            - calls mame_validitychecks() [validity.c] to perform validity checks on all compiled drivers
            - begins resource tracking (level 1)
            - calls create_machine [mame.c] to initialize the running_machine structure
            - calls init_machine() [mame.c]

            init_machine() [mame.c]
                - calls fileio_init() [fileio.c] to initialize file I/O info
                - calls config_init() [config.c] to initialize configuration system
                - calls input_init() [input.c] to initialize the input system
                - calls output_init() [output.c] to initialize the output system
                - calls state_init() [state.c] to initialize save state system
                - calls state_save_allow_registration() [state.c] to allow registrations
                - calls palette_init() [palette.c] to initialize palette system
                - calls render_init() [render.c] to initialize the rendering system
                - calls ui_init() [ui.c] to initialize the user interface
                - calls generic_machine_init() [machine/generic.c] to initialize generic machine structures
                - calls generic_video_init() [video/generic.c] to initialize generic video structures
                - calls generic_sound_init() [audio/generic.c] to initialize generic sound structures
                - calls timer_init() [timer.c] to reset the timer system
                - calls osd_init() [osdepend.h] to do platform-specific initialization
                - calls input_port_init() [inptport.c] to set up the input ports
                - calls rom_init() [romload.c] to load the game's ROMs
                - calls memory_init() [memory.c] to process the game's memory maps
                - calls watchdog_init() [watchdog.c] to initialize the watchdog system
                - calls the driver's DRIVER_INIT callback
                - calls device_list_start() [devintrf.c] to start any devices
                - calls video_init() [video.c] to start the video system
                - calls tilemap_init() [tilemap.c] to start the tilemap system
                - calls crosshair_init() [crsshair.c] to configure the crosshairs
                - calls sound_init() [sound.c] to start the audio system
                - calls debugger_init() [debugger.c] to set up the debugger
                - calls the driver's MACHINE_START, SOUND_START, and VIDEO_START callbacks
                - calls cheat_init() [cheat.c] to initialize the cheat system
                - calls image_init() [image.c] to initialize the image system

            - calls config_load_settings() [config.c] to load the configuration file
            - calls nvram_load [machine/generic.c] to load NVRAM
            - calls ui_display_startup_screens() [ui.c] to display the the startup screens
            - begins resource tracking (level 2)
            - calls soft_reset() [mame.c] to reset all systems

                -------------------( at this point, we're up and running )----------------------

            - calls scheduler->timeslice() [schedule.c] over and over until we exit
            - ends resource tracking (level 2), freeing all auto_mallocs and timers
            - calls the nvram_save() [machine/generic.c] to save NVRAM
            - calls config_save_settings() [config.c] to save the game's configuration
            - calls all registered exit routines [mame.c]
            - ends resource tracking (level 1), freeing all auto_mallocs and timers

        - exits the program

***************************************************************************/

#include "emu.h"
#include "emuopts.h"
#include "osdepend.h"
#include "config.h"
#include "debugger.h"
#include "image.h"
#include "profiler.h"
#include "render.h"
#include "cheat.h"
#include "ui.h"
#include "uimenu.h"
#include "uiinput.h"
#include "crsshair.h"
#include "validity.h"
#include "unzip.h"
#include "debug/debugcon.h"
#ifdef USE_HISCORE
#include "hiscore.h"
#endif /* USE_HISCORE */

#include <time.h>

#ifdef KAILLERA
#include "KailleraChat.h"
#include "ui_temp.h"
#include "extmem.h"
extern int kPlay;
char Trace_filename[_MAX_PATH];
#endif /* KAILLERA */



//**************************************************************************
//  GLOBAL VARIABLES
//**************************************************************************

// a giant string buffer for temporary strings
#ifdef KAILLERA
char giant_string_buffer[65536] = { 0 };
#else
static char giant_string_buffer[65536] = { 0 };
#endif /* KAILLERA */



//**************************************************************************
//  RUNNING MACHINE
//**************************************************************************

//-------------------------------------------------
//  running_machine - constructor
//-------------------------------------------------

running_machine::running_machine(const machine_config &_config, osd_interface &osd, core_options &options, bool exit_to_game_select)
	: m_regionlist(m_respool),
	  m_devicelist(m_respool),
	  config(&_config),
	  m_config(_config),
	  firstcpu(NULL),
	  gamedrv(&_config.gamedrv()),
	  m_game(_config.gamedrv()),
	  primary_screen(NULL),
	  palette(NULL),
	  pens(NULL),
	  colortable(NULL),
	  shadow_table(NULL),
	  priority_bitmap(NULL),
	  sample_rate(options_get_int(&options, OPTION_SAMPLERATE)),
	  debug_flags(0),
      ui_active(false),
	  memory_data(NULL),
	  palette_data(NULL),
	  tilemap_data(NULL),
	  romload_data(NULL),
	  input_data(NULL),
	  input_port_data(NULL),
	  ui_input_data(NULL),
	  debugcpu_data(NULL),
	  generic_machine_data(NULL),
	  generic_video_data(NULL),
	  generic_audio_data(NULL),
	  m_logerror_list(NULL),
	  m_state(*this),
	  m_scheduler(*this),
	  m_options(options),
	  m_osd(osd),
	  m_basename(_config.gamedrv().name),
	  m_current_phase(MACHINE_PHASE_PREINIT),
	  m_paused(false),
	  m_hard_reset_pending(false),
	  m_exit_pending(false),
	  m_exit_to_game_select(exit_to_game_select),
	  m_new_driver_pending(NULL),
	  m_soft_reset_timer(NULL),
	  m_logfile(NULL),
	  m_saveload_schedule(SLS_NONE),
	  m_saveload_schedule_time(attotime::zero),
	  m_saveload_searchpath(NULL),
	  m_rand_seed(0x9d14abd7),
	  m_driver_device(NULL),
	  m_cheat(NULL),
	  m_render(NULL),
	  m_sound(NULL),
	  m_video(NULL),
	  m_debug_view(NULL)
{
	memset(gfx, 0, sizeof(gfx));
	memset(&generic, 0, sizeof(generic));
	memset(m_notifier_list, 0, sizeof(m_notifier_list));
	memset(&m_base_time, 0, sizeof(m_base_time));

	// find the driver device config and tell it which game
	device_config *config = m_config.m_devicelist.find("root");
	if (config == NULL)
		throw emu_fatalerror("Machine configuration missing driver_device");

	// attach this machine to all the devices in the configuration
	m_devicelist.import_config_list(m_config.m_devicelist, *this);
	m_driver_device = device<driver_device>("root");
	assert(m_driver_device != NULL);

	// find devices
	primary_screen = downcast<screen_device *>(m_devicelist.first(SCREEN));
	for (device_t *device = m_devicelist.first(); device != NULL; device = device->next())
		if (dynamic_cast<cpu_device *>(device) != NULL)
		{
			firstcpu = downcast<cpu_device *>(device);
			break;
		}
#ifdef USE_HISCORE
		cpu[0] = firstcpu;
		for (int cpunum = 1; cpunum < ARRAY_LENGTH(cpu) && cpu[cpunum - 1] != NULL; cpunum++)
			cpu[cpunum] = cpu[cpunum - 1]->typenext();
#endif /* USE_HISCORE */

	// fetch core options
	if (options_get_bool(&m_options, OPTION_DEBUG))
		debug_flags = (DEBUG_FLAG_ENABLED | DEBUG_FLAG_CALL_HOOK) | (options_get_bool(&m_options, OPTION_DEBUG_INTERNAL) ? 0 : DEBUG_FLAG_OSD_ENABLED);
}


//-------------------------------------------------
//  ~running_machine - destructor
//-------------------------------------------------

running_machine::~running_machine()
{

#ifdef KAILLERA
	k_machine = NULL;
#endif /* KAILLERA */
}


//-------------------------------------------------
//  describe_context - return a string describing
//  which device is currently executing and its
//  PC
//-------------------------------------------------

const char *running_machine::describe_context()
{
	device_execute_interface *executing = m_scheduler.currently_executing();
	if (executing != NULL)
	{
		cpu_device *cpu = downcast<cpu_device *>(&executing->device());
		if (cpu != NULL)
			m_context.printf("'%s' (%s)", cpu->tag(), core_i64_hex_format(cpu->pc(), cpu->space(AS_PROGRAM)->logaddrchars()));
		else
			m_context.printf("'%s'", cpu->tag());
	}
	else
		m_context.cpy("(no context)");

	return m_context;
}


//-------------------------------------------------
//  start - initialize the emulated machine
//-------------------------------------------------

void running_machine::start()
{
	// initialize basic can't-fail systems here
	config_init(this);
	input_init(this);
	output_init(this);
	palette_init(this);
	m_render = auto_alloc(this, render_manager(*this));
	generic_machine_init(this);
	generic_sound_init(this);

#ifdef KAILLERA
	input_temp_Clear();
	{
		MemoryHackFunction.Init = MemoryHackDummy;
		MemoryHackFunction.StateLoad = MemoryHackDummy;
		MemoryHackFunction.Update = MemoryHackDummy;
	}
#endif /* KAILLERA */

	// allocate a soft_reset timer
	m_soft_reset_timer = m_scheduler.timer_alloc(MSTUB(timer_expired, running_machine, soft_reset), this);

	// init the osd layer
	m_osd.init(*this);

	// create the video manager
	m_video = auto_alloc(this, video_manager(*this));
	ui_init(this);

	// initialize the base time (needed for doing record/playback)
	::time(&m_base_time);

	// initialize the input system and input ports for the game
	// this must be done before memory_init in order to allow specifying
	// callbacks based on input port tags
	time_t newbase = input_port_init(this, m_game.ipt, m_config.m_devicelist);
	if (newbase != 0)
		m_base_time = newbase;

	// intialize UI input
	ui_input_init(this);

	// initialize the streams engine before the sound devices start
	m_sound = auto_alloc(this, sound_manager(*this));

	// first load ROMs, then populate memory, and finally initialize CPUs
	// these operations must proceed in this order
	rom_init(this);
	memory_init(this);
	watchdog_init(this);

	// must happen after memory_init because this relies on generic.spriteram
	generic_video_init(this);

	// allocate the gfx elements prior to device initialization
	gfx_init(this);

	// initialize natural keyboard support
	inputx_init(this);

	// initialize image devices
	image_init(this);
	tilemap_init(this);
	crosshair_init(this);

	// initialize the debugger
	if ((debug_flags & DEBUG_FLAG_ENABLED) != 0)
		debugger_init(this);

	// call the game driver's init function
	// this is where decryption is done and memory maps are altered
	// so this location in the init order is important
	ui_set_startup_text(this, _("Initializing..."), true);

	// start up the devices
	m_devicelist.start_all();

	// if we're coming in with a savegame request, process it now
	const char *savegame = options_get_string(&m_options, OPTION_STATE);
	if (savegame[0] != 0)
		schedule_load(savegame);

	// if we're in autosave mode, schedule a load
	else if (options_get_bool(&m_options, OPTION_AUTOSAVE) && (m_game.flags & GAME_SUPPORTS_SAVE) != 0)
		schedule_load("auto");

	// set up the cheat engine
	m_cheat = auto_alloc(this, cheat_manager(*this));

#ifdef USE_HISCORE
  //MKCHAMP - INITIALIZING THE HISCORE ENGINE
 	hiscore_init(this);
#endif /* USE_HISCORE */

	// disallow save state registrations starting here
	m_state.allow_registration(false);
}


//-------------------------------------------------
//  run - execute the machine
//-------------------------------------------------

#ifdef KAILLERA
int running_machine::run(running_machine &machine, bool firstrun)
#else /* KAILLERA */
int running_machine::run(bool firstrun)
#endif /* KAILLERA */
{
	int error = MAMERR_NONE;

	// use try/catch for deep error recovery
	try
	{
		// move to the init phase
		m_current_phase = MACHINE_PHASE_INIT;

		// if we have a logfile, set up the callback
		if (options_get_bool(&m_options, OPTION_LOG))
		{
			m_logfile = auto_alloc(this, emu_file(m_options, SEARCHPATH_DEBUGLOG, OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS));
			file_error filerr = m_logfile->open("error.log");
			assert_always(filerr == FILERR_NONE, "unable to open log file");
			add_logerror_callback(logfile_callback);
		}

		// then finish setting up our local machine
		start();

		// load the configuration settings and NVRAM
		bool settingsloaded = config_load_settings(this);
		nvram_load(this);
		sound().ui_mute(false);

		// display the startup screens
		ui_display_startup_screens(this, firstrun, !settingsloaded);

		// perform a soft reset -- this takes us to the running phase
		soft_reset(*this);

		// run the CPUs until a reset or exit
		m_hard_reset_pending = false;

#ifdef KAILLERA
		if (kPlay)
			KailleraChatInit(machine);
#endif /* KAILLERA */

		while ((!m_hard_reset_pending && !m_exit_pending) || m_saveload_schedule != SLS_NONE)
		{
			g_profiler.start(PROFILER_EXTRA);

			// execute CPUs if not paused
			if (!m_paused)
				m_scheduler.timeslice();

			// otherwise, just pump video updates through
			else
				m_video->frame_update();

			// handle save/load
			if (m_saveload_schedule != SLS_NONE)
				handle_saveload();

			g_profiler.stop();
		}

#ifdef KAILLERA
		if (kPlay)
			KailleraChatExit();
#endif /* KAILLERA */

		// and out via the exit phase
		m_current_phase = MACHINE_PHASE_EXIT;

		// save the NVRAM and configuration
		sound().ui_mute(true);
		nvram_save(this);
		// mamep: dont save settings during playback
		if (!has_playback_file(this))
		config_save_settings(this);
	}
	catch (emu_fatalerror &fatal)
	{
		mame_printf_error("%s\n", fatal.string());
		error = MAMERR_FATALERROR;
		if (fatal.exitcode() != 0)
			error = fatal.exitcode();
	}
	catch (emu_exception &)
	{
		mame_printf_error("Caught unhandled emulator exception\n");
		error = MAMERR_FATALERROR;
	}
	catch (std::bad_alloc &)
	{
		mame_printf_error("Out of memory!\n");
		error = MAMERR_FATALERROR;
	}

	// call all exit callbacks registered
	call_notifiers(MACHINE_NOTIFY_EXIT);
	zip_file_cache_clear();

	// close the logfile
	auto_free(this, m_logfile);
	return error;
}


//-------------------------------------------------
//  schedule_exit - schedule a clean exit
//-------------------------------------------------

void running_machine::schedule_exit()
{
	// if we are in-game but we started with the select game menu, return to that instead
	if (m_exit_to_game_select && options_get_string(&m_options, OPTION_GAMENAME)[0] != 0)
	{
		options_set_string(&m_options, OPTION_GAMENAME, "", OPTION_PRIORITY_CMDLINE);
		ui_menu_force_game_select(this, &render().ui_container());
	}

	// otherwise, exit for real
	else
		m_exit_pending = true;

	// if we're executing, abort out immediately
	m_scheduler.eat_all_cycles();

	// if we're autosaving on exit, schedule a save as well
	if (options_get_bool(&m_options, OPTION_AUTOSAVE) && (m_game.flags & GAME_SUPPORTS_SAVE) && this->time() > attotime::zero)
		schedule_save("auto");
}


//-------------------------------------------------
//  schedule_hard_reset - schedule a hard-reset of
//  the machine
//-------------------------------------------------

void running_machine::schedule_hard_reset()
{
	m_hard_reset_pending = true;

	// if we're executing, abort out immediately
	m_scheduler.eat_all_cycles();
}


//-------------------------------------------------
//  schedule_soft_reset - schedule a soft-reset of
//  the system
//-------------------------------------------------

void running_machine::schedule_soft_reset()
{
	m_soft_reset_timer->adjust(attotime::zero);

	// we can't be paused since the timer needs to fire
	resume();

	// if we're executing, abort out immediately
	m_scheduler.eat_all_cycles();
}


//-------------------------------------------------
//  schedule_new_driver - schedule a new game to
//  be loaded
//-------------------------------------------------

void running_machine::schedule_new_driver(const game_driver &driver)
{
	m_hard_reset_pending = true;
	m_new_driver_pending = &driver;

	// if we're executing, abort out immediately
	m_scheduler.eat_all_cycles();
}


//-------------------------------------------------
//  set_saveload_filename - specifies the filename
//  for state loading/saving
//-------------------------------------------------

void running_machine::set_saveload_filename(const char *filename)
{
	// free any existing request and allocate a copy of the requested name
	if (osd_is_absolute_path(filename))
	{
		m_saveload_searchpath = NULL;
		m_saveload_pending_file.cpy(filename);
	}
	else
	{
		m_saveload_searchpath = SEARCHPATH_STATE;
		m_saveload_pending_file.cpy(basename()).cat(PATH_SEPARATOR).cat(filename).cat(".sta");
	}
}


//-------------------------------------------------
//  schedule_save - schedule a save to occur as
//  soon as possible
//-------------------------------------------------

void running_machine::schedule_save(const char *filename)
{
	// specify the filename to save or load
	set_saveload_filename(filename);

	// note the start time and set a timer for the next timeslice to actually schedule it
	m_saveload_schedule = SLS_SAVE;
	m_saveload_schedule_time = this->time();

	// we can't be paused since we need to clear out anonymous timers
	resume();
}


//-------------------------------------------------
//  schedule_load - schedule a load to occur as
//  soon as possible
//-------------------------------------------------

void running_machine::schedule_load(const char *filename)
{
	// specify the filename to save or load
	set_saveload_filename(filename);

	// note the start time and set a timer for the next timeslice to actually schedule it
	m_saveload_schedule = SLS_LOAD;
	m_saveload_schedule_time = this->time();

	// we can't be paused since we need to clear out anonymous timers
	resume();
}


//-------------------------------------------------
//  pause - pause the system
//-------------------------------------------------

void running_machine::pause()
{
	// ignore if nothing has changed
	if (m_paused)
		return;
	m_paused = true;

	// call the callbacks
	call_notifiers(MACHINE_NOTIFY_PAUSE);
}


//-------------------------------------------------
//  resume - resume the system
//-------------------------------------------------

void running_machine::resume()
{
	// ignore if nothing has changed
	if (!m_paused)
		return;
	m_paused = false;

	// call the callbacks
	call_notifiers(MACHINE_NOTIFY_RESUME);
}


//-------------------------------------------------
//  region_alloc - allocates memory for a region
//-------------------------------------------------

memory_region *running_machine::region_alloc(const char *name, UINT32 length, UINT32 flags)
{
    // make sure we don't have a region of the same name; also find the end of the list
    memory_region *info = m_regionlist.find(name);
    if (info != NULL)
		fatalerror("region_alloc called with duplicate region name \"%s\"\n", name);

	// allocate the region
	return &m_regionlist.append(name, *auto_alloc(this, memory_region(*this, name, length, flags)));
}


//-------------------------------------------------
//  region_free - releases memory for a region
//-------------------------------------------------

void running_machine::region_free(const char *name)
{
	m_regionlist.remove(name);
}


//-------------------------------------------------
//  add_notifier - add a notifier of the
//  given type
//-------------------------------------------------

void running_machine::add_notifier(machine_notification event, notify_callback callback)
{
	assert_always(m_current_phase == MACHINE_PHASE_INIT, "Can only call add_notifier at init time!");

	// exit notifiers are added to the head, and executed in reverse order
	if (event == MACHINE_NOTIFY_EXIT)
	{
		notifier_callback_item *notifier = auto_alloc(this, notifier_callback_item(callback));
		notifier->m_next = m_notifier_list[event];
		m_notifier_list[event] = notifier;
	}

	// all other notifiers are added to the tail, and executed in the order registered
	else
	{
		notifier_callback_item **tailptr;
		for (tailptr = &m_notifier_list[event]; *tailptr != NULL; tailptr = &(*tailptr)->m_next) ;
		*tailptr = auto_alloc(this, notifier_callback_item(callback));
	}
}


//-------------------------------------------------
//  add_logerror_callback - adds a callback to be
//  called on logerror()
//-------------------------------------------------

void running_machine::add_logerror_callback(logerror_callback callback)
{
	assert_always(m_current_phase == MACHINE_PHASE_INIT, "Can only call add_logerror_callback at init time!");

	logerror_callback_item **tailptr;
	for (tailptr = &m_logerror_list; *tailptr != NULL; tailptr = &(*tailptr)->m_next) ;
	*tailptr = auto_alloc(this, logerror_callback_item(callback));
}


//-------------------------------------------------
//  logerror - printf-style error logging
//-------------------------------------------------

void CLIB_DECL running_machine::logerror(const char *format, ...)
{
	// process only if there is a target
	if (m_logerror_list != NULL)
	{
		va_list arg;
		va_start(arg, format);
		vlogerror(format, arg);
		va_end(arg);
	}
}


//-------------------------------------------------
//  vlogerror - vprintf-style error logging
//-------------------------------------------------

void CLIB_DECL running_machine::vlogerror(const char *format, va_list args)
{
	// process only if there is a target
	if (m_logerror_list != NULL)
	{
		g_profiler.start(PROFILER_LOGERROR);

		// dump to the buffer
		vsnprintf(giant_string_buffer, ARRAY_LENGTH(giant_string_buffer), format, args);

		// log to all callbacks
		for (logerror_callback_item *cb = m_logerror_list; cb != NULL; cb = cb->m_next)
			(*cb->m_func)(*this, giant_string_buffer);

		g_profiler.stop();
	}
}


//-------------------------------------------------
//  base_datetime - retrieve the time of the host
//  system; useful for RTC implementations
//-------------------------------------------------

void running_machine::base_datetime(system_time &systime)
{
	systime.set(m_base_time);
}


//-------------------------------------------------
//  current_datetime - retrieve the current time
//  (offset by the base); useful for RTC
//  implementations
//-------------------------------------------------

void running_machine::current_datetime(system_time &systime)
{
	systime.set(m_base_time + this->time().seconds);
}


//-------------------------------------------------
//  rand - standardized random numbers
//-------------------------------------------------

UINT32 running_machine::rand()
{
	m_rand_seed = 1664525 * m_rand_seed + 1013904223;

	// return rotated by 16 bits; the low bits have a short period
    // and are frequently used
	return (m_rand_seed >> 16) | (m_rand_seed << 16);
}


//-------------------------------------------------
//  call_notifiers - call notifiers of the given
//  type
//-------------------------------------------------

void running_machine::call_notifiers(machine_notification which)
{
	for (notifier_callback_item *cb = m_notifier_list[which]; cb != NULL; cb = cb->m_next)
		(*cb->m_func)(*this);
}


//-------------------------------------------------
//  handle_saveload - attempt to perform a save
//  or load
//-------------------------------------------------

void running_machine::handle_saveload()
{
	UINT32 openflags = (m_saveload_schedule == SLS_LOAD) ? OPEN_FLAG_READ : (OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS);
	const char *opnamed = (m_saveload_schedule == SLS_LOAD) ? _("loaded") : _("saved");
	const char *opname = (m_saveload_schedule == SLS_LOAD) ? _("load") : _("save");
	file_error filerr = FILERR_NONE;

	// if no name, bail
	emu_file file(m_options, m_saveload_searchpath, openflags);
	if (!m_saveload_pending_file)
		goto cancel;

	// if there are anonymous timers, we can't save just yet, and we can't load yet either
	// because the timers might overwrite data we have loaded
	if (m_scheduler.can_save())
	{
		// if more than a second has passed, we're probably screwed
		if ((this->time() - m_saveload_schedule_time) > attotime::from_seconds(1))
		{
			popmessage(_("Unable to %s due to pending anonymous timers. See error.log for details."), opname);
			goto cancel;
		}
		return;
	}

	// open the file
	filerr = file.open(m_saveload_pending_file);
	if (filerr == FILERR_NONE)
	{
		// read/write the save state
		state_save_error staterr = (m_saveload_schedule == SLS_LOAD) ? m_state.read_file(file) : m_state.write_file(file);

		// handle the result
		switch (staterr)
		{
			case STATERR_ILLEGAL_REGISTRATIONS:
				popmessage(_("Error: Unable to %s state due to illegal registrations. See error.log for details."), opname);
				break;

			case STATERR_INVALID_HEADER:
				popmessage(_("Error: Unable to %s state due to an invalid header. Make sure the save state is correct for this game."), opname);
				break;

			case STATERR_READ_ERROR:
				popmessage(_("Error: Unable to %s state due to a read error (file is likely corrupt)."), opname);
				break;

			case STATERR_WRITE_ERROR:
				popmessage(_("Error: Unable to %s state due to a write error. Verify there is enough disk space."), opname);
				break;

			case STATERR_NONE:
				if (!(m_game.flags & GAME_SUPPORTS_SAVE))
					popmessage(_("State successfully %s.\nWarning: Save states are not officially supported for this game."), opnamed);
				else
					popmessage(_("State successfully %s."), opnamed);
				break;

			default:
				popmessage(_("Error: Unknown error during state %s."), opnamed);
				break;
		}

		// close and perhaps delete the file
		if (staterr != STATERR_NONE && m_saveload_schedule == SLS_SAVE)
			file.remove_on_close();
	}
	else
		popmessage(_("Error: Failed to open file for %s operation."), opname);

	// unschedule the operation
cancel:
	m_saveload_pending_file.reset();
	m_saveload_searchpath = NULL;
	m_saveload_schedule = SLS_NONE;
}


//-------------------------------------------------
//  soft_reset - actually perform a soft-reset
//  of the system
//-------------------------------------------------

void running_machine::soft_reset(running_machine &machine, int param)
{
	logerror("Soft reset\n");

	// temporarily in the reset phase
	m_current_phase = MACHINE_PHASE_RESET;

	// call all registered reset callbacks
	call_notifiers(MACHINE_NOTIFY_RESET);

	// now we're running
	m_current_phase = MACHINE_PHASE_RUNNING;
}


//-------------------------------------------------
//  logfile_callback - callback for logging to
//  logfile
//-------------------------------------------------

void running_machine::logfile_callback(running_machine &machine, const char *buffer)
{
	if (machine.m_logfile != NULL)
		machine.m_logfile->puts(buffer);
}



/***************************************************************************
    MEMORY REGIONS
***************************************************************************/

//-------------------------------------------------
//  memory_region - constructor
//-------------------------------------------------

memory_region::memory_region(running_machine &machine, const char *name, UINT32 length, UINT32 flags)
	: m_machine(machine),
	  m_next(NULL),
	  m_name(name),
	  m_length(length),
	  m_flags(flags)
{
	m_base.u8 = auto_alloc_array(&machine, UINT8, length);
}


//-------------------------------------------------
//  ~memory_region - destructor
//-------------------------------------------------

memory_region::~memory_region()
{
	auto_free(&m_machine, m_base.v);
}



//**************************************************************************
//  CALLBACK ITEMS
//**************************************************************************

//-------------------------------------------------
//  notifier_callback_item - constructor
//-------------------------------------------------

running_machine::notifier_callback_item::notifier_callback_item(notify_callback func)
	: m_next(NULL),
	  m_func(func)
{
}


//-------------------------------------------------
//  logerror_callback_item - constructor
//-------------------------------------------------

running_machine::logerror_callback_item::logerror_callback_item(logerror_callback func)
	: m_next(NULL),
	  m_func(func)
{
}



//**************************************************************************
//  DRIVER DEVICE
//**************************************************************************

//-------------------------------------------------
//  driver_device_config_base - constructor
//-------------------------------------------------

driver_device_config_base::driver_device_config_base(const machine_config &mconfig, device_type type, const char *tag, const device_config *owner)
	: device_config(mconfig, type, "Driver Device", tag, owner, 0),
	  m_game(NULL),
	  m_palette_init(NULL)
{
	memset(m_callbacks, 0, sizeof(m_callbacks));
}


//-------------------------------------------------
//  static_set_game - set the game in the device
//  configuration
//-------------------------------------------------

void driver_device_config_base::static_set_game(device_config *device, const game_driver *game)
{
	downcast<driver_device_config_base *>(device)->m_game = game;
	downcast<driver_device_config_base *>(device)->m_shortname = game->name;
}


//-------------------------------------------------
//  static_set_machine_start - set the legacy
//  machine start callback in the device
//  configuration
//-------------------------------------------------

void driver_device_config_base::static_set_callback(device_config *device, callback_type type, legacy_callback_func callback)
{
	downcast<driver_device_config_base *>(device)->m_callbacks[type] = callback;
}


//-------------------------------------------------
//  static_set_palette_init - set the legacy
//  palette init callback in the device
//  configuration
//-------------------------------------------------

void driver_device_config_base::static_set_palette_init(device_config *device, palette_init_func callback)
{
	downcast<driver_device_config_base *>(device)->m_palette_init = callback;
}


//-------------------------------------------------
//  rom_region - return a pointer to the ROM
//  regions specified for the current game
//-------------------------------------------------

const rom_entry *driver_device_config_base::rom_region() const
{
	return m_game->rom;
}



//**************************************************************************
//  DRIVER DEVICE
//**************************************************************************

//-------------------------------------------------
//  driver_device - constructor
//-------------------------------------------------

driver_device::driver_device(running_machine &machine, const driver_device_config_base &config)
	: device_t(machine, config),
	  m_config(config)
{
}


//-------------------------------------------------
//  driver_device - destructor
//-------------------------------------------------

driver_device::~driver_device()
{
}


//-------------------------------------------------
//  driver_start - default implementation which
//  does nothing
//-------------------------------------------------

void driver_device::driver_start()
{
}


//-------------------------------------------------
//  machine_start - default implementation which
//  calls to the legacy machine_start function
//-------------------------------------------------

void driver_device::machine_start()
{
	if (m_config.m_callbacks[driver_device_config_base::CB_MACHINE_START] != NULL)
		(*m_config.m_callbacks[driver_device_config_base::CB_MACHINE_START])(&m_machine);
}


//-------------------------------------------------
//  sound_start - default implementation which
//  calls to the legacy sound_start function
//-------------------------------------------------

void driver_device::sound_start()
{
	if (m_config.m_callbacks[driver_device_config_base::CB_SOUND_START] != NULL)
		(*m_config.m_callbacks[driver_device_config_base::CB_SOUND_START])(&m_machine);
}


//-------------------------------------------------
//  video_start - default implementation which
//  calls to the legacy video_start function
//-------------------------------------------------

void driver_device::video_start()
{
	if (m_config.m_callbacks[driver_device_config_base::CB_VIDEO_START] != NULL)
		(*m_config.m_callbacks[driver_device_config_base::CB_VIDEO_START])(&m_machine);
}


//-------------------------------------------------
//  driver_reset - default implementation which
//  does nothing
//-------------------------------------------------

void driver_device::driver_reset()
{
}


//-------------------------------------------------
//  machine_reset - default implementation which
//  calls to the legacy machine_reset function
//-------------------------------------------------

void driver_device::machine_reset()
{
	if (m_config.m_callbacks[driver_device_config_base::CB_MACHINE_RESET] != NULL)
		(*m_config.m_callbacks[driver_device_config_base::CB_MACHINE_RESET])(&m_machine);
}


//-------------------------------------------------
//  sound_reset - default implementation which
//  calls to the legacy sound_reset function
//-------------------------------------------------

void driver_device::sound_reset()
{
	if (m_config.m_callbacks[driver_device_config_base::CB_SOUND_RESET] != NULL)
		(*m_config.m_callbacks[driver_device_config_base::CB_SOUND_RESET])(&m_machine);
}


//-------------------------------------------------
//  video_reset - default implementation which
//  calls to the legacy video_reset function
//-------------------------------------------------

void driver_device::video_reset()
{
	if (m_config.m_callbacks[driver_device_config_base::CB_VIDEO_RESET] != NULL)
		(*m_config.m_callbacks[driver_device_config_base::CB_VIDEO_RESET])(&m_machine);
}


//-------------------------------------------------
//  video_update - default implementation which
//  calls to the legacy video_update function
//-------------------------------------------------

bool driver_device::screen_update(screen_device &screen, bitmap_t &bitmap, const rectangle &cliprect)
{
	return 0;
}


//-------------------------------------------------
//  video_eof - default implementation which
//  calls to the legacy video_eof function
//-------------------------------------------------

void driver_device::screen_eof()
{
}


//-------------------------------------------------
//  device_start - device override which calls
//  the various helpers
//-------------------------------------------------

void driver_device::device_start()
{
	// reschedule ourselves to be last
	if (next() != NULL)
		throw device_missing_dependencies();

	// call the game-specific init
	if (m_config.m_game->driver_init != NULL)
		(*m_config.m_game->driver_init)(&m_machine);

	// finish image devices init process
	image_postdevice_init(&m_machine);

	// call palette_init if present
	if (m_config.m_palette_init != NULL)
		(*m_config.m_palette_init)(&m_machine, machine->region("proms")->base());

	// start the various pieces
	driver_start();
	machine_start();
	sound_start();
	video_start();
}


//-------------------------------------------------
//  device_reset - device override which calls
//  the various helpers
//-------------------------------------------------

void driver_device::device_reset()
{
	// reset each piece
	driver_reset();
	machine_reset();
	sound_reset();
	video_reset();
}



//**************************************************************************
//  SYSTEM TIME
//**************************************************************************

//-------------------------------------------------
//  system_time - constructor
//-------------------------------------------------

system_time::system_time()
{
	set(0);
}


//-------------------------------------------------
//  set - fills out a system_time structure
//-------------------------------------------------

void system_time::set(time_t t)
{
	time = t;
	local_time.set(*localtime(&t));
	utc_time.set(*gmtime(&t));
}


//-------------------------------------------------
//  get_tm_time - converts a tm struction to a
//  MAME mame_system_tm structure
//-------------------------------------------------

void system_time::full_time::set(struct tm &t)
{
	second	= t.tm_sec;
	minute	= t.tm_min;
	hour	= t.tm_hour;
	mday	= t.tm_mday;
	month	= t.tm_mon;
	year	= t.tm_year + 1900;
	weekday	= t.tm_wday;
	day		= t.tm_yday;
	is_dst	= t.tm_isdst;
}
