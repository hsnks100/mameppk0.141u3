/***************************************************************************

    clifront.c

    Command-line interface frontend for MAME.

    Copyright Nicola Salmoria and the MAME Team.
    Visit http://mamedev.org for licensing and usage restrictions.

***************************************************************************/

#include "emu.h"
#include "emuopts.h"
#include "hash.h"
#include "jedparse.h"
#include "audit.h"
#include "info.h"
#include "unzip.h"
#include "validity.h"
#include "sound/samples.h"
#include "clifront.h"
#include "xmlfile.h"

#include <new>
#include <ctype.h>
#ifdef MESS
#include "mess.h"
#endif

#ifdef KAILLERA
#include "ui_temp.h"
extern int kPlay;
#endif /* KAILLERA */



/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

typedef struct _romident_status romident_status;
struct _romident_status
{
	int			total;				/* total files processed */
	int			matches;			/* number of matches found */
	int			nonroms;			/* number of non-ROM files found */
};



/***************************************************************************
    FUNCTION PROTOTYPES
***************************************************************************/

static int execute_simple_commands(core_options *options, const char *exename);
static int execute_commands(core_options *options, const char *exename, const game_driver *driver);
static void display_help(void);

/* informational functions */
static int info_verifyroms(core_options *options, const char *gamename);
static int info_verifysamples(core_options *options, const char *gamename);
static int info_romident(core_options *options, const char *gamename);
static int info_listmedia(core_options *opts, const char *gamename);
static int info_listsoftware(core_options *options, const char *gamename);

/* utilities */
static void romident(core_options *options, const char *filename, romident_status *status);
static void identify_file(core_options *options, const char *name, romident_status *status);
static void identify_data(core_options *options, const char *name, const UINT8 *data, int length, romident_status *status);
static void match_roms(core_options *options, const hash_collection &hashes, int length, int *found);
static void display_suggestions(const char *gamename);
static void namecopy(char *name_ref, const char *desc);

//mamep: required for using -listxml to parse -driver_config
extern int parse_ini_file(core_options *options, const char *name, int priority);



/***************************************************************************
    COMMAND-LINE OPTIONS
***************************************************************************/

static const options_entry cli_options[] =
{
	/* core commands */
	{ NULL,                       NULL,       OPTION_HEADER,     "CORE COMMANDS" },
	{ "help;h;?",                 "0",        OPTION_COMMAND,    "show help message" },
	{ "validate;valid",           "0",        OPTION_COMMAND,    "perform driver validation on all game drivers" },

	/* configuration commands */
	{ NULL,                       NULL,       OPTION_HEADER,     "CONFIGURATION COMMANDS" },
	{ "createconfig;cc",          "0",        OPTION_COMMAND,    "create the default configuration file" },
	{ "showconfig;sc",            "0",        OPTION_COMMAND,    "display running parameters" },
	{ "showusage;su",             "0",        OPTION_COMMAND,    "show this help" },

	/* frontend commands */
	{ NULL,                       NULL,       OPTION_HEADER,     "FRONTEND COMMANDS" },
	{ "listxml;lx",               "0",        OPTION_COMMAND,    "all available info on driver in XML format" },
	{ "listfull;ll",              "0",        OPTION_COMMAND,    "short name, full name" },
	{ "listsource;ls",            "0",        OPTION_COMMAND,    "driver sourcefile" },
	{ "listclones;lc",            "0",        OPTION_COMMAND,    "show clones" },
	{ "listbrothers;lb",          "0",        OPTION_COMMAND,    "show \"brothers\", or other drivers from same sourcefile" },
	{ "listcrc",                  "0",        OPTION_COMMAND,    "CRC-32s" },
	{ "listroms",                 "0",        OPTION_COMMAND,    "list required roms for a driver" },
	{ "listsamples",              "0",        OPTION_COMMAND,    "list optional samples for a driver" },
	{ "verifyroms",               "0",        OPTION_COMMAND,    "report romsets that have problems" },
	{ "verifysamples",            "0",        OPTION_COMMAND,    "report samplesets that have problems" },
	{ "romident",                 "0",        OPTION_COMMAND,    "compare files with known MAME roms" },
	{ "listdevices;ld",           "0",        OPTION_COMMAND,    "list available devices" },
	{ "listmedia;lm",             "0",        OPTION_COMMAND,    "list available media for the system" },
	{ "listsoftware;lsoft",       "0",        OPTION_COMMAND,    "list known software for the system" },
	{ "listgames",                "0",        OPTION_COMMAND,    "year, manufacturer and full name" },		// for make tp_manufact.txt

	{ NULL }
};



/***************************************************************************
    CORE IMPLEMENTATION
***************************************************************************/

static void display_suggestions(const char *gamename)
{
	const game_driver *matches[10];
	int drvnum;

	/* get the top 10 approximate matches */
	driver_list_get_approx_matches(drivers, gamename, ARRAY_LENGTH(matches), matches);

	/* print them out */
	fprintf(stderr, _("\n\"%s\" approximately matches the following\n"
			"supported " GAMESNOUN " (best match first):\n\n"), gamename);
	for (drvnum = 0; drvnum < ARRAY_LENGTH(matches); drvnum++)
		if (matches[drvnum] != NULL)
			fprintf(stderr, "%-18s%s\n", matches[drvnum]->name, _LST(matches[drvnum]->description));
}
/*-------------------------------------------------
    cli_execute - execute a game via the standard
    command line interface
-------------------------------------------------*/

int cli_execute(int argc, char **argv, osd_interface &osd, const options_entry *osd_options)
{
	core_options *options = NULL;
	const char *gamename_option;
	const game_driver *driver;
	output_callback_func prevcb;
	void *prevparam;
	int result = MAMERR_FATALERROR;
	astring gamename;
	astring exename;

	try
	{
		/* initialize the options manager and add the CLI-specific options */
		options = mame_options_init(osd_options);
		options_add_entries(options, cli_options);

		setup_language(options);

		/* parse the command line first; if we fail here, we're screwed */
		if (options_parse_command_line(options, argc, argv, OPTION_PRIORITY_CMDLINE, FALSE))
		{
			result = MAMERR_INVALID_CONFIG;
			goto error;
		}

		//mamep: ignore error for options added by callback later
		mame_set_output_channel(OUTPUT_CHANNEL_ERROR, mame_null_output_callback, NULL, &prevcb, &prevparam);

		setup_language(options);

		/* parse the simple commmands before we go any further */
		core_filename_extract_base(&exename, argv[0], TRUE);
		result = execute_simple_commands(options, exename);
		if (result != -1)
			goto error;

		//mamep: required for using -listxml to parse -driver_config
		options_set_string(options, OPTION_INIPATH, ".", OPTION_PRIORITY_INI);
		parse_ini_file(options, CONFIGNAME, OPTION_PRIORITY_MAME_INI);

#ifdef DRIVER_SWITCH
		assign_drivers(options);
#endif /* DRIVER_SWITCH */

		//mamep: enable error; now we have all options we can use
		mame_set_output_channel(OUTPUT_CHANNEL_ERROR, prevcb, prevparam, NULL, NULL);

		//mamep: try command line again
		/* parse the command line again; if we fail here, we're screwed */
		if (options_parse_command_line(options, argc, argv, OPTION_PRIORITY_CMDLINE, TRUE))
		{
			result = MAMERR_INVALID_CONFIG;
			goto error;
		}

		//mamep: required for using -listxml to parse -driver_config
		options_set_string(options, OPTION_INIPATH, ".", OPTION_PRIORITY_INI);
		parse_ini_file(options, CONFIGNAME, OPTION_PRIORITY_MAME_INI);

		/* find out what game we might be referring to */
		gamename_option = options_get_string(options, OPTION_GAMENAME);
		core_filename_extract_base(&gamename, gamename_option, TRUE);
		driver = driver_get_name(gamename);

		/* execute any commands specified */
		result = execute_commands(options, exename, driver);
		if (result != -1)
			goto error;

		/* if we don't have a valid driver selected, offer some suggestions */
		if (strlen(gamename_option) > 0 && driver == NULL)
		{
			display_suggestions(gamename_option);
			/* exit with an error */
			result = MAMERR_NO_SUCH_GAME;
			goto error;
		}
		/* parse the command line first; if we fail here, we're screwed */
		if (options_parse_command_line(options, argc, argv, OPTION_PRIORITY_CMDLINE, TRUE))
		{
			result = MAMERR_INVALID_CONFIG;
			goto error;
		}


		/* run the game */
		result = mame_execute(osd, options);
	}
	catch (emu_fatalerror &fatal)
	{
		fprintf(stderr, "%s\n", fatal.string());
		if (fatal.exitcode() != 0)
			result = fatal.exitcode();
	}
	catch (emu_exception &)
	{
		fprintf(stderr, "Caught unhandled emulator exception\n");
	}
	catch (std::bad_alloc &)
	{
		fprintf(stderr, "Out of memory!\n");
	}
	catch (...)
	{
		fprintf(stderr, "Caught unhandled exception\n");
	}

error:
#ifdef DRIVER_SWITCH
#ifdef KAILLERA
	if (!kPlay)
#endif /* KAILLERA */
	global_free(drivers);
#endif /* DRIVER_SWITCH */ 

	/* free our options and exit */
	if (options != NULL)
		options_free(options);

	/* report any unfreed memory */
	dump_unfreed_mem();
	return result;
}


