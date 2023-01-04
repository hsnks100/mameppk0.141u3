/******************************************************************************

    mameplusdriv.c

    This is an unofficial version based on MAME.
    Please do not send any reports from this build to the MAME team.

    The list of all available drivers. Drivers have to be included here to be
    recognized by the executable.

    To save some typing, we use a hack here. This file is recursively #included
    twice, with different definitions of the DRIVER() macro. The first one
    declares external references to the drivers; the second one builds an array
    storing all the drivers.

******************************************************************************/

#include "emu.h"

#ifndef DRIVER_RECURSIVE

#define DRIVER_RECURSIVE

/* step 1: declare all external references */
#define DRIVER(NAME) GAME_EXTERN(NAME);
#include "mameplusdriv.c"

/* step 2: define the drivers[] array */
#undef DRIVER
#define DRIVER(NAME) &GAME_NAME(NAME),
const game_driver * const plusdrivers[] =
{
#include "mameplusdriv.c"
	0	/* end of array */
};

#else	/* DRIVER_RECURSIVE */

	/* Capcom CPS1 bootleg */
	DRIVER( knightsb2 )	/* bootleg */
	DRIVER( wofb )		/* bootleg */
	DRIVER( wofsj )		/* bootleg, 1995  Holy Sword Three Kingdoms / Sheng Jian San Guo */
	DRIVER( wofsja )	/* bootleg, 1995  Holy Sword Three Kingdoms / Sheng Jian San Guo */
	DRIVER( wofsjb )	/* bootleg, 1995  Holy Sword Three Kingdoms / Sheng Jian San Guo */
	DRIVER( wof3js )	/* bootleg, 1997  Three Sword Masters / San Jian Sheng */
	DRIVER( wof3sj )	/* bootleg, 1997  Three Holy Swords / San Sheng Jian */
	DRIVER( wof3sja )	/* bootleg, 1997  Three Holy Swords / San Sheng Jian */
	DRIVER( wofh ) 		/* bootleg, 1999  Legend of Three Kingdoms' Heroes / Sanguo Yingxiong Zhuan */
	DRIVER( wofha ) 	/* bootleg, 1999  Legend of Three Kingdoms' Heroes / Sanguo Yingxiong Zhuan */
	DRIVER( cawingb )	/* bootleg */
	DRIVER( sf2m8 )		/* bootleg */
	DRIVER( sf2m9 )		/* bootleg */
	DRIVER( sf2m10 )	/* bootleg */
	DRIVER( sf2m11 )	/* bootleg */
	DRIVER( sf2m12 )	/* bootleg */
	DRIVER( sf2m13 )	/* bootleg */
	DRIVER( sf2tlona )	/* bootleg, Tu Long set 1 */
	DRIVER( sf2tlonb )	/* bootleg, Tu Long set 2 */

	/* Capcom CPS1 hack */
	DRIVER( knightsh )	/* hack */
	DRIVER( kodh )		/* hack */
	DRIVER( dinoh )		/* hack */
	DRIVER( dinoha )	/* hack */
	DRIVER( dinohb )	/* hack */

	/* Neo Geo bootleg */
	DRIVER( kof96ep )	/* 0214 (c) 1996 bootleg */
	DRIVER( kof97pla )	/* 0232 (c) 2003 bootleg */
	DRIVER( kf2k1pls )	/* 0262 (c) 2001 bootleg */
	DRIVER( kf2k1pa )	/* 0262 (c) 2001 bootleg */
	DRIVER( cthd2k3a )	/* bootleg of kof2001*/
	DRIVER( kf2k2plb )	/* 0265 (c) 2002 bootleg */
	DRIVER( kf2k2plc )	/* 0265 (c) 2002 bootleg */
	DRIVER( kf2k4pls )	/* bootleg of kof2002 */
	DRIVER( mslug5b )	/* 0268 (c) 2003 bootleg */

	/* Neo Geo prototype */
	DRIVER( bangbedp )	/* 0259 prototype */

	/* CD to MVS Conversion */
	DRIVER( zintrkcd )	/* 0211 hack - CD to MVS Conversion by Razoola */
	DRIVER( fr2ch )		/* 0098 hack - CD to MVS Conversion */

	/* Wii Virtual Console to MVS Conversion */
	DRIVER( ironclad )	/* 0220 (c) 1996 Saurus - Wii Virtual Console to MVS Conversion */

#ifndef NCP
	/* Konami "Nemesis hardware" games */
	DRIVER( spclone )	/* GX587 (c) 1986 based */
#endif /* !NCP */

#ifdef MAMEUIPLUSPLUS
	/* Capcom CPS2 games */
	DRIVER( hsf2nb )			/* 02/02/2004 (c) 2004 (Asia) Phoenix Edition */

	/* Capcom CPS3 games */
	DRIVER( jojobap )	/* 13/09/1999 (c) 1999 Capcom */

	/* Neo Geo games */
	DRIVER( ssh5spnd )	/* 0272 (c) 2004 Yuki Enterprise / SNK Playmore */
#endif /* MAMEUIPLUSPLUS */

