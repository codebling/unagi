AM_CPPFLAGS = -I$(top_srcdir)/include $(UNAGI_CFLAGS)
unagi_LDADD = $(UNAGI_LIBS) -ldl
unagi_LDFLAGS = -rdynamic
bin_PROGRAMS = unagi
bin_SCRIPTS = unagi-client

## For sqrtl() used to measure painting performance
if DEBUG
unagi_LDADD += -lm
endif

unagi_SOURCES = display.c 	\
	event.c 		\
	window.c 		\
	atoms.c 		\
	util.c 			\
	key.c 			\
	plugin.c		\
	plugin_common.c		\
	rendering.c		\
	dbus.c			\
	unagi.c
