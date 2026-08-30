SUBDIRS = atrun definitions cfg

EXTRA_DIST = *.rb fixtures/complete-frame-gate.v1.tsv fixtures/infinite-frame-adapter-abi.v2.tsv runat.sh runat.bat runut.sh runut.bat setup.sh

AUTOMAKE_OPTIONS = serial-tests

TESTS_ENVIRONMENT = QUICKFIX_TEST_SRCDIR='$(abs_srcdir)' QUICKFIX_TEST_BUILDDIR='$(abs_builddir)'

TESTS = check.sh
