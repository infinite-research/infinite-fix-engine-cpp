noinst_PROGRAMS = ut

EXTRA_DIST = VerifyInfiniteCompleteFrameFixture.cmake InfiniteFrameAdapterInstalledConsumer.c $(abs_top_srcdir)/test/fixtures/infinite-frame-adapter-abi.v2.tsv

ut_LDADD = ../libquickfix.la

ut_SOURCES = \
	ut.cpp \
	catch_amalgamated.cpp \
	catch_amalgamated.hpp \
	DataDictionaryProviderTestCase.cpp \
	DataDictionaryTestCase.cpp \
	DictionaryTestCase.cpp \
	FieldBaseTestCase.cpp \
	FieldConvertorsTestCase.cpp \
	FieldMapTestCase.cpp \
	FieldTypesTestCase.cpp \
	FileLogTestCase.cpp \
	FileStoreFactoryTestCase.cpp \
	FileStoreTestCase.cpp \
	FileUtilitiesTestCase.cpp \
	GroupTestCase.cpp \
	HostDetailsProviderTestCase.cpp \
	HttpMessageTestCase.cpp \
	HttpParserTestCase.cpp \
	InfiniteCompleteFrameTestCase.cpp \
	InfiniteFrameAdapterTestCase.cpp \
	MemoryStoreTestCase.cpp \
	MessageSortersTestCase.cpp \
	MessagesTestCase.cpp \
	MessageStoreTestCase.h \
	MySQLStoreTestCase.cpp \
	NullStoreTestCase.cpp \
	OdbcStoreTestCase.cpp \
	ParserTestCase.cpp \
	PostgreSQLStoreTestCase.cpp \
	SessionFactoryTestCase.cpp \
	SessionIDTestCase.cpp \
	SessionSettingsTestCase.cpp \
	SessionStateTestCase.cpp \
	SessionTestCase.cpp \
	SettingsTestCase.cpp \
	SharedArrayTestCase.cpp \
	SocketAcceptorTestCase.cpp \
	SocketConnectionTestCase.cpp \
	SocketConnectorTestCase.cpp \
	SocketMonitorTestCase.cpp \
	SocketServerTestCase.cpp \
	StringUtilitiesTestCase.cpp \
	TestHelper.cpp \
	TestHelper.h \
	TimeRangeTestCase.cpp \
	UtcTimeOnlyTestCase.cpp \
	UtcTimeStampTestCase.cpp \
	UtilityTestCase.cpp
	
AM_CPPFLAGS = -DCATCH_AMALGAMATED_CUSTOM_MAIN \
	'-DINFINITE_COMPLETE_FRAME_FIXTURE_PATH="$(abs_top_srcdir)/test/fixtures/complete-frame-gate.v1.tsv"' \
	'-DINFINITE_FRAME_ADAPTER_ABI_FIXTURE_PATH="$(abs_top_srcdir)/test/fixtures/infinite-frame-adapter-abi.v2.tsv"' \
	-I$(top_srcdir)/src/C++ \
	-I..

check-local:
	@fixture="$(abs_top_srcdir)/test/fixtures/complete-frame-gate.v1.tsv"; \
	expected="60d02ae5f56f7d4c9e8239eb435b1cebd2295c57ea86bd68d49820a9d620996b"; \
	if command -v sha256sum >/dev/null 2>&1; then \
		actual=`sha256sum "$$fixture" | sed 's/[[:space:]].*//'`; \
	elif command -v shasum >/dev/null 2>&1; then \
		actual=`shasum -a 256 "$$fixture" | sed 's/[[:space:]].*//'`; \
	elif command -v openssl >/dev/null 2>&1; then \
		actual=`openssl dgst -sha256 "$$fixture" | sed 's/^.*= //'`; \
	else \
		echo "no SHA-256 tool available for complete-frame fixture verification" >&2; \
		exit 1; \
	fi; \
	test "$$actual" = "$$expected" || { echo "complete-frame fixture digest mismatch" >&2; exit 1; }
