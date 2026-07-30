bin_PROGRAMS = pekwm_lock
pekwm_lock_SOURCES = pekwm_lock.cc pekwm_lock.hh
pekwm_lock_CXXFLAGS = $(LIB_CFLAGS) $(libcurl_CFLAGS) -I../lib -I../
pekwm_lock_LDADD = ../tk/libpekwm_tk.a ../lib/libpekwm_lib.a $(LIB_LIBS) \
		   $(TK_LIBS) $(pam_LIBS) $(libsystemd_LIBS)

EXTRA_DIST = CMakeLists.txt
