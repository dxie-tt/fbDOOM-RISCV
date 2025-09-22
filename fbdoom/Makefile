################################################################
#
# $Id:$
#
# $Log:$
#

CROSS_COMPILE=/opt/tenstorrent/tt-riscv-toolchain-latest/bin/riscv64-unknown-linux-gnu-
NOSDL=1

ifeq ($(V),1)
	VB=''
else
	VB=@
endif

#LIBS+=-lXext -lX11 -lnsl -lm -lSDL
#CFLAGS+=-Wunused-const-variable=0 
#CFLAGS+=-fsanitize=address
OBJS+=$(OBJDIR)/i_video_fbdev.o
OBJS+=$(OBJDIR)/i_input_linux.o

OBJS_VEC+=$(OBJDIR_VEC)/i_video_fbdev.o
OBJS_VEC+=$(OBJDIR_VEC)/i_input_linux.o

OBJS_AUTOVEC+=$(OBJDIR_VEC)/i_video_fbdev.o
OBJS_AUTOVEC+=$(OBJDIR_VEC)/i_input_linux.o

CC=$(CROSS_COMPILE)gcc  # gcc or g++
CFLAGS+=-ggdb3 -O3 -g -mcpu=tt-ascalon-d8 -std=c11 -D_GNU_SOURCE
LDFLAGS+=-Wl,--gc-sections
CFLAGS+=-ggdb3 -Wall -DNORMALUNIX -DLINUX -DSNDSERV # -DUSEASM
LIBS+=-lm -lc -static

ifneq ($(NOSDL),1)
	LIBS+= -lSDL
endif

# subdirectory for objects
OBJDIR=build
OBJDIR_VEC=build_vec
OBJDIR_AUTOVEC=build_autovec
OUTPUT=fbdoom
OUTPUT_VEC=fbdoom_vec
OUTPUT_AUTOVEC=fbdoom_autovec

SRC_DOOM = i_main.o dummy.o am_map.o doomdef.o doomstat.o dstrings.o d_event.o d_items.o d_iwad.o d_loop.o d_main.o d_mode.o d_net.o f_finale.o f_wipe.o g_game.o hu_lib.o hu_stuff.o info.o i_cdmus.o i_endoom.o i_joystick.o i_scale.o i_sound.o i_system.o i_timer.o memio.o m_argv.o m_bbox.o m_cheat.o m_config.o m_controls.o m_fixed.o m_menu.o m_misc.o m_random.o p_ceilng.o p_doors.o p_enemy.o p_floor.o p_inter.o p_lights.o p_map.o p_maputl.o p_mobj.o p_plats.o p_pspr.o p_saveg.o p_setup.o p_sight.o p_spec.o p_switch.o p_telept.o p_tick.o p_user.o r_bsp.o r_data.o r_draw.o r_main.o r_plane.o r_segs.o r_sky.o r_things.o sha1.o sounds.o statdump.o st_lib.o st_stuff.o s_sound.o tables.o v_video.o wi_stuff.o w_checksum.o w_file.o w_file_stdc_unbuffered.o w_main.o w_wad.o z_zone.o
OBJS += $(addprefix $(OBJDIR)/, $(SRC_DOOM))
OBJS_VEC += $(addprefix $(OBJDIR_VEC)/, $(SRC_DOOM))
OBJS_AUTOVEC += $(addprefix $(OBJDIR_AUTOVEC)/, $(SRC_DOOM))

all:	 $(OUTPUT) $(OUTPUT_VEC) $(OUTPUT_AUTOVEC)

clean:
	rm -rf $(OBJDIR)
	rm -rf $(OBJDIR_VEC)
	rm -rf $(OBJDIR_AUTOVEC)
	rm -f $(OUTPUT)
	rm -f $(OUTPUT_VEC)
	rm -f $(OUTPUT_AUTOVEC)
	rm -f $(OUTPUT).gdb
	rm -f $(OUTPUT).map
	rm -f $(OUTPUT_VEC).gdb
	rm -f $(OUTPUT_AUTOVEC).map
	rm -f $(OUTPUT_AUTOVEC).gdb
	rm -f $(OUTPUT_VEC).map

$(OUTPUT):	$(OBJS)
	@echo [Linking $@]
	$(VB)$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) \
	-o $(OUTPUT) $(LIBS) -Wl,-Map,$(OUTPUT).map
	@echo [Size]
	-$(CROSS_COMPILE)size $(OUTPUT)

$(OUTPUT_VEC):	$(OBJS_VEC)
	@echo [Linking $@]
	$(VB)$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS_VEC) \
	-o $(OUTPUT_VEC) $(LIBS) -Wl,-Map,$(OUTPUT_VEC).map
	@echo [Size]
	-$(CROSS_COMPILE)size $(OUTPUT_VEC)

$(OUTPUT_AUTOVEC):	$(OBJS_AUTOVEC)
	@echo [Linking $@]
	$(VB)$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS_AUTOVEC) \
	-o $(OUTPUT_AUTOVEC) $(LIBS) -Wl,-Map,$(OUTPUT_AUTOVEC).map
	@echo [Size]
	-$(CROSS_COMPILE)size $(OUTPUT_AUTOVEC)

$(OBJS): | $(OBJDIR)
$(OBJS_VEC): | $(OBJDIR_VEC)
$(OBJS_AUTOVEC): | $(OBJDIR_AUTOVEC)

$(OBJDIR):
	mkdir -p $(OBJDIR)
$(OBJDIR_VEC):
	mkdir -p $(OBJDIR_VEC)
$(OBJDIR_AUTOVEC):
	mkdir -p $(OBJDIR_AUTOVEC)

$(OBJDIR)/%.o:	%.c
	@echo [Compiling $<]
	$(VB)$(CC) $(CFLAGS) -c $< -o $@ -fno-tree-vectorize

$(OBJDIR_VEC)/%.o:	%.c
	@echo [Compiling $<]
	$(VB)$(CC) $(CFLAGS) -c $< -o $@ -DUSE_VECTOR

$(OBJDIR_AUTOVEC)/%.o:	%.c
	@echo [Compiling $<]
	$(VB)$(CC) $(CFLAGS) -c $< -o $@

print:
	@echo OBJS: $(OBJS)