/*-------------------------------------------------
    help_output - output callback for printing
    requested help information
-------------------------------------------------*/

static void help_output(const char *s)
{
	mame_printf_info("%s", s);
}


/*-------------------------------------------------
    execute_simple_commands - execute basic
    commands that don't require any context
-------------------------------------------------*/

static int execute_simple_commands(core_options *options, const char *exename)
{
	/* help? */
	if (options_get_bool(options, CLIOPTION_HELP))
	{
		display_help();
		return MAMERR_NONE;
	}

	/* showusage? */
	if (options_get_bool(options, CLIOPTION_SHOWUSAGE))
	{
		setup_language(options);
		mame_printf_info(_("Usage: %s [%s] [options]\n\nOptions:\n"), exename, _(GAMENOUN));
		options_output_help(options, help_output);
		return MAMERR_NONE;
	}

	/* validate? */
	if (options_get_bool(options, CLIOPTION_VALIDATE))
	{
#ifdef DRIVER_SWITCH
		options_set_string(options, OPTION_DRIVER_CONFIG, "all", OPTION_PRIORITY_INI);
		assign_drivers(options);
#endif /* DRIVER_SWITCH */
		return mame_validitychecks(*options, NULL);
	}

	return -1;
}


/*-------------------------------------------------
    execute_commands - execute various frontend
    commands
-------------------------------------------------*/

static int execute_commands(core_options *options, const char *exename, const game_driver *driver)
{
	static const struct
	{
		const char *option;
		int (*function)(core_options *options, const char *gamename);
	} info_commands[] =
	{
		{ CLIOPTION_LISTXML,		cli_info_listxml },
		{ CLIOPTION_LISTFULL,		cli_info_listfull },
		{ CLIOPTION_LISTSOURCE,		cli_info_listsource },
		{ CLIOPTION_LISTCLONES,		cli_info_listclones },
		{ CLIOPTION_LISTBROTHERS,	cli_info_listbrothers },
		{ CLIOPTION_LISTCRC,		cli_info_listcrc },
		{ CLIOPTION_LISTDEVICES,	cli_info_listdevices },
		{ CLIOPTION_LISTROMS,		cli_info_listroms },
		{ CLIOPTION_LISTSAMPLES,	cli_info_listsamples },
		{ CLIOPTION_VERIFYROMS,		info_verifyroms },
		{ CLIOPTION_VERIFYSAMPLES,	info_verifysamples },
		{ CLIOPTION_LISTMEDIA,		info_listmedia },
		{ CLIOPTION_LISTSOFTWARE,	info_listsoftware },
		{ CLIOPTION_ROMIDENT,		info_romident }
		,{ CLIOPTION_LISTGAMES,		cli_info_listgames }		// for make tp_manufact.txt
	};
	int i;

	/* createconfig? */
	if (options_get_bool(options, CLIOPTION_CREATECONFIG))
	{
		emu_file file(*options, NULL, OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS);

		/* parse any relevant INI files before proceeding */
		mame_parse_ini_files(options, driver);

		/* make the output filename */
		file_error filerr = file.open(CONFIGNAME ".ini");

		/* error if unable to create the file */
		if (filerr != FILERR_NONE)
		{
			mame_printf_info(_("Unable to create file " CONFIGNAME ".ini\n"));
			return MAMERR_FATALERROR;
		}

		/* output the configuration and exit cleanly */
		options_output_ini_file(options, file);
		return MAMERR_NONE;
	}

	/* showconfig? */
	if (options_get_bool(options, CLIOPTION_SHOWCONFIG))
	{
		/* parse any relevant INI files before proceeding */
		mame_parse_ini_files(options, driver);
		options_output_ini_stdfile(options, stdout);
		return MAMERR_NONE;
	}

	/* informational commands? */
	for (i = 0; i < ARRAY_LENGTH(info_commands); i++)
		if (options_get_bool(options, info_commands[i].option))
		{
			const char *gamename = options_get_string(options, OPTION_GAMENAME);
			/* parse any relevant INI files before proceeding */
			mame_parse_ini_files(options, driver);
			int retVal = (*info_commands[i].function)(options, (gamename[0] == 0) ? "*" : gamename);
			if ( retVal == MAMERR_NO_SUCH_GAME) {
				display_suggestions(gamename);
				return MAMERR_NO_SUCH_GAME;
			}
			return retVal;
		}

	return -1;
}


/*-------------------------------------------------
    display_help - display help to standard
    output
-------------------------------------------------*/

static void display_help(void)
{
#ifndef MESS
	mame_printf_info(_("M.A.M.E. v%s - Multiple Arcade Machine Emulator\n"
		   "Copyright Nicola Salmoria and the MAME Team\n\n"), build_version);
	mame_printf_info("%s\n", _(mame_disclaimer));
	mame_printf_info(_("Usage:  MAME gamename [options]\n\n"
		   "        MAME -showusage    for a brief list of options\n"
		   "        MAME -showconfig   for a list of configuration options\n"
		   "        MAME -createconfig to create a " CONFIGNAME ".ini\n\n"
		   "For usage instructions, please consult the file windows.txt\n"));
#else
	mess_display_help();
#endif
}