#ifdef KAILLERA
	/* Capcom CPS1 games */
	DRIVER( captcomm3p )	/* 14/10/1991 (c) 1991 (World) */
	DRIVER( captcomu3p )	/* 28/ 9/1991 (c) 1991 (US)    */
	DRIVER( captcomj3p )	/* 02/12/1991 (c) 1991 (Japan) */
	DRIVER( captcomm4p )	/* 14/10/1991 (c) 1991 (World) */
	DRIVER( captcomu4p )	/* 28/ 9/1991 (c) 1991 (US)    */
	DRIVER( captcomj4p )	/* 02/12/1991 (c) 1991 (Japan) */

	/* Capcom CPS2 games */
	DRIVER( avsp3p )		/* 20/05/1994 (c) 1994 (Euro) */
	DRIVER( avspu3p )		/* 20/05/1994 (c) 1994 (US) */
	DRIVER( avspj3p )		/* 20/05/1994 (c) 1994 (Japan) */
	DRIVER( avspa3p )		/* 20/05/1994 (c) 1994 (Asia) */
	DRIVER( batcir4p )		/* 19/03/1997 (c) 1997 (Euro) */
	DRIVER( batcirj4p )		/* 19/03/1997 (c) 1997 (Japan) */
	DRIVER( ddsom4p )		/* 19/06/1996 (c) 1996 (Euro) */
	DRIVER( ddsomr2_4p )	/* 09/02/1996 (c) 1996 (Euro) */
	DRIVER( ddsomu4p )		/* 19/06/1996 (c) 1996 (US) */
	DRIVER( ddsomur1_4p )	/* 09/02/1996 (c) 1996 (US) */
	DRIVER( ddsomjr1_4p )	/* 06/02/1996 (c) 1996 (Japan) */
	DRIVER( ddsomj4p )		/* 19/06/1996 (c) 1996 (Japan) */
	DRIVER( ddsoma4p )		/* 19/06/1996 (c) 1996 (Asia) */
	DRIVER( sfa3p )			/* 04/09/1998 (c) 1998 (US) */
	DRIVER( sfa3up )		/* 04/09/1998 (c) 1998 (US) */
	DRIVER( sfa3ur1p )		/* 29/06/1998 (c) 1998 (US) */
	DRIVER( sfz3jp )		/* 04/09/1998 (c) 1998 (Japan) */
	DRIVER( sfz3jr1p )		/* 27/07/1998 (c) 1998 (Japan) */
	DRIVER( sfz3jr2p )		/* 29/06/1998 (c) 1998 (Japan) */
	DRIVER( sfz3ar1p )		/* 01/07/1998 (c) 1998 (Asia) */
#ifdef MAMEUIPLUSPLUS
	DRIVER( xmvsfregion4p )	/* 23/10/1996 (c) 1996 (US) */
#endif /* MAMEUIPLUSPLUS */
	DRIVER( mshvsfj4p )		/* 07/07/1997 (c) 1997 (Japan) */
	DRIVER( mvscj4p )		/* 23/01/1998 (c) 1998 (Japan) */

	/* Neo Geo games */
	DRIVER( lbowling4p )	/* 0019 (c) 1990 SNK */
	DRIVER( kof95_6p )		/* 0084 (c) 1995 SNK */
	DRIVER( kof98_6p )		/* 0242 (c) 1998 SNK */

#ifndef NCP
	/* Video System Co. games */
	DRIVER( fromanc2_k )	/* (c) 1995 Video System Co. (Japan) */
	DRIVER( fromancr_k )	/* (c) 1995 Video System Co. (Japan) */
	DRIVER( fromanc4_k )	/* (c) 1998 Video System Co. (Japan) */

	/* Namco System 86 games */
	DRIVER( roishtar2p )	/* (c) 1986 */

	/* Psikyo games */
	DRIVER( hotgmck_k )		/* (c) 1997 */
	DRIVER( hgkairak_k )	/* (c) 1998 */
	DRIVER( hotgmck3_k )	/* (c) 1999 */
	DRIVER( hotgm4ev_k )	/* (c) 2000 */
	DRIVER( loderndf_k )	/* (c) 2000 */
	DRIVER( loderndf_vs )	/* (c) 2000 */
	DRIVER( hotdebut_k )	/* (c) 2000 */

	/* Konami games */
	DRIVER( hyprolym4p )	/* GX361 (c) 1983 */
	DRIVER( hyperspt4p )	/* GX330 (c) 1984 + Centuri */
	DRIVER( hpolym84_4p )	/* GX330 (c) 1984 */

	/* System 16C */
	DRIVER( fz2dx )	/* (c) 1987 */

	/* Sega System 32 games */
	DRIVER( ga2j4p )		/* (c) 1992 (Japan) */

	/* IGS PGM System Games */
 	DRIVER( kov2106_4p )	/* (c) 2000 */
 	DRIVER( kov2p4p )		/* (c) 2000 */
#endif /* NCP */

#endif /* KAILLERA */

#endif	/* DRIVER_RECURSIVE */
