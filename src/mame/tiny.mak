###########################################################################
#
#   tiny.mak
#
#   Small driver-specific example makefile
#	Use make SUBTARGET=tiny to build
#
#   Copyright Nicola Salmoria and the MAME Team.
#   Visit  http://mamedev.org for licensing and usage restrictions.
#
###########################################################################

MAMESRC = $(SRC)/mame
MAMEOBJ = $(OBJ)/mame

AUDIO = $(MAMEOBJ)/audio
DRIVERS = $(MAMEOBJ)/drivers
LAYOUT = $(MAMEOBJ)/layout
MACHINE = $(MAMEOBJ)/machine
VIDEO = $(MAMEOBJ)/video

OBJDIRS += \
	$(AUDIO) \
	$(DRIVERS) \
	$(LAYOUT) \
	$(MACHINE) \
	$(VIDEO) \

DEFS += -DTINY_BUILD



#-------------------------------------------------
# Specify all the CPU cores necessary for the
# drivers referenced in tiny.c.
#-------------------------------------------------

CPUS += Z80
CPUS += M6502
CPUS += I386
CPUS += MCS48
CPUS += MCS51
CPUS += M6800
CPUS += M6809
CPUS += M680X0
CPUS += TMS9900
CPUS += COP400
CPUS += SH2



#-------------------------------------------------
# Specify all the sound cores necessary for the
# drivers referenced in tiny.c.
#-------------------------------------------------

SOUNDS += SAMPLES
SOUNDS += DAC
SOUNDS += DISCRETE
SOUNDS += AY8910
SOUNDS += YM2151
SOUNDS += YM2203
SOUNDS += YM2608
SOUNDS += YM2610
SOUNDS += YMF278B
SOUNDS += YMZ280B
SOUNDS += ASTROCADE
SOUNDS += TMS5220
ifneq ($(WINUI),)
SOUNDS += VLM5030
endif
SOUNDS += OKIM6295
SOUNDS += HC55516
SOUNDS += YM3812
SOUNDS += CEM3394



#-------------------------------------------------
# This is the list of files that are necessary
# for building all of the drivers referenced
# in tiny.c
#-------------------------------------------------

DRVLIBS = \
	$(MAMEOBJ)/tiny.o \
	$(EMUDRIVERS)/emudummy.o \
	$(MACHINE)/ticket.o \
	$(DRIVERS)/carpolo.o $(MACHINE)/carpolo.o $(VIDEO)/carpolo.o \
	$(DRIVERS)/circus.o $(AUDIO)/circus.o $(VIDEO)/circus.o \
	$(DRIVERS)/exidy.o $(AUDIO)/exidy.o $(VIDEO)/exidy.o \
	$(AUDIO)/exidy440.o \
	$(DRIVERS)/starfire.o $(VIDEO)/starfire.o \
	$(DRIVERS)/vertigo.o $(MACHINE)/vertigo.o $(VIDEO)/vertigo.o \
	$(DRIVERS)/victory.o $(VIDEO)/victory.o \
	$(AUDIO)/targ.o \
	$(DRIVERS)/astrocde.o $(VIDEO)/astrocde.o \
	$(DRIVERS)/gridlee.o $(AUDIO)/gridlee.o $(VIDEO)/gridlee.o \
	$(DRIVERS)/williams.o $(MACHINE)/williams.o $(AUDIO)/williams.o $(VIDEO)/williams.o \
	$(AUDIO)/gorf.o \
	$(AUDIO)/wow.o \
	$(DRIVERS)/gaelco.o $(VIDEO)/gaelco.o $(MACHINE)/gaelcrpt.o \
	$(DRIVERS)/wrally.o $(MACHINE)/wrally.o $(VIDEO)/wrally.o \
	$(DRIVERS)/looping.o \
	$(DRIVERS)/supertnk.o \

DRVLIBS += \
	$(MAMEOBJ)/psikyo.a \
	$(MAMEOBJ)/misc.a \
	$(MAMEOBJ)/shared.a \



#-------------------------------------------------
# the following files are general components and
# shared across a number of drivers
#-------------------------------------------------

$(MAMEOBJ)/shared.a: \
	$(MACHINE)/nmk112.o \



#-------------------------------------------------
# manufacturer-specific groupings for drivers
#-------------------------------------------------

$(MAMEOBJ)/psikyo.a: \
	$(DRIVERS)/psikyo.o $(VIDEO)/psikyo.o \
	$(DRIVERS)/psikyosh.o $(VIDEO)/psikyosh.o \


#-------------------------------------------------
# remaining drivers
#-------------------------------------------------

$(MAMEOBJ)/misc.a: \
	$(DRIVERS)/cave.o $(VIDEO)/cave.o \



#-------------------------------------------------
# layout dependencies
#-------------------------------------------------

$(DRIVERS)/astrocde.o:	$(LAYOUT)/gorf.lh \
						$(LAYOUT)/tenpindx.lh
$(DRIVERS)/circus.o:	$(LAYOUT)/circus.lh \
						$(LAYOUT)/crash.lh

$(MAMEOBJ)/mamedriv.o:	$(LAYOUT)/pinball.lh
