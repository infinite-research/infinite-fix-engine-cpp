SUBDIRS = atrun definitions cfg

EXTRA_DIST = *.rb fixtures/complete-frame-gate.v1.tsv fixtures/infinite-frame-adapter-abi.v2.tsv \
	AcceptanceHarnessTest.sh PerformanceHarnessTest.sh runat.sh runat.bat runat_threaded.sh \
	generate-test-pki.sh runpt.sh runut.sh runut.bat setup.sh

AUTOMAKE_OPTIONS = serial-tests

TESTS_ENVIRONMENT = QUICKFIX_TEST_SRCDIR='$(abs_srcdir)' QUICKFIX_TEST_BUILDDIR='$(abs_builddir)' \
	QUICKFIX_TEST_RUNTIMEDIR='$(abs_top_builddir)/test-runtime'

TESTS = check.sh PerformanceHarnessTest.sh