#ifdef DRIVER_SWITCH
void assign_drivers(core_options *options)
{
	static const struct
	{
		const char *name;
		const game_driver * const *driver;
	} drivers_table[] =
	{
		{ "mame",	mamedrivers },
#ifndef TINY_BUILD
		{ "plus",	plusdrivers },
		{ "homebrew",	homebrewdrivers },
		{ "decrypted",	decrypteddrivers },
#ifdef MAMEMESS
		{ "console",	consoledrivers },
#endif /* MAMEMESS */
#endif /* !TINY_BUILD */
		{ NULL }
	};

	UINT32 enabled = 0;
	int i, n;

#ifndef TINY_BUILD
	const char *drv_option = options_get_string(options, OPTION_DRIVER_CONFIG);
	if (drv_option)
	{
		char *temp = mame_strdup(drv_option);
		if (temp)
		{
			char *p = strtok(temp, ",");
 			while (p)
			{
				char *s = core_strtrim(p);	//get individual driver name
				if (s[0])
				{
					if (mame_stricmp(s, "all") == 0)
					{
						enabled = (UINT32)-1;
						break;
					}

					for (i = 0; drivers_table[i].name; i++)
						if (mame_stricmp(s, drivers_table[i].name) == 0)
						{
							enabled |= 1 << i;
							break;
						}

					if (!drivers_table[i].name)
						mame_printf_warning(_("Illegal value for %s = %s\n"), OPTION_DRIVER_CONFIG, s);
				}
				osd_free(s);
 				p = strtok(NULL, ",");
			}
 			osd_free(temp);
		}
	}
#endif /* !TINY_BUILD */

	if (enabled == 0)
		enabled = 1;	// default to mamedrivers

	n = 0;
	for (i = 0; drivers_table[i].name; i++)
		if (enabled & (1 << i))
		{
			int c;

			for (c = 0; drivers_table[i].driver[c]; c++)
				n++;
		}

	if (drivers)
		global_free(drivers);
	drivers = global_alloc_array(const game_driver *, (n + 1) * sizeof (game_driver*));

	n = 0;
	for (i = 0; drivers_table[i].name; i++)
		if (enabled & (1 << i))
		{
			int c;

			for (c = 0; drivers_table[i].driver[c]; c++)
				drivers[n++] = drivers_table[i].driver[c];
		}

	drivers[n] = NULL;

#ifdef OPTION_ADDED_DEVICE_OPTIONS
	options_set_bool(options, OPTION_ADDED_DEVICE_OPTIONS, FALSE, OPTION_PRIORITY_DEFAULT);

	//add options by callback if we need
	if (!options_get_bool(options, OPTION_ADDED_DEVICE_OPTIONS))
	{
		const char *gamename = options_get_string(options, OPTION_GAMENAME);
		if (gamename)
		{
			const char *argv[2];

			argv[0] = gamename;
			argv[1] = NULL;

			options_parse_command_line(options, ARRAY_LENGTH(argv) - 1, (char **)argv, OPTION_PRIORITY_CMDLINE, FALSE);
		}
	}
#endif /* OPTION_ADDED_DEVICE_OPTIONS */
}
#endif /* DRIVER_SWITCH */



/***************************************************************************
    setup_language
***************************************************************************/

void setup_language(core_options *options)
{
	const char *langname = options_get_string(options, OPTION_LANGUAGE);
	int use_lang_list =options_get_bool(options, OPTION_USE_LANG_LIST);

	int langcode = mame_stricmp(langname, "auto") ?
		lang_find_langname(langname) :
		lang_find_codepage(osd_get_default_codepage());

	if (langcode < 0)
	{
		langcode = UI_LANG_EN_US;
		lang_set_langcode(options, langcode);
		set_osdcore_acp(ui_lang_info[langcode].codepage);

		if (mame_stricmp(langname, "auto"))
			mame_printf_warning("Invalid language value %s; reverting to %s\n",
		                langname, ui_lang_info[langcode].description);
	}

	lang_set_langcode(options, langcode);
	set_osdcore_acp(ui_lang_info[langcode].codepage);

	lang_message_enable(UI_MSG_LIST, use_lang_list);
	lang_message_enable(UI_MSG_MANUFACTURE, use_lang_list);
}



/***************************************************************************
    INFORMATIONAL FUNCTIONS
***************************************************************************/

/*-------------------------------------------------
    cli_info_listxml - output the XML data for one
    or more games
-------------------------------------------------*/

int cli_info_listxml(core_options *options, const char *gamename)
{
	print_mame_xml(stdout, drivers, gamename);
	return MAMERR_NONE;
}


/*-------------------------------------------------
    cli_info_listfull - output the name and description
    of one or more games
-------------------------------------------------*/

int cli_info_listfull(core_options *options, const char *gamename)
{
	int drvindex, count = 0;

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
		if ((drivers[drvindex]->flags & GAME_NO_STANDALONE) == 0 && mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			char name[200];

			/* print the header on the first one */
			if (count == 0)
				mame_printf_info(_("Name:             Description:\n"));
			count++;

			/* output the remaining information */
			mame_printf_info("%-18s", drivers[drvindex]->name);

			if (lang_message_is_enabled(UI_MSG_LIST))
			{
				strcpy(name, _LST(drivers[drvindex]->description));

				mame_printf_info("\"%s\"\n", name);

				continue;
			}

			namecopy(name,drivers[drvindex]->description);

			mame_printf_info("\"%s",name);

			/* print the additional description only if we are listing clones */
			{
				const char *pdest = strstr(drivers[drvindex]->description, " (");

				if (pdest != NULL && pdest > drivers[drvindex]->description)
					mame_printf_info("%s", pdest);
			}

			mame_printf_info("\"\n");
		}

	/* return an error if none found */
	return (count > 0) ? MAMERR_NONE : MAMERR_NO_SUCH_GAME;
}


/*-------------------------------------------------
    cli_info_listsource - output the name and source
    filename of one or more games
-------------------------------------------------*/

int cli_info_listsource(core_options *options, const char *gamename)
{
	int drvindex, count = 0;
	astring filename;

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
		if (mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			/* output the remaining information */
			mame_printf_info("%-16s %s\n", drivers[drvindex]->name, core_filename_extract_base(&filename, drivers[drvindex]->source_file, FALSE)->cstr());
			count++;
		}

	/* return an error if none found */
	return (count > 0) ? MAMERR_NONE : MAMERR_NO_SUCH_GAME;
}


/*-------------------------------------------------
    cli_info_listclones - output the name and source
    filename of one or more games
-------------------------------------------------*/

int cli_info_listclones(core_options *options, const char *gamename)
{
	int drvindex, count = 0, drvcnt = 0;

	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
	{
		if (mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			drvcnt++;
		}
	}
	if (drvcnt==0) return MAMERR_NO_SUCH_GAME;
	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
	{
		const game_driver *clone_of = driver_get_clone(drivers[drvindex]);

		/* if we are a clone, and either our name matches the gamename, or the clone's name matches, display us */
		if (clone_of != NULL && (clone_of->flags & GAME_IS_BIOS_ROOT) == 0)
			if (mame_strwildcmp(gamename, drivers[drvindex]->name) == 0 || mame_strwildcmp(gamename, clone_of->name) == 0)
			{
				/* print the header on the first one */
				if (count == 0)
					mame_printf_info(_("Name:            Clone of:\n"));

				/* output the remaining information */
				mame_printf_info("%-16s %-8s\n", drivers[drvindex]->name, clone_of->name);
				count++;
			}
	}

	return MAMERR_NONE;
}


/*-------------------------------------------------
    cli_info_listbrothers - output the name and
    source filename of one or more games
-------------------------------------------------*/

int cli_info_listbrothers(core_options *options, const char *gamename)
{
	UINT8 *didit = global_alloc_array_clear(UINT8, driver_list_get_count(drivers));
	int drvindex, count = 0;
	astring filename;

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
		if (!didit[drvindex] && mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			int matchindex;

			didit[drvindex] = TRUE;
			if (count > 0)
				mame_printf_info("\n");
			mame_printf_info(_("%s ... other drivers in %s:\n"), drivers[drvindex]->name, core_filename_extract_base(&filename, drivers[drvindex]->source_file, FALSE)->cstr());

			/* now iterate again over drivers, finding those with the same source file */
			for (matchindex = 0; drivers[matchindex]; matchindex++)
				if (matchindex != drvindex && strcmp(drivers[drvindex]->source_file, drivers[matchindex]->source_file) == 0)
				{
					const char *matchstring = (mame_strwildcmp(gamename, drivers[matchindex]->name) == 0) ? "-> " : "   ";
					const game_driver *clone_of = driver_get_clone(drivers[matchindex]);

					if (clone_of != NULL && (clone_of->flags & GAME_IS_BIOS_ROOT) == 0)
						mame_printf_info("%s%-16s [%s]\n", matchstring, drivers[matchindex]->name, clone_of->name);
					else
						mame_printf_info("%s%s\n", matchstring, drivers[matchindex]->name);
					didit[matchindex] = TRUE;
				}

			count++;
		}

	/* return an error if none found */
	global_free(didit);
	return (count > 0) ? MAMERR_NONE : MAMERR_NO_SUCH_GAME;
}


