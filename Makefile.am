ACLOCAL_AMFLAGS = -I m4

if USE_TAPENC
TAPENC_DIR = tapenc
else
TAPENC_DIR =
endif

if USE_TAPDEC
TAPDEC_DIR = tapdec
else
TAPDEC_DIR =
endif

SUBDIRS = tap \
$(TAPENC_DIR) \
$(TAPDEC_DIR)

EXTRA_DIST = autogen.sh
