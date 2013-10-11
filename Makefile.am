ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libmmq.la

mmqincludedir = $(includedir)
nobase_mmqinclude_HEADERS = mmq.h lin_fifo.h
libmmq_la_SOURCES = mmq.C

dist_noinst_SCRIPTS = autogen.sh

AM_CPPFLAGS = -I$(top_srcdir)

AM_DEFAULT_SOURCE_EXT = .C
TESTS = test/test
test_test_LDADD = libmmq.la
check_PROGRAMS = $(TESTS)

PHONY: git-clean update-m4

include aminclude.am

MOSTLYCLEANFILES = $(DX_CLEANFILES)

EXTRA_DIST = cleanup bootstrap $(DX_CONFIG)

git-clean: clean
	git status --ignored --porcelain | sed -ne 's:^\(??\|!!\) ::p' | xargs rm -rf

update-m4:
	cd m4; for i in ax_*.m4; do\
	    wget "http://git.savannah.gnu.org/gitweb/?p=autoconf-archive.git;a=blob_plain;f=m4/$$i" -O "$$i.tmp";\
	    mv "$$i"{.tmp,};\
	done