/*-------------------------------------------------
    cli_info_listcrc - output the CRC and name of
    all ROMs referenced by MAME
-------------------------------------------------*/

int cli_info_listcrc(core_options *options, const char *gamename)
{
	int drvindex, count = 0;

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
		if (mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			machine_config config(*drivers[drvindex]);
			const rom_entry *region, *rom;
			const rom_source *source;

			/* iterate over sources, regions, and then ROMs within the region */
			for (source = rom_first_source(config); source != NULL; source = rom_next_source(*source))
				for (region = rom_first_region(*source); region; region = rom_next_region(region))
					for (rom = rom_first_file(region); rom; rom = rom_next_file(rom))
					{
						/* if we have a CRC, display it */
						UINT32 crc;
						if (hash_collection(ROM_GETHASHDATA(rom)).crc(crc))
							mame_printf_info("%08x %-12s %s\n", crc, ROM_GETNAME(rom), _LST(drivers[drvindex]->description));
					}

			count++;
		}

	/* return an error if none found */
	return (count > 0) ? MAMERR_NONE : MAMERR_NO_SUCH_GAME;
}


/*-------------------------------------------------
    cli_info_listroms - output the list of ROMs
    referenced by a given game or set of games
-------------------------------------------------*/

int cli_info_listroms(core_options *options, const char *gamename)
{
	int drvindex, count = 0;
	astring tempstr;

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
		if (mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			machine_config config(*drivers[drvindex]);

			/* print the header */
			if (count > 0)
				mame_printf_info("\n");
			mame_printf_info(_("This is the list of the ROMs required for driver \"%s\".\n"
					"Name            Size Checksum\n"), drivers[drvindex]->name);

			/* iterate over sources, regions and then ROMs within the region */
			for (const rom_source *source = rom_first_source(config); source != NULL; source = rom_next_source(*source))
				for (const rom_entry *region = rom_first_region(*source); region != NULL; region = rom_next_region(region))
					for (const rom_entry *rom = rom_first_file(region); rom != NULL; rom = rom_next_file(rom))
					{
						/* accumulate the total length of all chunks */
						int length = -1;
						if (ROMREGION_ISROMDATA(region))
							length = rom_file_size(rom);

						/* start with the name */
						const char *name = ROM_GETNAME(rom);
						mame_printf_info("%-12s ", name);

						/* output the length next */
						if (length >= 0)
							mame_printf_info("%7d", length);
						else
							mame_printf_info("       ");

						/* output the hash data */
						hash_collection hashes(ROM_GETHASHDATA(rom));
						if (!hashes.flag(hash_collection::FLAG_NO_DUMP))
						{
							if (hashes.flag(hash_collection::FLAG_BAD_DUMP))
								mame_printf_info(_(" BAD"));
							mame_printf_info(" %s", hashes.macro_string(tempstr));
						}
						else
							mame_printf_info(_(" NO GOOD DUMP KNOWN"));

						/* end with a CR */
						mame_printf_info("\n");
					}

			count++;
		}

	return (count > 0) ? MAMERR_NONE : MAMERR_NO_SUCH_GAME;
}


/*-------------------------------------------------
    cli_info_listsamples - output the list of samples
    referenced by a given game or set of games
-------------------------------------------------*/

int cli_info_listsamples(core_options *options, const char *gamename)
{
	int count = 0;
	int drvindex;

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
		if (mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			machine_config config(*drivers[drvindex]);
			const device_config_sound_interface *sound = NULL;

			/* find samples interfaces */
			for (bool gotone = config.m_devicelist.first(sound); gotone; gotone = sound->next(sound))
				if (sound->devconfig().type() == SAMPLES)
				{
					const char *const *samplenames = ((const samples_interface *)sound->devconfig().static_config())->samplenames;
					int sampnum;

					/* if the list is legit, walk it and print the sample info */
					if (samplenames != NULL)
						for (sampnum = 0; samplenames[sampnum] != NULL; sampnum++)
							mame_printf_info("%s\n", samplenames[sampnum]);
				}

			count++;
		}

	return (count > 0) ? MAMERR_NONE : MAMERR_NO_SUCH_GAME;
}


/*-------------------------------------------------
    cli_info_listdevices - output the list of
    devices referenced by a given game or set of
    games
-------------------------------------------------*/

int cli_info_listdevices(core_options *options, const char *gamename)
{
	int count = 0;
	int drvindex;

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
		if (mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			machine_config config(*drivers[drvindex]);
			const device_config *devconfig;

			if (count != 0)
				printf("\n");
			mame_printf_info(_("Driver %s (%s):\n"), drivers[drvindex]->name, _LST(drivers[drvindex]->description));

			/* iterate through devices */
			for (devconfig = config.m_devicelist.first(); devconfig != NULL; devconfig = devconfig->next())
			{
				printf("   %s ('%s')", devconfig->name(), devconfig->tag());

				UINT32 clock = devconfig->clock();
				if (clock >= 1000000000)
					printf(" @ %d.%02d GHz\n", clock / 1000000000, (clock / 10000000) % 100);
				else if (clock >= 1000000)
					printf(" @ %d.%02d MHz\n", clock / 1000000, (clock / 10000) % 100);
				else if (clock >= 1000)
					printf(" @ %d.%02d kHz\n", clock / 1000, (clock / 10) % 100);
				else if (clock > 0)
					printf(" @ %d Hz\n", clock);
				else
					printf("\n");
			}

			count++;
		}

	return (count > 0) ? MAMERR_NONE : MAMERR_NO_SUCH_GAME;
}


/*-------------------------------------------------
    info_verifyroms - verify the ROM sets of
    one or more games
-------------------------------------------------*/

static int info_verifyroms(core_options *options, const char *gamename)
{
	int correct = 0;
	int incorrect = 0;
	int notfound = 0;
	int drvindex;

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
		if (mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			audit_record *audit;
			int audit_records;
			int res;

			/* audit the ROMs in this set */
			audit_records = audit_images(options, drivers[drvindex], AUDIT_VALIDATE_FAST, &audit);
			res = audit_summary(drivers[drvindex], audit_records, audit, TRUE);
			if (audit_records > 0)
				global_free(audit);

			/* if not found, count that and leave it at that */
			if (res == NOTFOUND)
				notfound++;

			/* else display information about what we discovered */
			else
			{
				const game_driver *clone_of;

				/* output the name of the driver and its clone */
				mame_printf_info(_("romset %s "), drivers[drvindex]->name);
				clone_of = driver_get_clone(drivers[drvindex]);
				if (clone_of != NULL)
					mame_printf_info("[%s] ", clone_of->name);

				/* switch off of the result */
				switch (res)
				{
					case INCORRECT:
						mame_printf_info(_("is bad\n"));
						incorrect++;
						break;

					case CORRECT:
						mame_printf_info(_("is good\n"));
						correct++;
						break;

					case BEST_AVAILABLE:
						mame_printf_info(_("is best available\n"));
						correct++;
						break;
				}
			}
		}

	/* clear out any cached files */
	zip_file_cache_clear();

	/* if we didn't get anything at all, display a generic end message */
	if (correct + incorrect == 0)
	{
		if (notfound > 0)
			mame_printf_info(_("romset \"%s\" not found!\n"), gamename);
		else
			mame_printf_info(_("romset \"%s\" not supported!\n"), gamename);
		return MAMERR_NO_SUCH_GAME;
	}

	/* otherwise, print a summary */
	else
	{
		mame_printf_info(_("%d romsets found, %d were OK.\n"), correct + incorrect, correct);
		return (incorrect > 0) ? MAMERR_MISSING_FILES : MAMERR_NONE;
	}
}

