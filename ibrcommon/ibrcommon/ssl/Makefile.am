## sub directory

SUBDIRS = gcm

h_sources = \
	CipherStream.h \
	HashStream.h \
	HMacStream.h \
	XORStream.h \
	RSASHA256Stream.h \
	AES128Stream.h \
	TLSStream.h \
	iostreamBIO.h

cc_sources = \
	CipherStream.cpp \
	HashStream.cpp \
	HMacStream.cpp \
	XORStream.cpp \
	RSASHA256Stream.cpp \
	AES128Stream.cpp \
	TLSStream.cpp \
	iostreamBIO.cpp

#Install the headers in a versioned directory
library_includedir = $(includedir)/$(GENERIC_LIBRARY_NAME)-$(GENERIC_API_VERSION)/$(GENERIC_LIBRARY_NAME)/ssl
library_include_HEADERS = $(h_sources)

AM_CPPFLAGS = -I$(top_srcdir) $(OPENSSL_CFLAGS)
AM_LDFLAGS = $(OPENSSL_LIBS)

if ANDROID
noinst_DATA = Android.mk
CLEANFILES = Android.mk
else
noinst_LTLIBRARIES = libibrcommon_ssl.la
libibrcommon_ssl_la_SOURCES = $(h_sources) $(cc_sources)
endif

Android.mk: Makefile.am
	$(ANDROGENIZER) -:PROJECT $(GENERIC_LIBRARY_NAME) \
		-:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
		-:STATIC libibrcommon_ssl \
		-:SOURCES $(h_sources) $(cc_sources) \
		-:CPPFLAGS $(AM_CPPFLAGS) \
		-:LDFLAGS $(AM_LDFLAGS) \
		-:SUBDIR $(patsubst %,ibrcommon/ssl/%, $(SUBDIRS)) \
		> $@
