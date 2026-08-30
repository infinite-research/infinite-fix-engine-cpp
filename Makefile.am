SUBDIRS = src bin spec test examples doc

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = \
	LICENSE \
	cmake/FindMySQL.cmake \
	cmake/PackageInfiniteAdapter.cmake \
	cmake/QuickfixPlatformSettings.cmake \
	cmake/QuickfixPrebuildSetup.cmake \
	cmake/VerifyInfiniteAdapterPackage.cmake \
	cmake/VerifyInfiniteDictionaryPins.cmake \
	cmake/VerifyInfiniteEmbeddedConfigure.cmake \
	cmake/VerifyInfiniteEmbeddedSymbols.cmake \
	licenses/double-conversion-BSD-3-Clause.txt \
	licenses/pugixml-MIT.txt \
	licenses/scope_guard-Unlicense.txt \
	tools/inf56-gcc13.Dockerfile

# Install pkgconf definition

pkgconfdir=$(libdir)/pkgconfig
pkgconf_DATA = quickfix.pc

generate:
	cd spec && bash generate.sh