/*-------------------------------------------------
    info_listsoftware - output the list of
    software supported by a given game or set of
    games
    TODO: Add all information read from the source files
    Possible improvement: use a sorted list for
        identifying duplicate lists.
-------------------------------------------------*/

static int info_listsoftware(core_options *options, const char *gamename)
{
	FILE *out = stdout;
	int nr_lists = 0;
	char ** lists = NULL;
	int list_idx = 0;

	/* First determine the maximum number of lists we might encounter */
	for ( int drvindex = 0; drivers[drvindex] != NULL; drvindex++ )
	{
		if ( mame_strwildcmp( gamename, drivers[drvindex]->name ) == 0 )
		{
			/* allocate the machine config */
			machine_config config(*drivers[drvindex]);

			for (const device_config *dev = config.m_devicelist.first(SOFTWARE_LIST); dev != NULL; dev = dev->typenext())
			{
				software_list_config *swlist = (software_list_config *)downcast<const legacy_device_config_base *>(dev)->inline_config();

				for ( int i = 0; i < DEVINFO_STR_SWLIST_MAX - DEVINFO_STR_SWLIST_0; i++ )
				{
					if ( swlist->list_name[i] && *swlist->list_name[i]  && (swlist->list_type == SOFTWARE_LIST_ORIGINAL_SYSTEM))
						nr_lists++;
				}
			}
		}
	}

	lists = global_alloc_array( char *, nr_lists );

	if (nr_lists)
	{
		fprintf( out,
				"<?xml version=\"1.0\"?>\n"
				"<!DOCTYPE softwarelist [\n"
				"<!ELEMENT softwarelists (softwarelist*)>\n"
				"\t<!ELEMENT softwarelist (software+)>\n"
				"\t\t<!ATTLIST softwarelist name CDATA #REQUIRED>\n"
				"\t\t<!ATTLIST softwarelist description CDATA #IMPLIED>\n"
				"\t\t<!ELEMENT software (description, year?, publisher, part*)>\n"
				"\t\t\t<!ATTLIST software name CDATA #REQUIRED>\n"
				"\t\t\t<!ATTLIST software cloneof CDATA #IMPLIED>\n"
				"\t\t\t<!ATTLIST software supported (yes|partial|no) \"yes\">\n"
				"\t\t\t<!ELEMENT description (#PCDATA)>\n"
				"\t\t\t<!ELEMENT year (#PCDATA)>\n"
				"\t\t\t<!ELEMENT publisher (#PCDATA)>\n"
				"\t\t\t<!ELEMENT part (feature*, dataarea*, diskarea*, dipswitch*)>\n"
				"\t\t\t\t<!ATTLIST part name CDATA #REQUIRED>\n"
				"\t\t\t\t<!ATTLIST part interface CDATA #REQUIRED>\n"
				"\t\t\t\t<!ELEMENT feature EMPTY>\n"
				"\t\t\t\t\t<!ATTLIST feature name CDATA #REQUIRED>\n"
				"\t\t\t\t\t<!ATTLIST feature value CDATA #IMPLIED>\n"
				"\t\t\t\t<!ELEMENT dataarea (rom*)>\n"
				"\t\t\t\t\t<!ATTLIST dataarea name CDATA #REQUIRED>\n"
				"\t\t\t\t\t<!ATTLIST dataarea size CDATA #REQUIRED>\n"
				"\t\t\t\t\t<!ATTLIST dataarea databits (8|16|32|64) \"8\">\n"
				"\t\t\t\t\t<!ATTLIST dataarea endian (big|little) \"little\">\n"
				"\t\t\t\t\t<!ELEMENT rom EMPTY>\n"
				"\t\t\t\t\t\t<!ATTLIST rom name CDATA #IMPLIED>\n"
				"\t\t\t\t\t\t<!ATTLIST rom size CDATA #REQUIRED>\n"
				"\t\t\t\t\t\t<!ATTLIST rom crc CDATA #IMPLIED>\n"
				"\t\t\t\t\t\t<!ATTLIST rom md5 CDATA #IMPLIED>\n"
				"\t\t\t\t\t\t<!ATTLIST rom sha1 CDATA #IMPLIED>\n"
				"\t\t\t\t\t\t<!ATTLIST rom offset CDATA #IMPLIED>\n"
				"\t\t\t\t\t\t<!ATTLIST rom value CDATA #IMPLIED>\n"
				"\t\t\t\t\t\t<!ATTLIST rom status (baddump|nodump|good) \"good\">\n"
				"\t\t\t\t\t\t<!ATTLIST rom loadflag (load16_byte|load16_word|load16_word_swap|load32_byte|load32_word|load32_word_swap|load32_dword|load64_word|load64_word_swap|reload|fill|continue) #IMPLIED>\n"
				"\t\t\t\t<!ELEMENT diskarea (disk*)>\n"
				"\t\t\t\t\t<!ATTLIST diskarea name CDATA #REQUIRED>\n"
				"\t\t\t\t\t<!ELEMENT disk EMPTY>\n"
				"\t\t\t\t\t\t<!ATTLIST disk name CDATA #REQUIRED>\n"
				"\t\t\t\t\t\t<!ATTLIST disk md5 CDATA #IMPLIED>\n"
				"\t\t\t\t\t\t<!ATTLIST disk sha1 CDATA #IMPLIED>\n"
				"\t\t\t\t\t\t<!ATTLIST disk status (baddump|nodump|good) \"good\">\n"
				"\t\t\t\t\t\t<!ATTLIST disk writeable (yes|no) \"no\">\n"
				// we still do not store the dipswitch values in softlist, so there is no output here
				// TODO: add parsing dipsw in softlist.c and then add output here!
				"\t\t\t\t<!ELEMENT dipswitch (dipvalue*)>\n"
				"\t\t\t\t\t<!ATTLIST dipswitch name CDATA #REQUIRED>\n"
				"\t\t\t\t\t<!ATTLIST dipswitch tag CDATA #REQUIRED>\n"
				"\t\t\t\t\t<!ATTLIST dipswitch mask CDATA #REQUIRED>\n"
				"\t\t\t\t\t<!ELEMENT dipvalue EMPTY>\n"
				"\t\t\t\t\t\t<!ATTLIST dipvalue name CDATA #REQUIRED>\n"
				"\t\t\t\t\t\t<!ATTLIST dipvalue value CDATA #REQUIRED>\n"
				"\t\t\t\t\t\t<!ATTLIST dipvalue default (yes|no) \"no\">\n"
				"]>\n\n"
				"<softwarelists>\n"
				);
	}

	for ( int drvindex = 0; drivers[drvindex] != NULL; drvindex++ )
	{
		if ( mame_strwildcmp( gamename, drivers[drvindex]->name ) == 0 )
		{
			/* allocate the machine config */
			machine_config config(*drivers[drvindex]);

			for (const device_config *dev = config.m_devicelist.first(SOFTWARE_LIST); dev != NULL; dev = dev->typenext())
			{
				software_list_config *swlist = (software_list_config *)downcast<const legacy_device_config_base *>(dev)->inline_config();

				for ( int i = 0; i < DEVINFO_STR_SWLIST_MAX - DEVINFO_STR_SWLIST_0; i++ )
				{
					if ( swlist->list_name[i] && *swlist->list_name[i] && (swlist->list_type == SOFTWARE_LIST_ORIGINAL_SYSTEM))
					{
						software_list *list = software_list_open( *options, swlist->list_name[i], FALSE, NULL );

						if ( list )
						{
							/* Verify if we have encountered this list before */
							bool seen_before = false;
							for ( int l = 0; l < list_idx && !seen_before; l++ )
							{
								if ( ! strcmp( swlist->list_name[i], lists[l] ) )
								{
									seen_before = true;
								}
							}

							if ( ! seen_before )
							{
								lists[list_idx] = core_strdup( swlist->list_name[i] );
								list_idx++;
								software_list_parse( list, NULL, NULL );

								fprintf(out, "\t<softwarelist name=\"%s\" description=\"%s\">\n", swlist->list_name[i], xml_normalize_string(software_list_get_description(list)) );

								for ( software_info *swinfo = software_list_find( list, "*", NULL ); swinfo != NULL; swinfo = software_list_find( list, "*", swinfo ) )
								{
									fprintf( out, "\t\t<software name=\"%s\"", swinfo->shortname );
									if ( swinfo->parentname != NULL )
										fprintf( out, " cloneof=\"%s\"", swinfo->parentname );
									if ( swinfo->supported == SOFTWARE_SUPPORTED_PARTIAL )
										fprintf( out, " supported=\"partial\"" );
									if ( swinfo->supported == SOFTWARE_SUPPORTED_NO )
										fprintf( out, " supported=\"no\"" );
									fprintf( out, ">\n" );
									fprintf( out, "\t\t\t<description>%s</description>\n", xml_normalize_string(swinfo->longname) );
									fprintf( out, "\t\t\t<year>%s</year>\n", xml_normalize_string( swinfo->year ) );
									fprintf( out, "\t\t\t<publisher>%s</publisher>\n", xml_normalize_string( swinfo->publisher ) );

									for ( software_part *part = software_find_part( swinfo, NULL, NULL ); part != NULL; part = software_part_next( part ) )
									{
										fprintf( out, "\t\t\t<part name=\"%s\"", part->name );
										if ( part->interface_ )
											fprintf( out, " interface=\"%s\"", part->interface_ );

										fprintf( out, ">\n");

										if ( part->featurelist )
										{
											feature_list *list = part->featurelist;

											while( list )
											{
												fprintf( out, "\t\t\t\t<feature name=\"%s\" value=\"%s\" />\n", list->name, list->value );
												list = list->next;
											}
										}

										/* TODO: display rom region information */
										for ( const rom_entry *region = part->romdata; region; region = rom_next_region( region ) )
										{
											int is_disk = ROMREGION_ISDISKDATA(region);

											if (!is_disk)
												fprintf( out, "\t\t\t\t<dataarea name=\"%s\" size=\"%d\">\n", ROMREGION_GETTAG(region), ROMREGION_GETLENGTH(region) );
											else
												fprintf( out, "\t\t\t\t<diskarea name=\"%s\">\n", ROMREGION_GETTAG(region) );

											for ( const rom_entry *rom = rom_first_file( region ); rom && !ROMENTRY_ISREGIONEND(rom); rom++ )
											{
												if ( ROMENTRY_ISFILE(rom) )
												{
													if (!is_disk)
														fprintf( out, "\t\t\t\t\t<rom name=\"%s\" size=\"%d\"", xml_normalize_string(ROM_GETNAME(rom)), rom_file_size(rom) );
													else
														fprintf( out, "\t\t\t\t\t<disk name=\"%s\"", xml_normalize_string(ROM_GETNAME(rom)) );

													/* dump checksum information only if there is a known dump */
													hash_collection hashes(ROM_GETHASHDATA(rom));
													if (!hashes.flag(hash_collection::FLAG_NO_DUMP))
													{
														astring tempstr;
														for (hash_base *hash = hashes.first(); hash != NULL; hash = hash->next())
															fprintf(out, " %s=\"%s\"", hash->name(), hash->string(tempstr));
													}

													if (!is_disk)
														fprintf( out, " offset=\"0x%x\"", ROM_GETOFFSET(rom) );

													if ( hashes.flag(hash_collection::FLAG_BAD_DUMP) )
														fprintf( out, " status=\"baddump\"" );
													if ( hashes.flag(hash_collection::FLAG_NO_DUMP) )
														fprintf( out, " status=\"nodump\"" );

													if (is_disk)
														fprintf( out, " writable=\"%s\"", (ROM_GETFLAGS(rom) & DISK_READONLYMASK) ? "no" : "yes");

													if ((ROM_GETFLAGS(rom) & ROM_SKIPMASK) == ROM_SKIP(1))
														fprintf( out, " loadflag=\"load16_byte\"" );

													if ((ROM_GETFLAGS(rom) & ROM_SKIPMASK) == ROM_SKIP(3))
														fprintf( out, " loadflag=\"load32_byte\"" );

													if (((ROM_GETFLAGS(rom) & ROM_SKIPMASK) == ROM_SKIP(2)) && ((ROM_GETFLAGS(rom) & ROM_GROUPMASK) == ROM_GROUPWORD))
													{
														if (!(ROM_GETFLAGS(rom) & ROM_REVERSEMASK))
															fprintf( out, " loadflag=\"load32_word\"" );
														else
															fprintf( out, " loadflag=\"load32_word_swap\"" );
													}

													if (((ROM_GETFLAGS(rom) & ROM_SKIPMASK) == ROM_SKIP(6)) && ((ROM_GETFLAGS(rom) & ROM_GROUPMASK) == ROM_GROUPWORD))
													{
														if (!(ROM_GETFLAGS(rom) & ROM_REVERSEMASK))
															fprintf( out, " loadflag=\"load64_word\"" );
														else
															fprintf( out, " loadflag=\"load64_word_swap\"" );
													}

													if (((ROM_GETFLAGS(rom) & ROM_SKIPMASK) == ROM_NOSKIP) && ((ROM_GETFLAGS(rom) & ROM_GROUPMASK) == ROM_GROUPWORD))
													{
														if (!(ROM_GETFLAGS(rom) & ROM_REVERSEMASK))
															fprintf( out, " loadflag=\"load32_dword\"" );
														else
															fprintf( out, " loadflag=\"load16_word_swap\"" );
													}

													fprintf( out, "/>\n" );
												}
												else if ( ROMENTRY_ISRELOAD(rom) )
												{
													fprintf( out, "\t\t\t\t\t<rom size=\"%d\" offset=\"0x%x\" loadflag=\"reload\" />\n", ROM_GETLENGTH(rom), ROM_GETOFFSET(rom) );
												}
												else if ( ROMENTRY_ISCONTINUE(rom) )
												{
													fprintf( out, "\t\t\t\t\t<rom size=\"%d\" offset=\"0x%x\" loadflag=\"continue\" />\n", ROM_GETLENGTH(rom), ROM_GETOFFSET(rom) );
												}
												else if ( ROMENTRY_ISFILL(rom) )
												{
													fprintf( out, "\t\t\t\t\t<rom size=\"%d\" offset=\"0x%x\" loadflag=\"fill\" />\n", ROM_GETLENGTH(rom), ROM_GETOFFSET(rom) );
												}
											}

											if (!is_disk)
												fprintf( out, "\t\t\t\t</dataarea>\n" );
											else
												fprintf( out, "\t\t\t\t</diskarea>\n" );
										}

										fprintf( out, "\t\t\t</part>\n" );
									}

									fprintf( out, "\t\t</software>\n" );
								}

								fprintf(out, "\t</softwarelist>\n" );
							}

							software_list_close( list );
						}
					}
				}
			}
		}
	}

	if (nr_lists)
		fprintf( out, "</softwarelists>\n" );
	else
		fprintf( out, "No software lists found for this system\n" );

	global_free( lists );

	return MAMERR_NONE;
}


/*-------------------------------------------------
    softlist_match_roms - scan for a matching
    software ROM by hash
-------------------------------------------------*/
static void softlist_match_roms(core_options *options, const hash_collection &hashes, int length, int *found)
{
	int drvindex;

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
	{
		machine_config config(*drivers[drvindex]);

		for (const device_config *dev = config.m_devicelist.first(SOFTWARE_LIST); dev != NULL; dev = dev->typenext())
		{
			software_list_config *swlist = (software_list_config *)downcast<const legacy_device_config_base *>(dev)->inline_config();

			for ( int i = 0; i < DEVINFO_STR_SWLIST_MAX - DEVINFO_STR_SWLIST_0; i++ )
			{
				if ( swlist->list_name[i] )
				{
					software_list *list = software_list_open( *options, swlist->list_name[i], FALSE, NULL );

					for ( software_info *swinfo = software_list_find( list, "*", NULL ); swinfo != NULL; swinfo = software_list_find( list, "*", swinfo ) )
					{
						for ( software_part *part = software_find_part( swinfo, NULL, NULL ); part != NULL; part = software_part_next( part ) )
						{
							for ( const rom_entry *region = part->romdata; region != NULL; region = rom_next_region(region) )
							{
								for ( const rom_entry *rom = rom_first_file(region); rom != NULL; rom = rom_next_file(rom) )
								{
									hash_collection romhashes(ROM_GETHASHDATA(rom));
									if ( hashes == romhashes )
									{
										bool baddump = romhashes.flag(hash_collection::FLAG_BAD_DUMP);

										/* output information about the match */
										if (*found != 0)
											mame_printf_info("                    ");
										mame_printf_info("= %s%-20s  %s:%s %s\n", baddump ? _("(BAD) ") : "", ROM_GETNAME(rom), swlist->list_name[i], swinfo->shortname, swinfo->longname);
										(*found)++;
									}
								}
							}
						}
					}

					software_list_close( list );
				}
			}
		}
	}
}

/*-------------------------------------------------
    info_listmedia - output the list of image
    devices referenced by a given game or set of
    games
-------------------------------------------------*/

static int info_listmedia(core_options *options, const char *gamename)
{
	int count = 0, devcount;
	int drvindex;
	const device_config_image_interface *dev = NULL;
	const char *src;
	const char *driver_name;
	const char *name;
	const char *shortname;
	char paren_shortname[16];

	printf(_(" SYSTEM      MEDIA NAME (brief)   IMAGE FILE EXTENSIONS SUPPORTED     \n"));
	printf(_("----------  --------------------  ------------------------------------\n"));

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
		if (mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			/* allocate the machine config */
			machine_config config(*drivers[drvindex]);

			driver_name = drivers[drvindex]->name;

			devcount = 0;

			for (bool gotone = config.m_devicelist.first(dev); gotone; gotone = dev->next(dev))
			{
				src = downcast<const legacy_image_device_config_base *>(dev)->file_extensions();
				name = downcast<const legacy_image_device_config_base *>(dev)->instance_name();
				shortname = downcast<const legacy_image_device_config_base *>(dev)->brief_instance_name();

				sprintf(paren_shortname, "(%s)", shortname);

				printf("%-13s%-12s%-8s   ", driver_name, name, paren_shortname);
				driver_name = " ";

				astring extensions(src);
				char *ext = strtok((char*)extensions.cstr(),",");
				while (ext != NULL)
				{
					printf(".%-5s",ext);
					ext = strtok (NULL, ",");
					devcount++;
				}
				printf("\n");
			}
			if (!devcount)
				mame_printf_info(_("%-13s(none)\n"),driver_name);

			count++;
		}

	if (!count)
		mame_printf_info(_("There are no Computers or Consoles named %s\n"), gamename);

	return (count > 0) ? MAMERR_NONE : MAMERR_NO_SUCH_GAME;
}


/*-------------------------------------------------
    info_verifysamples - verify the sample sets of
    one or more games
-------------------------------------------------*/

static int info_verifysamples(core_options *options, const char *gamename)
{
	int correct = 0;
	int incorrect = 0;
	int notfound = FALSE;
	int drvindex;

	/* now iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
		if (mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			audit_record *audit;
			int audit_records;
			int res;

			/* audit the samples in this set */
			audit_records = audit_samples(options, drivers[drvindex], &audit);
			res = audit_summary(drivers[drvindex], audit_records, audit, TRUE);
			if (audit_records > 0)
				global_free(audit);
			else
				continue;

			/* if not found, print a message and set the flag */
			if (res == NOTFOUND)
			{
				mame_printf_error(_("sampleset \"%s\" not found!\n"), drivers[drvindex]->name);
				notfound = TRUE;
			}

			/* else display information about what we discovered */
			else
			{
				mame_printf_info(_("sampleset %s "), drivers[drvindex]->name);

				/* switch off of the result */
				switch (res)
				{
					case INCORRECT:
						mame_printf_info(_("is bad\n"));
						incorrect++;
						break;

					case CORRECT:
						mame_printf_info(_("is good\n"));
						correct++;
						break;

					case BEST_AVAILABLE:
						mame_printf_info(_("is best available\n"));
						correct++;
						break;
				}
			}
		}

	/* clear out any cached files */
	zip_file_cache_clear();

	/* if we didn't get anything at all because of an unsupported set, display message */
	if (correct + incorrect == 0)
	{
		if (!notfound)
			mame_printf_error(_("sampleset \"%s\" not supported!\n"), gamename);
		return MAMERR_NO_SUCH_GAME;
	}

	/* otherwise, print a summary */
	else
	{
		mame_printf_info(_("%d samplesets found, %d were OK.\n"), correct + incorrect, correct);
		return (incorrect > 0) ? MAMERR_MISSING_FILES : MAMERR_NONE;
	}
}


/*-------------------------------------------------
    info_romident - identify ROMs by looking for
    matches in our internal database
-------------------------------------------------*/

static int info_romident(core_options *options, const char *gamename)
{
	romident_status status;

	/* a NULL gamename is a fatal error */
	if (gamename == NULL)
		return MAMERR_FATALERROR;

	/* do the identification */
	romident(options, gamename, &status);

	/* clear out any cached files */
	zip_file_cache_clear();

	/* return the appropriate error code */
	if (status.matches == status.total)
		return MAMERR_NONE;
	else if (status.matches == status.total - status.nonroms)
		return MAMERR_IDENT_NONROMS;
	else if (status.matches > 0)
		return MAMERR_IDENT_PARTIAL;
	else
		return MAMERR_IDENT_NONE;
}


/*-------------------------------------------------
    cli_info_listgames - output the manufacturer
    for make tp_manufact.txt
-------------------------------------------------*/

int cli_info_listgames(core_options *options, const char *gamename)
{
	int drvindex, count = 0;

	/* a NULL gamename == '*' */
	if (gamename == NULL)
		gamename = "*";

	for (drvindex = 0; drivers[drvindex]; drvindex++)
		if ((drivers[drvindex]->flags & GAME_NO_STANDALONE) == 0 && mame_strwildcmp(gamename, drivers[drvindex]->name) == 0)
		{
			char name[200];

			mame_printf_info("%-5s%-36s ",drivers[drvindex]->year, _MANUFACT(drivers[drvindex]->manufacturer));

			if (lang_message_is_enabled(UI_MSG_LIST))
			{
				strcpy(name, _LST(drivers[drvindex]->description));
				mame_printf_info("\"%s\"\n", name);
				continue;
			}

			namecopy(name,drivers[drvindex]->description);
			mame_printf_info("\"%s",name);

			/* print the additional description only if we are listing clones */
			{
				const char *pdest = strstr(drivers[drvindex]->description, " (");

				if (pdest != NULL && pdest > drivers[drvindex]->description)
					mame_printf_info("%s", pdest);
			}

			mame_printf_info("\"\n");

			count++;
		}

	/* return an error if none found */
	return (count > 0) ? MAMERR_NONE : MAMERR_NO_SUCH_GAME;
}



/***************************************************************************
    UTILITIES
***************************************************************************/

/*-------------------------------------------------
    romident - identify files
-------------------------------------------------*/

static void romident(core_options *options, const char *filename, romident_status *status)
{
	osd_directory *directory;

	/* reset the status */
	memset(status, 0, sizeof(*status));

	/* first try to open as a directory */
	directory = osd_opendir(filename);
	if (directory != NULL)
	{
		const osd_directory_entry *entry;

		/* iterate over all files in the directory */
		while ((entry = osd_readdir(directory)) != NULL)
			if (entry->type == ENTTYPE_FILE)
			{
				astring curfile(filename, PATH_SEPARATOR, entry->name);
				identify_file(options, curfile, status);
			}
		osd_closedir(directory);
	}

	/* if that failed, and the filename ends with .zip, identify as a ZIP file */
	else if (core_filename_ends_with(filename, ".zip"))
	{
		/* first attempt to examine it as a valid ZIP file */
		zip_file *zip = NULL;
		zip_error ziperr = zip_file_open(filename, &zip);
		if (ziperr == ZIPERR_NONE && zip != NULL)
		{
			const zip_file_header *entry;

			/* loop over entries in the ZIP, skipping empty files and directories */
			for (entry = zip_file_first_file(zip); entry; entry = zip_file_next_file(zip))
				if (entry->uncompressed_length != 0)
				{
					UINT8 *data = global_alloc_array(UINT8, entry->uncompressed_length);
					if (data != NULL)
					{
						/* decompress data into RAM and identify it */
						ziperr = zip_file_decompress(zip, data, entry->uncompressed_length);
						if (ziperr == ZIPERR_NONE)
							identify_data(options, entry->filename, data, entry->uncompressed_length, status);
						global_free(data);
					}
				}

			/* close up */
			zip_file_close(zip);
		}
	}

	/* otherwise, identify as a raw file */
	else
		identify_file(options, filename, status);
}


/*-------------------------------------------------
    identify_file - identify a file; if it is a
    ZIP file, scan it and identify all enclosed
    files
-------------------------------------------------*/

static void identify_file(core_options *options, const char *name, romident_status *status)
{
	file_error filerr;
	osd_file *file;
	UINT64 length;

	if (core_filename_ends_with(name, ".chd"))
	{
		chd_file *chd;
		chd_error err;
		astring basename;
		int found = 0;

		core_filename_extract_base(&basename, name, FALSE);
		mame_printf_info("%-20s", basename.cstr());

		status->total++;

		err = chd_open(name, CHD_OPEN_READ, NULL, &chd);
		if (err != CHDERR_NONE)
		{
			mame_printf_info(_("NOT A CHD\n"));
			status->nonroms++;
		}
		else
		{
			chd_header header;

			header = *chd_get_header(chd);
			if (header.flags & CHDFLAGS_IS_WRITEABLE)
			{
				mame_printf_info(_("is a writable CHD\n"));
			}
			else
			{
				static const UINT8 nullhash[20] = { 0 };
				hash_collection hashes;

				/* if there's an MD5 or SHA1 hash, add them to the output hash */
				if (memcmp(nullhash, header.md5, sizeof(header.md5)) != 0)
					hashes.add_from_buffer(hash_collection::HASH_MD5, header.md5, sizeof(header.md5));
				if (memcmp(nullhash, header.sha1, sizeof(header.sha1)) != 0)
					hashes.add_from_buffer(hash_collection::HASH_SHA1, header.sha1, sizeof(header.sha1));

				length = header.logicalbytes;

				match_roms(options, hashes, length, &found);

				if (found == 0)
				{
					mame_printf_info(_("NO MATCH\n"));
				}

				/* if we did find it, count it as a match */
				else
					status->matches++;
			}

			chd_close(chd);
		}
	}
	else
	{
		/* open for read and process if it opens and has a valid length */
		filerr = osd_open(name, OPEN_FLAG_READ, &file, &length);
		if (filerr == FILERR_NONE && length > 0 && (UINT32)length == length)
		{
			UINT8 *data = global_alloc_array(UINT8, length);
			if (data != NULL)
			{
				UINT32 bytes;

				/* read file data into RAM and identify it */
				filerr = osd_read(file, data, 0, length, &bytes);
				if (filerr == FILERR_NONE)
					identify_data(options, name, data, bytes, status);
				global_free(data);
			}
			osd_close(file);
		}
	}
}


/*-------------------------------------------------
    identify_data - identify a buffer full of
    data; if it comes from a .JED file, parse the
    fusemap into raw data first
-------------------------------------------------*/

static void identify_data(core_options *options, const char *name, const UINT8 *data, int length, romident_status *status)
{
	UINT8 *tempjed = NULL;
	astring basename;
	int found = 0;
	jed_data jed;

	/* if this is a '.jed' file, process it into raw bits first */
	if (core_filename_ends_with(name, ".jed") && jed_parse(data, length, &jed) == JEDERR_NONE)
	{
		/* now determine the new data length and allocate temporary memory for it */
		length = jedbin_output(&jed, NULL, 0);
		tempjed = global_alloc_array(UINT8, length);
		if (tempjed == NULL)
			return;

		/* create a binary output of the JED data and use that instead */
		jedbin_output(&jed, tempjed, length);
		data = tempjed;
	}

	/* compute the hash of the data */
	hash_collection hashes;
	hashes.compute(data, length, hash_collection::HASH_TYPES_CRC_SHA1);

	/* output the name */
	status->total++;
	core_filename_extract_base(&basename, name, FALSE);
	mame_printf_info("%-20s", basename.cstr());

	/* see if we can find a match in the ROMs */
	match_roms(options, hashes, length, &found);

	/* if we didn't find it, try to guess what it might be */
	if (found == 0)
	{
		/* if not a power of 2, assume it is a non-ROM file */
		if ((length & (length - 1)) != 0)
		{
			mame_printf_info(_("NOT A ROM\n"));
			status->nonroms++;
		}

		/* otherwise, it's just not a match */
		else
			mame_printf_info(_("NO MATCH\n"));
	}

	/* if we did find it, count it as a match */
	else
		status->matches++;

	/* free any temporary JED data */
	if (tempjed != NULL)
		global_free(tempjed);
}


/*-------------------------------------------------
    match_roms - scan for a matching ROM by hash
-------------------------------------------------*/

static void match_roms(core_options *options, const hash_collection &hashes, int length, int *found)
{
	int drvindex;

	/* iterate over drivers */
	for (drvindex = 0; drivers[drvindex] != NULL; drvindex++)
	{
		machine_config config(*drivers[drvindex]);
		const rom_entry *region, *rom;
		const rom_source *source;

		/* iterate over sources, regions and files within the region */
		for (source = rom_first_source(config); source != NULL; source = rom_next_source(*source))
			for (region = rom_first_region(*source); region; region = rom_next_region(region))
				for (rom = rom_first_file(region); rom; rom = rom_next_file(rom))
				{
					hash_collection romhashes(ROM_GETHASHDATA(rom));
					if (!romhashes.flag(hash_collection::FLAG_NO_DUMP) && hashes == romhashes)
					{
						bool baddump = romhashes.flag(hash_collection::FLAG_BAD_DUMP);

						/* output information about the match */
						if (*found != 0)
							mame_printf_info("                    ");
						mame_printf_info("= %s%-20s  %-10s %s\n", baddump ? _("(BAD) ") : "", ROM_GETNAME(rom), drivers[drvindex]->name, _LST(drivers[drvindex]->description));
						(*found)++;
					}
				}
	}

	softlist_match_roms( options, hashes, length, found );
}


static void namecopy(char *name_ref, const char *desc)
{
	char name[200];

	if (lang_message_is_enabled(UI_MSG_LIST))
	{
		strcpy(name, _LST(desc));
		if (strstr(name," (")) *strstr(name," (") = 0;
		sprintf(name_ref,"%s",name);
		return;
	}

	strcpy(name,desc);

	/* remove details in parenthesis */
	if (strstr(name," (")) *strstr(name," (") = 0;

	/* Move leading "The" to the end */
	if (strncmp(name,"The ",4) == 0)
		sprintf(name_ref,"%s, The",name+4);
	else
		sprintf(name_ref,"%s",name);
}
