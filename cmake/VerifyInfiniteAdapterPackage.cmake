cmake_minimum_required(VERSION 3.28)

set(_irfq_package_files
    InfiniteFrameAdapter.h
    LICENSES.txt
    infinite-frame-adapter-abi.v2.tsv
    libquickfix.a
    manifest.sha256)
set(_irfq_manifest_keys
    schema
    source_commit
    source_tree
    source_merge_base
    source_diff_sha256
    specification_commit
    specification_tree
    specification_bundle_sha256
    target
    build_image
    build_image_sha256
    cc
    cxx
    cmake
    ninja
    objcopy
    linker
    ar
    build_type
    have_ssl
    quickfix_shared_libs
    quickfix_examples
    quickfix_tests
    irfq_infinite_embed_dictionaries
    build_command
    transport_dictionary_id
    transport_dictionary_size
    transport_dictionary_sha256
    application_dictionary_id
    application_dictionary_size
    application_dictionary_sha256
    dictionary_provenance_sha256
    dictionary_license_inventory_sha256
    c_abi_exports
    c_abi_exports_sha256
    archive_path
    archive_size
    archive_sha256
    header_path
    header_size
    header_sha256
    abi_fixture_path
    abi_fixture_size
    abi_fixture_sha256
    licenses_path
    licenses_size
    licenses_sha256)

function(_irfq_require variable)
  if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
    message(FATAL_ERROR "${variable} is required")
  endif()
endfunction()

function(_irfq_require_regular_file path label)
  if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}" OR IS_SYMLINK "${path}")
    message(FATAL_ERROR "${label} must be a regular non-symlink file: ${path}")
  endif()
endfunction()

function(_irfq_require_equal actual expected label)
  if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "${label} mismatch: expected '${expected}', got '${actual}'")
  endif()
endfunction()

function(_irfq_require_hex value width label)
  string(LENGTH "${value}" _length)
  if(NOT _length EQUAL width OR NOT "${value}" MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "${label} must be exactly ${width} lowercase hexadecimal characters")
  endif()
endfunction()

function(_irfq_require_positive_decimal value label)
  if(NOT "${value}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "${label} must be a canonical positive decimal")
  endif()
endfunction()

function(_irfq_require_artifact package_path source_path size_value hash_value label)
  _irfq_require_regular_file("${package_path}" "packaged ${label}")
  _irfq_require_regular_file("${source_path}" "source ${label}")
  file(SIZE "${package_path}" _package_size)
  file(SIZE "${source_path}" _source_size)
  file(SHA256 "${package_path}" _package_hash)
  file(SHA256 "${source_path}" _source_hash)
  _irfq_require_positive_decimal("${size_value}" "${label} size")
  _irfq_require_hex("${hash_value}" 64 "${label} SHA-256")
  _irfq_require_equal("${_package_size}" "${size_value}" "${label} manifest size")
  _irfq_require_equal("${_package_hash}" "${hash_value}" "${label} manifest SHA-256")
  _irfq_require_equal("${_package_size}" "${_source_size}" "${label} source size")
  _irfq_require_equal("${_package_hash}" "${_source_hash}" "${label} source SHA-256")
endfunction()

function(_irfq_verify_source_provenance checkout release_base self_test_base allow_self_test_base)
  if(NOT release_base STREQUAL "386ce46e917ae494ab6e90b1be90fd421cdbe3f9")
    message(FATAL_ERROR "source release base must equal the pinned production release base")
  endif()
  if(allow_self_test_base)
    if(self_test_base STREQUAL "")
      message(FATAL_ERROR "package verifier self-test requires its explicit synthetic release base")
    endif()
    set(_base "${self_test_base}")
  else()
    if(NOT self_test_base STREQUAL "")
      message(FATAL_ERROR "production verification cannot override the pinned source release base")
    endif()
    set(_base "${release_base}")
  endif()
  if(NOT IS_ABSOLUTE "${checkout}" OR NOT IS_DIRECTORY "${checkout}" OR IS_SYMLINK "${checkout}")
    message(FATAL_ERROR "source checkout must be an absolute non-symlink directory")
  endif()
  file(REAL_PATH "${checkout}" _checkout)
  set(_clean_env
      "${CMAKE_COMMAND}" -E env
      --unset=GIT_DIR --unset=GIT_WORK_TREE --unset=GIT_COMMON_DIR --unset=GIT_INDEX_FILE
      --unset=GIT_OBJECT_DIRECTORY --unset=GIT_ALTERNATE_OBJECT_DIRECTORIES --unset=GIT_SHALLOW_FILE
      --unset=GIT_REPLACE_REF_BASE --unset=GIT_CONFIG_COUNT --unset=GIT_CONFIG_PARAMETERS
      --unset=GIT_CONFIG_SYSTEM --unset=GIT_EXTERNAL_DIFF --unset=GIT_DIFF_OPTS --unset=GIT_PAGER
      --unset=GIT_NAMESPACE --unset=GIT_ATTR_SOURCE
      GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null GIT_ATTR_NOSYSTEM=1
      GIT_NO_REPLACE_OBJECTS=1 GIT_NO_LAZY_FETCH=1 GIT_TERMINAL_PROMPT=0 GIT_OPTIONAL_LOCKS=0
      LC_ALL=C LANG=C)
  set(_fixed_config
      -c safe.directory=${_checkout}
      -c core.attributesFile=/dev/null -c core.quotePath=true -c core.bigFileThreshold=512m
      -c diff.external= -c diff.noprefix=false -c diff.mnemonicPrefix=false
      -c diff.srcPrefix=a/ -c diff.dstPrefix=b/ -c diff.algorithm=myers
      -c diff.indentHeuristic=true -c diff.compactionHeuristic=false
      -c diff.suppressBlankEmpty=false -c diff.interHunkContext=0 -c diff.submodule=short
      -c diff.orderFile=/dev/null -c color.ui=false)

  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}" rev-parse --show-toplevel
    RESULT_VARIABLE _top_result OUTPUT_VARIABLE _top ERROR_VARIABLE _top_error OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _top_result EQUAL 0)
    message(FATAL_ERROR "verifier cannot read source checkout Git metadata: ${_top_error}")
  endif()
  file(REAL_PATH "${_top}" _top)
  if(NOT _top STREQUAL _checkout)
    message(FATAL_ERROR "verified source checkout must equal the Git worktree root")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            rev-parse --path-format=absolute --git-path info/attributes
    RESULT_VARIABLE _attributes_result OUTPUT_VARIABLE _info_attributes ERROR_VARIABLE _attributes_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _attributes_result EQUAL 0)
    message(FATAL_ERROR "verifier could not resolve source info attributes: ${_attributes_error}")
  elseif(EXISTS "${_info_attributes}" OR IS_SYMLINK "${_info_attributes}")
    message(FATAL_ERROR "verified source checkout must not define Git info attributes: ${_info_attributes}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            rev-parse --is-shallow-repository
    RESULT_VARIABLE _shallow_result OUTPUT_VARIABLE _shallow ERROR_VARIABLE _shallow_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _shallow_result EQUAL 0 OR NOT _shallow STREQUAL "false")
    message(FATAL_ERROR "verified source checkout must have complete non-shallow metadata: ${_shallow_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            config --get-regexp "^(extensions\\.partialclone|remote\\..*\\.promisor)$"
    RESULT_VARIABLE _partial_result OUTPUT_VARIABLE _partial ERROR_VARIABLE _partial_error)
  if(_partial_result EQUAL 0)
    message(FATAL_ERROR "verified source checkout must not use promisor or partial-clone metadata: ${_partial}")
  elseif(NOT _partial_result EQUAL 1)
    message(FATAL_ERROR "verifier could not inspect partial-clone metadata: ${_partial_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            ls-files -v
    RESULT_VARIABLE _flags_result OUTPUT_VARIABLE _index_flags ERROR_VARIABLE _flags_error)
  if(NOT _flags_result EQUAL 0 OR _index_flags MATCHES "(^|\n)[^H] ")
    message(FATAL_ERROR "verified source index contains special tracked-file flags: ${_index_flags}${_flags_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            diff-index --quiet --cached HEAD --
    RESULT_VARIABLE _index_result ERROR_VARIABLE _index_error)
  if(NOT _index_result EQUAL 0)
    message(FATAL_ERROR "verified source index must equal HEAD: ${_index_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            diff --quiet --no-ext-diff --no-textconv --ignore-submodules=none HEAD --
    RESULT_VARIABLE _worktree_result ERROR_VARIABLE _worktree_error)
  if(NOT _worktree_result EQUAL 0)
    message(FATAL_ERROR "verified source tracked bytes must equal HEAD: ${_worktree_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            status --porcelain=v1 --untracked-files=all --ignored=matching --ignore-submodules=none
    RESULT_VARIABLE _status_result OUTPUT_VARIABLE _status ERROR_VARIABLE _status_error)
  if(NOT _status_result EQUAL 0 OR NOT _status STREQUAL "")
    message(FATAL_ERROR "verified source checkout must be clean: ${_status}${_status_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            rev-parse --verify "HEAD^{commit}"
    RESULT_VARIABLE _commit_result OUTPUT_VARIABLE _commit ERROR_VARIABLE _commit_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _commit_result EQUAL 0)
    message(FATAL_ERROR "verifier cannot read source HEAD commit: ${_commit_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            rev-parse --verify "HEAD^{tree}"
    RESULT_VARIABLE _tree_result OUTPUT_VARIABLE _tree ERROR_VARIABLE _tree_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _tree_result EQUAL 0)
    message(FATAL_ERROR "verifier cannot read source HEAD tree: ${_tree_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}" cat-file -e "${_base}^{commit}"
    RESULT_VARIABLE _base_result ERROR_VARIABLE _base_error)
  if(NOT _base_result EQUAL 0)
    message(FATAL_ERROR "verifier cannot read source release-base commit: ${_base_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            fsck --connectivity-only --no-dangling --no-reflogs --no-progress "${_commit}" "${_base}"
    RESULT_VARIABLE _connectivity_result ERROR_VARIABLE _connectivity_error)
  if(NOT _connectivity_result EQUAL 0)
    message(FATAL_ERROR "verified source has missing reachable Git objects: ${_connectivity_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            merge-base --is-ancestor "${_base}" "${_commit}"
    RESULT_VARIABLE _ancestor_result ERROR_VARIABLE _ancestor_error)
  if(NOT _ancestor_result EQUAL 0)
    message(FATAL_ERROR "verified source release base is not an ancestor of HEAD: ${_ancestor_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}"
            merge-base "${_commit}" "${_base}"
    RESULT_VARIABLE _merge_result OUTPUT_VARIABLE _merge_base ERROR_VARIABLE _merge_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _merge_result EQUAL 0 OR NOT _merge_base STREQUAL _base)
    message(FATAL_ERROR "verified source merge base must equal the pinned release base: ${_merge_base}${_merge_error}")
  endif()
  execute_process(
    COMMAND ${_clean_env} "${IRFQ_EXPECTED_GIT}" ${_fixed_config} -C "${_checkout}" --no-pager diff
            --no-ext-diff --no-textconv --no-color --binary --full-index --no-renames --ignore-submodules=none
            --src-prefix=a/ --dst-prefix=b/ --diff-algorithm=myers --indent-heuristic --unified=3
            "${_merge_base}" "${_commit}" --
    COMMAND "${IRFQ_EXPECTED_SHA256SUM}"
    RESULTS_VARIABLE _diff_results OUTPUT_VARIABLE _diff_output ERROR_VARIABLE _diff_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT "${_diff_results}" STREQUAL "0;0" OR NOT _diff_output MATCHES "^([0-9a-f]+)[ \t]+-$")
    message(FATAL_ERROR "verifier could not hash the canonical source diff: ${_diff_output}${_diff_error}")
  endif()
  set(_diff_sha256 "${CMAKE_MATCH_1}")
  _irfq_require_hex("${_diff_sha256}" 64 "derived source diff SHA-256")

  set(IRFQ_VERIFIED_SOURCE_COMMIT "${_commit}" PARENT_SCOPE)
  set(IRFQ_VERIFIED_SOURCE_TREE "${_tree}" PARENT_SCOPE)
  set(IRFQ_VERIFIED_SOURCE_MERGE_BASE "${_merge_base}" PARENT_SCOPE)
  set(IRFQ_VERIFIED_SOURCE_DIFF_SHA256 "${_diff_sha256}" PARENT_SCOPE)
endfunction()

if(SELF_TEST)
  _irfq_require(IRFQ_INFINITE_SELF_TEST_DIR)
  if(NOT IS_ABSOLUTE "${IRFQ_INFINITE_SELF_TEST_DIR}")
    message(FATAL_ERROR "IRFQ_INFINITE_SELF_TEST_DIR must be absolute")
  endif()
  cmake_path(NORMAL_PATH IRFQ_INFINITE_SELF_TEST_DIR OUTPUT_VARIABLE _irfq_self_root)
  cmake_path(GET _irfq_self_root FILENAME _irfq_self_leaf)
  if(NOT _irfq_self_leaf MATCHES "^irfq-package-self-test-[A-Za-z0-9_.-]+$")
    message(FATAL_ERROR "self-test directory must have an irfq-package-self-test-* leaf")
  endif()

  set(_irfq_package_script "${CMAKE_CURRENT_LIST_DIR}/PackageInfiniteAdapter.cmake")
  if(NOT EXISTS "${_irfq_package_script}")
    message(FATAL_ERROR "PackageInfiniteAdapter.cmake is missing")
  endif()

  if(EXISTS "${_irfq_self_root}")
    message(FATAL_ERROR "self-test directory must not already exist: ${_irfq_self_root}")
  endif()
  file(MAKE_DIRECTORY "${_irfq_self_root}/inputs")
  set(_irfq_self_build_root "${_irfq_self_root}/binary")
  file(MAKE_DIRECTORY "${_irfq_self_build_root}")
  file(WRITE "${_irfq_self_build_root}/.irfq-package-root"
       "irfq.infinite-fix-engine-package-root.v1\n")
  set(_irfq_input_dir "${_irfq_self_root}/inputs")
  set(_irfq_output_dir "${_irfq_self_build_root}/infinite-adapter-package")
  if(_irfq_output_dir STREQUAL "/build/infinite-adapter-package")
    message(FATAL_ERROR "package self-test output aliases the governed package output")
  endif()
  set(_irfq_self_quickfix_license "${CMAKE_CURRENT_LIST_DIR}/../LICENSE")
  set(_irfq_self_apache_license
      "${CMAKE_CURRENT_LIST_DIR}/../spec/infinite-rfq-1.0.0/licenses/FIXTradingCommunity-orchestrations-Apache-2.0.txt")
  set(_irfq_self_double_conversion_license
      "${CMAKE_CURRENT_LIST_DIR}/../licenses/double-conversion-BSD-3-Clause.txt")
  set(_irfq_self_pugixml_license "${CMAKE_CURRENT_LIST_DIR}/../licenses/pugixml-MIT.txt")
  set(_irfq_self_scope_guard_license "${CMAKE_CURRENT_LIST_DIR}/../licenses/scope_guard-Unlicense.txt")
  file(WRITE "${_irfq_input_dir}/InfiniteFrameAdapter.h" "#pragma once\n")
  file(WRITE "${_irfq_input_dir}/infinite-frame-adapter-abi.v2.tsv" "symbol\tabi\n")

  find_program(_irfq_self_cc NAMES gcc-13 gcc REQUIRED)
  find_program(_irfq_self_cxx NAMES g++-13 g++ REQUIRED)
  find_program(_irfq_self_ninja NAMES ninja REQUIRED)
  find_program(_irfq_self_objcopy NAMES objcopy REQUIRED)
  find_program(_irfq_self_linker NAMES ld REQUIRED)
  find_program(_irfq_self_ar NAMES ar REQUIRED)
  find_program(_irfq_self_ranlib NAMES ranlib REQUIRED)
  find_program(_irfq_self_readelf NAMES readelf REQUIRED)
  find_program(_irfq_self_git NAMES git REQUIRED)
  find_program(_irfq_self_sha256sum NAMES sha256sum REQUIRED)

  set(_irfq_self_source_checkout "${_irfq_self_root}/source")
  file(MAKE_DIRECTORY "${_irfq_self_source_checkout}")
  execute_process(COMMAND "${_irfq_self_git}" init -q --object-format=sha1 "${_irfq_self_source_checkout}"
                  COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" config user.name
                          "Infinite package self-test" COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" config user.email
                          self-test@example.invalid COMMAND_ERROR_IS_FATAL ANY)
  file(WRITE "${_irfq_self_source_checkout}/source.txt" "base\n")
  file(WRITE "${_irfq_self_source_checkout}/stable.txt" "stable\n")
  file(WRITE "${_irfq_self_source_checkout}/.gitignore" "ignored.txt\n")
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" add source.txt stable.txt .gitignore
                  COMMAND_ERROR_IS_FATAL ANY)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            GIT_AUTHOR_DATE=2000-01-01T00:00:00Z GIT_COMMITTER_DATE=2000-01-01T00:00:00Z
            "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" commit -q -m base
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(
    COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" rev-parse HEAD
    OUTPUT_VARIABLE _irfq_self_source_base OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  file(WRITE "${_irfq_self_source_checkout}/source.txt" "base\nhead\n")
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" add source.txt
                  COMMAND_ERROR_IS_FATAL ANY)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            GIT_AUTHOR_DATE=2000-01-02T00:00:00Z GIT_COMMITTER_DATE=2000-01-02T00:00:00Z
            "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" commit -q -m head
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(
    COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" rev-parse HEAD
    OUTPUT_VARIABLE _irfq_self_source_commit OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  execute_process(
    COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" rev-parse "HEAD^{tree}"
    OUTPUT_VARIABLE _irfq_self_source_tree OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  execute_process(
    COMMAND "${_irfq_self_git}" -c core.attributesFile=/dev/null -c diff.external= -c diff.noprefix=false
            -c diff.mnemonicPrefix=false -c diff.srcPrefix=a/ -c diff.dstPrefix=b/ -c diff.algorithm=myers
            -c diff.indentHeuristic=true -c core.quotePath=true -C "${_irfq_self_source_checkout}"
            --no-pager diff --no-ext-diff --binary --full-index --no-renames
            "${_irfq_self_source_base}" "${_irfq_self_source_commit}" --
    COMMAND "${_irfq_self_sha256sum}"
    OUTPUT_VARIABLE _irfq_self_source_diff_output OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(NOT _irfq_self_source_diff_output MATCHES "^([0-9a-f]+)[ \t]+-$")
    message(FATAL_ERROR "could not derive self-test source diff SHA-256: ${_irfq_self_source_diff_output}")
  endif()
  set(_irfq_self_source_diff "${CMAKE_MATCH_1}")
  _irfq_require_hex("${_irfq_self_source_diff}" 64 "self-test source diff SHA-256")
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" config diff.noprefix true
                  COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" config diff.algorithm histogram
                  COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" config diff.indentHeuristic false
                  COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" config diff.mnemonicPrefix true
                  COMMAND_ERROR_IS_FATAL ANY)
  file(WRITE "${_irfq_input_dir}/exports.c"
       "__attribute__((visibility(\"hidden\"))) const unsigned char irfq_infinite_embedded_dictionaries_v1 = 0;\n"
       "void irfq_infinite_abort_v2(void) {}\n"
       "void irfq_infinite_apply_committed_v2(void) {}\n"
       "void irfq_infinite_destroy_v2(void) {}\n"
       "void irfq_infinite_prepare_v2(void) {}\n"
       "void irfq_infinite_resume_v2(void) {}\n"
       "void irfq_infinite_scan_v2(void) {}\n"
       "void irfq_infinite_session_create_v2(void) {}\n")
  execute_process(
    COMMAND "${_irfq_self_cc}" -c "${_irfq_input_dir}/exports.c" -o "${_irfq_input_dir}/exports.o"
    RESULT_VARIABLE _irfq_compile_result
    ERROR_VARIABLE _irfq_compile_error)
  execute_process(
    COMMAND "${_irfq_self_ar}" rcs "${_irfq_input_dir}/libquickfix.a" "${_irfq_input_dir}/exports.o"
    RESULT_VARIABLE _irfq_ar_result
    ERROR_VARIABLE _irfq_ar_error)
  if(NOT _irfq_compile_result EQUAL 0 OR NOT _irfq_ar_result EQUAL 0)
    message(FATAL_ERROR "could not create self-test archive: ${_irfq_compile_error}${_irfq_ar_error}")
  endif()

  set(_irfq_self_fake_source_commit 1111111111111111111111111111111111111111)
  set(_irfq_self_fake_source_tree 2222222222222222222222222222222222222222)
  set(_irfq_self_fake_source_merge_base 3333333333333333333333333333333333333333)
  set(_irfq_self_fake_source_diff 4444444444444444444444444444444444444444444444444444444444444444)
  set(_irfq_self_spec_commit cb12d80aecd90f52b7fbcc009246dab640ba7a7b)
  set(_irfq_self_spec_tree bae719cd3d4001329f6efbe5e32d92be3549b5f9)
  set(_irfq_self_spec_bundle 4a35891736502ffe2413de8d25690748879b88e4938abf89a5bda835a3ad7ab9)
  set(_irfq_self_image irfq-task2e-cpp:2026-08-30)
  set(_irfq_self_image_hash c333c04c1f5ca0496016b43996fbdeed30c3b1b91f5e1581a418bf68a518139c)
  set(_irfq_self_build_command
      "cmake -S /src -B /build -G Ninja -DCMAKE_C_COMPILER=/usr/bin/gcc-13 -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 -DCMAKE_BUILD_TYPE=Release -DHAVE_SSL=OFF -DQUICKFIX_SHARED_LIBS=OFF -DQUICKFIX_EXAMPLES=OFF -DQUICKFIX_TESTS=ON -DIRFQ_INFINITE_EMBED_DICTIONARIES=ON -DQUICKFIX_LIB_OUTPUT_DIR=/build/out && cmake --build /build --parallel 2 --target infinite_adapter_package")

  set(_irfq_self_common
      "-DIRFQ_PACKAGE_SELF_TEST=ON"
      "-DIRFQ_PACKAGE_BINARY_DIR=${_irfq_self_build_root}"
      "-DIRFQ_PACKAGE_OUTPUT_DIR=${_irfq_output_dir}"
      "-DIRFQ_PACKAGE_ARCHIVE=${_irfq_input_dir}/libquickfix.a"
      "-DIRFQ_PACKAGE_HEADER=${_irfq_input_dir}/InfiniteFrameAdapter.h"
      "-DIRFQ_PACKAGE_ABI_FIXTURE=${_irfq_input_dir}/infinite-frame-adapter-abi.v2.tsv"
      "-DIRFQ_PACKAGE_QUICKFIX_LICENSE=${_irfq_self_quickfix_license}"
      "-DIRFQ_PACKAGE_APACHE_LICENSE=${_irfq_self_apache_license}"
      "-DIRFQ_PACKAGE_DOUBLE_CONVERSION_LICENSE=${_irfq_self_double_conversion_license}"
      "-DIRFQ_PACKAGE_PUGIXML_LICENSE=${_irfq_self_pugixml_license}"
      "-DIRFQ_PACKAGE_SCOPE_GUARD_LICENSE=${_irfq_self_scope_guard_license}"
      "-DIRFQ_PACKAGE_SOURCE_CHECKOUT=${_irfq_self_source_checkout}"
      "-DIRFQ_PACKAGE_SOURCE_RELEASE_BASE=386ce46e917ae494ab6e90b1be90fd421cdbe3f9"
      "-DIRFQ_PACKAGE_SELF_TEST_RELEASE_BASE=${_irfq_self_source_base}"
      "-DIRFQ_PACKAGE_SOURCE_COMMIT=${_irfq_self_fake_source_commit}"
      "-DIRFQ_PACKAGE_SOURCE_TREE=${_irfq_self_fake_source_tree}"
      "-DIRFQ_PACKAGE_SOURCE_MERGE_BASE=${_irfq_self_fake_source_merge_base}"
      "-DIRFQ_PACKAGE_SOURCE_DIFF_SHA256=${_irfq_self_fake_source_diff}"
      "-DIRFQ_PACKAGE_SPECIFICATION_COMMIT=${_irfq_self_spec_commit}"
      "-DIRFQ_PACKAGE_SPECIFICATION_TREE=${_irfq_self_spec_tree}"
      "-DIRFQ_PACKAGE_SPECIFICATION_BUNDLE_SHA256=${_irfq_self_spec_bundle}"
      "-DIRFQ_PACKAGE_BUILD_IMAGE=${_irfq_self_image}"
      "-DIRFQ_PACKAGE_BUILD_IMAGE_SHA256=${_irfq_self_image_hash}"
      "-DIRFQ_PACKAGE_BUILD_COMMAND=${_irfq_self_build_command}"
      "-DIRFQ_PACKAGE_C_COMPILER=${_irfq_self_cc}"
      "-DIRFQ_PACKAGE_CXX_COMPILER=${_irfq_self_cxx}"
      "-DIRFQ_PACKAGE_SOURCE_DIR=/src"
      "-DIRFQ_PACKAGE_LIB_OUTPUT_DIR=/build/out"
      "-DIRFQ_PACKAGE_ARCHIVE_OUTPUT_DIR=/build/out"
      "-DIRFQ_PACKAGE_ARCHIVE_OUTPUT_DIR_RELEASE=/build/out"
      "-DIRFQ_PACKAGE_GENERATOR=Ninja"
      "-DIRFQ_PACKAGE_NINJA=${_irfq_self_ninja}"
      "-DIRFQ_PACKAGE_OBJCOPY=${_irfq_self_objcopy}"
      "-DIRFQ_PACKAGE_LINKER=${_irfq_self_linker}"
      "-DIRFQ_PACKAGE_AR=${_irfq_self_ar}"
      "-DIRFQ_PACKAGE_RANLIB=${_irfq_self_ranlib}"
      "-DIRFQ_PACKAGE_GIT=${_irfq_self_git}"
      "-DIRFQ_PACKAGE_SHA256SUM=${_irfq_self_sha256sum}"
      "-DIRFQ_PACKAGE_READELF=${_irfq_self_readelf}"
      "-DIRFQ_PACKAGE_BUILD_TYPE=Release"
      "-DIRFQ_PACKAGE_HAVE_SSL=OFF"
      "-DIRFQ_PACKAGE_QUICKFIX_SHARED_LIBS=OFF"
      "-DIRFQ_PACKAGE_QUICKFIX_EXAMPLES=OFF"
      "-DIRFQ_PACKAGE_QUICKFIX_TESTS=ON"
      "-DIRFQ_PACKAGE_EMBED_DICTIONARIES=ON"
      "-DIRFQ_PACKAGE_HAVE_MYSQL=OFF"
      "-DIRFQ_PACKAGE_HAVE_POSTGRESQL=OFF"
      "-DIRFQ_PACKAGE_HAVE_ODBC=OFF"
      "-DIRFQ_PACKAGE_HAVE_PYTHON3=OFF"
      "-DIRFQ_PACKAGE_POSITION_INDEPENDENT_CODE="
      "-DIRFQ_PACKAGE_INTERPROCEDURAL_OPTIMIZATION="
      "-DIRFQ_PACKAGE_INTERPROCEDURAL_OPTIMIZATION_RELEASE="
      "-DIRFQ_PACKAGE_UNITY_BUILD="
      "-DIRFQ_PACKAGE_CXX_EXTENSIONS="
      "-DIRFQ_PACKAGE_TOOLCHAIN_FILE="
      "-DIRFQ_PACKAGE_SYSROOT="
      "-DIRFQ_PACKAGE_C_COMPILER_LAUNCHER="
      "-DIRFQ_PACKAGE_CXX_COMPILER_LAUNCHER="
      "-DIRFQ_PACKAGE_C_FLAGS="
      "-DIRFQ_PACKAGE_C_FLAGS_RELEASE=-O3 -DNDEBUG"
      "-DIRFQ_PACKAGE_CXX_FLAGS="
      "-DIRFQ_PACKAGE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG"
      "-DIRFQ_PACKAGE_STATIC_LINKER_FLAGS="
      "-DIRFQ_PACKAGE_STATIC_LINKER_FLAGS_RELEASE="
      "-DIRFQ_PACKAGE_VERIFY_SCRIPT=${CMAKE_CURRENT_LIST_FILE}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_self_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_result
    OUTPUT_VARIABLE _irfq_output
    ERROR_VARIABLE _irfq_error)
  if(NOT _irfq_result EQUAL 0)
    message(FATAL_ERROR "positive package case failed:\n${_irfq_output}${_irfq_error}")
  endif()

  file(READ "${_irfq_output_dir}/manifest.sha256" _irfq_first_manifest)
  if(NOT _irfq_first_manifest MATCHES "source_commit=${_irfq_self_source_commit}\n"
     OR NOT _irfq_first_manifest MATCHES "source_tree=${_irfq_self_source_tree}\n"
     OR NOT _irfq_first_manifest MATCHES "source_merge_base=${_irfq_self_source_base}\n"
     OR NOT _irfq_first_manifest MATCHES "source_diff_sha256=${_irfq_self_source_diff}\n")
    message(FATAL_ERROR "packager trusted caller-supplied source identity claims")
  endif()

  set(_irfq_alternate_claims ${_irfq_self_common})
  list(TRANSFORM _irfq_alternate_claims REPLACE
       "^-DIRFQ_PACKAGE_SOURCE_COMMIT=.*" "-DIRFQ_PACKAGE_SOURCE_COMMIT=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
  list(TRANSFORM _irfq_alternate_claims REPLACE
       "^-DIRFQ_PACKAGE_SOURCE_TREE=.*" "-DIRFQ_PACKAGE_SOURCE_TREE=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
  list(TRANSFORM _irfq_alternate_claims REPLACE
       "^-DIRFQ_PACKAGE_SOURCE_MERGE_BASE=.*"
       "-DIRFQ_PACKAGE_SOURCE_MERGE_BASE=cccccccccccccccccccccccccccccccccccccccc")
  list(TRANSFORM _irfq_alternate_claims REPLACE
       "^-DIRFQ_PACKAGE_SOURCE_DIFF_SHA256=.*"
       "-DIRFQ_PACKAGE_SOURCE_DIFF_SHA256=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_alternate_claims} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_alternate_claims_result
    OUTPUT_VARIABLE _irfq_alternate_claims_output
    ERROR_VARIABLE _irfq_alternate_claims_error)
  if(NOT _irfq_alternate_claims_result EQUAL 0)
    message(FATAL_ERROR
            "package rejected irrelevant legacy source claims:\n${_irfq_alternate_claims_output}${_irfq_alternate_claims_error}")
  endif()
  file(READ "${_irfq_output_dir}/manifest.sha256" _irfq_second_manifest)
  if(NOT _irfq_second_manifest STREQUAL _irfq_first_manifest)
    message(FATAL_ERROR "legacy source claims changed package output")
  endif()

  set(_irfq_missing_identity ${_irfq_self_common})
  list(FILTER _irfq_missing_identity EXCLUDE REGEX "^-DIRFQ_PACKAGE_SOURCE_CHECKOUT=")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_missing_identity} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_missing_identity_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_missing_identity_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a missing source checkout")
  endif()

  set(_irfq_source_escape "${_irfq_self_root}-source-escape")
  execute_process(
    COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" worktree add --detach --quiet
            "${_irfq_source_escape}" "${_irfq_self_source_commit}"
    COMMAND_ERROR_IS_FATAL ANY)
  set(_irfq_source_escape_common ${_irfq_self_common})
  list(TRANSFORM _irfq_source_escape_common REPLACE
       "^-DIRFQ_PACKAGE_SOURCE_CHECKOUT=.*" "-DIRFQ_PACKAGE_SOURCE_CHECKOUT=${_irfq_source_escape}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_source_escape_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_source_escape_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_source_escape_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a self-test source outside its isolated root")
  endif()

  file(APPEND "${_irfq_self_source_checkout}/source.txt" "staged\n")
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" add source.txt
                  COMMAND_ERROR_IS_FATAL ANY)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_self_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_dirty_staged_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_dirty_staged_result EQUAL 0)
    message(FATAL_ERROR "packager accepted staged tracked source state")
  endif()
  file(WRITE "${_irfq_self_source_checkout}/source.txt" "base\nhead\n")
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" add source.txt
                  COMMAND_ERROR_IS_FATAL ANY)

  file(APPEND "${_irfq_self_source_checkout}/source.txt" "unstaged\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_self_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_dirty_tracked_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_dirty_tracked_result EQUAL 0)
    message(FATAL_ERROR "packager accepted dirty tracked source state")
  endif()
  file(WRITE "${_irfq_self_source_checkout}/source.txt" "base\nhead\n")

  file(WRITE "${_irfq_self_source_checkout}/untracked.txt" "dirty\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_self_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_dirty_untracked_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_dirty_untracked_result EQUAL 0)
    message(FATAL_ERROR "packager accepted untracked source state")
  endif()
  file(REMOVE "${_irfq_self_source_checkout}/untracked.txt")

  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}"
                          update-index --assume-unchanged source.txt COMMAND_ERROR_IS_FATAL ANY)
  file(APPEND "${_irfq_self_source_checkout}/source.txt" "hidden drift\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_self_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_assume_unchanged_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_assume_unchanged_result EQUAL 0)
    message(FATAL_ERROR "packager accepted assume-unchanged tracked drift")
  endif()
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}"
                          update-index --no-assume-unchanged source.txt COMMAND_ERROR_IS_FATAL ANY)
  file(WRITE "${_irfq_self_source_checkout}/source.txt" "base\nhead\n")

  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}"
                          update-index --skip-worktree source.txt COMMAND_ERROR_IS_FATAL ANY)
  file(APPEND "${_irfq_self_source_checkout}/source.txt" "hidden drift\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_self_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_skip_worktree_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_skip_worktree_result EQUAL 0)
    message(FATAL_ERROR "packager accepted skip-worktree tracked drift")
  endif()
  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}"
                          update-index --no-skip-worktree source.txt COMMAND_ERROR_IS_FATAL ANY)
  file(WRITE "${_irfq_self_source_checkout}/source.txt" "base\nhead\n")

  file(WRITE "${_irfq_self_source_checkout}/ignored.txt" "hidden\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_self_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_ignored_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_ignored_result EQUAL 0)
    message(FATAL_ERROR "packager accepted an ignored untracked file")
  endif()
  file(REMOVE "${_irfq_self_source_checkout}/ignored.txt")

  file(WRITE "${_irfq_self_source_checkout}/.git/info/attributes" "*.txt -diff\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_self_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_info_attributes_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_info_attributes_result EQUAL 0)
    message(FATAL_ERROR "packager accepted checkout-local Git info attributes")
  endif()
  file(REMOVE "${_irfq_self_source_checkout}/.git/info/attributes")

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GIT_ATTR_SOURCE=${_irfq_self_source_base}
            "${CMAKE_COMMAND}" ${_irfq_self_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_attr_source_result OUTPUT_QUIET ERROR_QUIET)
  if(NOT _irfq_attr_source_result EQUAL 0)
    message(FATAL_ERROR "packager did not neutralize GIT_ATTR_SOURCE")
  endif()
  file(READ "${_irfq_output_dir}/manifest.sha256" _irfq_attr_source_manifest)
  if(NOT _irfq_attr_source_manifest STREQUAL _irfq_first_manifest)
    message(FATAL_ERROR "GIT_ATTR_SOURCE changed package provenance")
  endif()

  execute_process(
    COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" rev-parse HEAD:stable.txt
    OUTPUT_VARIABLE _irfq_stable_blob OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  execute_process(
    COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}"
            rev-parse --path-format=absolute --git-path objects
    OUTPUT_VARIABLE _irfq_object_dir OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  string(SUBSTRING "${_irfq_stable_blob}" 0 2 _irfq_object_prefix)
  string(SUBSTRING "${_irfq_stable_blob}" 2 -1 _irfq_object_suffix)
  set(_irfq_stable_object "${_irfq_object_dir}/${_irfq_object_prefix}/${_irfq_object_suffix}")
  file(RENAME "${_irfq_stable_object}" "${_irfq_stable_object}.missing" RESULT _irfq_hide_object_result)
  if(NOT _irfq_hide_object_result STREQUAL "0")
    message(FATAL_ERROR "could not hide reachable self-test blob: ${_irfq_hide_object_result}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_self_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_missing_reachable_result OUTPUT_QUIET ERROR_QUIET)
  file(RENAME "${_irfq_stable_object}.missing" "${_irfq_stable_object}" RESULT _irfq_restore_object_result)
  if(NOT _irfq_restore_object_result STREQUAL "0")
    message(FATAL_ERROR "could not restore reachable self-test blob: ${_irfq_restore_object_result}")
  elseif(_irfq_missing_reachable_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a missing reachable source blob")
  endif()

  set(_irfq_missing_base ${_irfq_self_common})
  list(TRANSFORM _irfq_missing_base REPLACE
       "^-DIRFQ_PACKAGE_SELF_TEST_RELEASE_BASE=.*"
       "-DIRFQ_PACKAGE_SELF_TEST_RELEASE_BASE=0000000000000000000000000000000000000000")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_missing_base} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_missing_base_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_missing_base_result EQUAL 0)
    message(FATAL_ERROR "packager accepted an unreadable release-base object")
  endif()

  set(_irfq_wrong_pinned_base ${_irfq_self_common})
  list(TRANSFORM _irfq_wrong_pinned_base REPLACE
       "^-DIRFQ_PACKAGE_SOURCE_RELEASE_BASE=.*"
       "-DIRFQ_PACKAGE_SOURCE_RELEASE_BASE=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_wrong_pinned_base} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_wrong_pinned_base_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_wrong_pinned_base_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a caller-selected production release base")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            GIT_AUTHOR_NAME=Infinite-package-self-test GIT_AUTHOR_EMAIL=self-test@example.invalid
            GIT_COMMITTER_NAME=Infinite-package-self-test GIT_COMMITTER_EMAIL=self-test@example.invalid
            GIT_AUTHOR_DATE=2000-01-03T00:00:00Z GIT_COMMITTER_DATE=2000-01-03T00:00:00Z
            "${_irfq_self_git}" -C "${_irfq_self_source_checkout}" commit-tree "HEAD^{tree}" -m unrelated
    OUTPUT_VARIABLE _irfq_self_unrelated_base OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  set(_irfq_nonancestor_base ${_irfq_self_common})
  list(TRANSFORM _irfq_nonancestor_base REPLACE
       "^-DIRFQ_PACKAGE_SELF_TEST_RELEASE_BASE=.*"
       "-DIRFQ_PACKAGE_SELF_TEST_RELEASE_BASE=${_irfq_self_unrelated_base}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_nonancestor_base} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_nonancestor_base_result OUTPUT_QUIET ERROR_QUIET)
  if(_irfq_nonancestor_base_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a nonancestor release base")
  endif()

  execute_process(
    COMMAND "${_irfq_self_ar}" rcT "${_irfq_input_dir}/thin.a" "${_irfq_input_dir}/exports.o"
    RESULT_VARIABLE _irfq_thin_ar_result
    ERROR_VARIABLE _irfq_thin_ar_error)
  if(NOT _irfq_thin_ar_result EQUAL 0)
    message(FATAL_ERROR "could not create thin archive: ${_irfq_thin_ar_error}")
  endif()
  set(_irfq_thin_common ${_irfq_self_common})
  list(TRANSFORM _irfq_thin_common REPLACE
       "^-DIRFQ_PACKAGE_ARCHIVE=.*" "-DIRFQ_PACKAGE_ARCHIVE=${_irfq_input_dir}/thin.a")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_thin_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_thin_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_thin_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a thin archive")
  endif()

  file(WRITE "${_irfq_input_dir}/extra-export.c" "void irfq_infinite_unapproved_v2(void) {}\n")
  execute_process(
    COMMAND "${_irfq_self_cc}" -c "${_irfq_input_dir}/extra-export.c" -o "${_irfq_input_dir}/extra-export.o"
    RESULT_VARIABLE _irfq_extra_compile_result
    ERROR_VARIABLE _irfq_extra_compile_error)
  file(COPY_FILE "${_irfq_input_dir}/libquickfix.a" "${_irfq_input_dir}/extra-export.a")
  execute_process(
    COMMAND "${_irfq_self_ar}" rcs "${_irfq_input_dir}/extra-export.a" "${_irfq_input_dir}/extra-export.o"
    RESULT_VARIABLE _irfq_extra_ar_result
    ERROR_VARIABLE _irfq_extra_ar_error)
  if(NOT _irfq_extra_compile_result EQUAL 0 OR NOT _irfq_extra_ar_result EQUAL 0)
    message(FATAL_ERROR "could not create unexpected-export archive: ${_irfq_extra_compile_error}${_irfq_extra_ar_error}")
  endif()
  set(_irfq_extra_export_common ${_irfq_self_common})
  list(TRANSFORM _irfq_extra_export_common REPLACE
       "^-DIRFQ_PACKAGE_ARCHIVE=.*" "-DIRFQ_PACKAGE_ARCHIVE=${_irfq_input_dir}/extra-export.a")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_extra_export_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_extra_export_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_extra_export_result EQUAL 0)
    message(FATAL_ERROR "packager accepted an unexpected C ABI export")
  endif()

  file(WRITE "${_irfq_input_dir}/weak-export.c"
       "__attribute__((weak)) void irfq_infinite_unapproved_v2(void) {}\n")
  execute_process(
    COMMAND "${_irfq_self_cc}" -c "${_irfq_input_dir}/weak-export.c" -o "${_irfq_input_dir}/weak-export.o"
    RESULT_VARIABLE _irfq_weak_compile_result
    ERROR_VARIABLE _irfq_weak_compile_error)
  file(COPY_FILE "${_irfq_input_dir}/libquickfix.a" "${_irfq_input_dir}/weak-export.a")
  execute_process(
    COMMAND "${_irfq_self_ar}" rcs "${_irfq_input_dir}/weak-export.a" "${_irfq_input_dir}/weak-export.o"
    RESULT_VARIABLE _irfq_weak_ar_result
    ERROR_VARIABLE _irfq_weak_ar_error)
  if(NOT _irfq_weak_compile_result EQUAL 0 OR NOT _irfq_weak_ar_result EQUAL 0)
    message(FATAL_ERROR "could not create weak-export archive: ${_irfq_weak_compile_error}${_irfq_weak_ar_error}")
  endif()
  set(_irfq_weak_export_common ${_irfq_self_common})
  list(TRANSFORM _irfq_weak_export_common REPLACE
       "^-DIRFQ_PACKAGE_ARCHIVE=.*" "-DIRFQ_PACKAGE_ARCHIVE=${_irfq_input_dir}/weak-export.a")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_weak_export_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_weak_export_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_weak_export_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a weak unexpected C ABI export")
  endif()

  file(WRITE "${_irfq_input_dir}/suffixed-export.c"
       "void irfq_extra(void) __asm__(\"irfq_infinite_unapproved_v2.extra\");\n"
       "void irfq_extra(void) {}\n")
  execute_process(
    COMMAND "${_irfq_self_cc}" -c "${_irfq_input_dir}/suffixed-export.c" -o "${_irfq_input_dir}/suffixed-export.o"
    RESULT_VARIABLE _irfq_suffixed_compile_result
    ERROR_VARIABLE _irfq_suffixed_compile_error)
  file(COPY_FILE "${_irfq_input_dir}/libquickfix.a" "${_irfq_input_dir}/suffixed-export.a")
  execute_process(
    COMMAND "${_irfq_self_ar}" rcs "${_irfq_input_dir}/suffixed-export.a" "${_irfq_input_dir}/suffixed-export.o"
    RESULT_VARIABLE _irfq_suffixed_ar_result
    ERROR_VARIABLE _irfq_suffixed_ar_error)
  if(NOT _irfq_suffixed_compile_result EQUAL 0 OR NOT _irfq_suffixed_ar_result EQUAL 0)
    message(FATAL_ERROR
            "could not create suffixed-export archive: ${_irfq_suffixed_compile_error}${_irfq_suffixed_ar_error}")
  endif()
  set(_irfq_suffixed_export_common ${_irfq_self_common})
  list(TRANSFORM _irfq_suffixed_export_common REPLACE
       "^-DIRFQ_PACKAGE_ARCHIVE=.*" "-DIRFQ_PACKAGE_ARCHIVE=${_irfq_input_dir}/suffixed-export.a")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_suffixed_export_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_suffixed_export_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_suffixed_export_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a suffixed unexpected C ABI export")
  endif()

  file(WRITE "${_irfq_input_dir}/space-export.S"
       ".globl \"irfq_infinite_unapproved_v2 extra\"\n"
       ".type \"irfq_infinite_unapproved_v2 extra\", @function\n"
       "\"irfq_infinite_unapproved_v2 extra\":\n"
       "  ret\n")
  execute_process(
    COMMAND "${_irfq_self_cc}" -c "${_irfq_input_dir}/space-export.S" -o "${_irfq_input_dir}/space-export.o"
    RESULT_VARIABLE _irfq_space_compile_result
    ERROR_VARIABLE _irfq_space_compile_error)
  file(COPY_FILE "${_irfq_input_dir}/libquickfix.a" "${_irfq_input_dir}/space-export.a")
  execute_process(
    COMMAND "${_irfq_self_ar}" rcs "${_irfq_input_dir}/space-export.a" "${_irfq_input_dir}/space-export.o"
    RESULT_VARIABLE _irfq_space_ar_result
    ERROR_VARIABLE _irfq_space_ar_error)
  if(NOT _irfq_space_compile_result EQUAL 0 OR NOT _irfq_space_ar_result EQUAL 0)
    message(FATAL_ERROR "could not create space-export archive: ${_irfq_space_compile_error}${_irfq_space_ar_error}")
  endif()
  set(_irfq_space_export_common ${_irfq_self_common})
  list(TRANSFORM _irfq_space_export_common REPLACE
       "^-DIRFQ_PACKAGE_ARCHIVE=.*" "-DIRFQ_PACKAGE_ARCHIVE=${_irfq_input_dir}/space-export.a")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_space_export_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_space_export_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_space_export_result EQUAL 0)
    message(FATAL_ERROR "packager accepted an unexpected C ABI export containing whitespace")
  endif()

  file(WRITE "${_irfq_input_dir}/semicolon-exports.S"
       ".globl \"irfq_infinite_abort_v2;extra\"\n.type \"irfq_infinite_abort_v2;extra\", @function\n\"irfq_infinite_abort_v2;extra\":\n  ret\n"
       ".globl \"irfq_infinite_apply_committed_v2;extra\"\n.type \"irfq_infinite_apply_committed_v2;extra\", @function\n\"irfq_infinite_apply_committed_v2;extra\":\n  ret\n"
       ".globl \"irfq_infinite_destroy_v2;extra\"\n.type \"irfq_infinite_destroy_v2;extra\", @function\n\"irfq_infinite_destroy_v2;extra\":\n  ret\n"
       ".globl \"irfq_infinite_prepare_v2;extra\"\n.type \"irfq_infinite_prepare_v2;extra\", @function\n\"irfq_infinite_prepare_v2;extra\":\n  ret\n"
       ".globl \"irfq_infinite_resume_v2;extra\"\n.type \"irfq_infinite_resume_v2;extra\", @function\n\"irfq_infinite_resume_v2;extra\":\n  ret\n"
       ".globl \"irfq_infinite_scan_v2;extra\"\n.type \"irfq_infinite_scan_v2;extra\", @function\n\"irfq_infinite_scan_v2;extra\":\n  ret\n"
       ".globl \"irfq_infinite_session_create_v2;extra\"\n.type \"irfq_infinite_session_create_v2;extra\", @function\n\"irfq_infinite_session_create_v2;extra\":\n  ret\n")
  execute_process(
    COMMAND "${_irfq_self_cc}" -c "${_irfq_input_dir}/semicolon-exports.S"
            -o "${_irfq_input_dir}/semicolon-exports.o"
    RESULT_VARIABLE _irfq_semicolon_compile_result
    ERROR_VARIABLE _irfq_semicolon_compile_error)
  execute_process(
    COMMAND "${_irfq_self_ar}" rcs "${_irfq_input_dir}/semicolon-exports.a"
            "${_irfq_input_dir}/semicolon-exports.o"
    RESULT_VARIABLE _irfq_semicolon_ar_result
    ERROR_VARIABLE _irfq_semicolon_ar_error)
  if(NOT _irfq_semicolon_compile_result EQUAL 0 OR NOT _irfq_semicolon_ar_result EQUAL 0)
    message(FATAL_ERROR
            "could not create semicolon-export archive: ${_irfq_semicolon_compile_error}${_irfq_semicolon_ar_error}")
  endif()
  set(_irfq_semicolon_export_common ${_irfq_self_common})
  list(TRANSFORM _irfq_semicolon_export_common REPLACE
       "^-DIRFQ_PACKAGE_ARCHIVE=.*" "-DIRFQ_PACKAGE_ARCHIVE=${_irfq_input_dir}/semicolon-exports.a")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_semicolon_export_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_semicolon_export_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_semicolon_export_result EQUAL 0)
    message(FATAL_ERROR "packager accepted semicolon-suffixed C ABI exports")
  endif()

  file(COPY_FILE "${_irfq_input_dir}/libquickfix.a" "${_irfq_input_dir}/trailing-space-export.a")
  execute_process(
    COMMAND "${_irfq_self_objcopy}"
            "--redefine-sym=irfq_infinite_abort_v2=irfq_infinite_abort_v2 "
            "${_irfq_input_dir}/trailing-space-export.a"
    RESULT_VARIABLE _irfq_trailing_space_result
    ERROR_VARIABLE _irfq_trailing_space_error)
  if(NOT _irfq_trailing_space_result EQUAL 0)
    message(FATAL_ERROR "could not create trailing-space export: ${_irfq_trailing_space_error}")
  endif()
  set(_irfq_trailing_space_common ${_irfq_self_common})
  list(TRANSFORM _irfq_trailing_space_common REPLACE
       "^-DIRFQ_PACKAGE_ARCHIVE=.*" "-DIRFQ_PACKAGE_ARCHIVE=${_irfq_input_dir}/trailing-space-export.a")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_trailing_space_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_trailing_space_package_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_trailing_space_package_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a trailing-space C ABI export")
  endif()

  file(WRITE "${_irfq_input_dir}/object-export.c" "const int irfq_infinite_abort_v2 = 0;\n")
  execute_process(
    COMMAND "${_irfq_self_cc}" -c "${_irfq_input_dir}/object-export.c" -o "${_irfq_input_dir}/object-export.o"
    RESULT_VARIABLE _irfq_object_compile_result
    ERROR_VARIABLE _irfq_object_compile_error)
  file(COPY_FILE "${_irfq_input_dir}/libquickfix.a" "${_irfq_input_dir}/object-export.a")
  execute_process(
    COMMAND "${_irfq_self_ar}" rcs "${_irfq_input_dir}/object-export.a" "${_irfq_input_dir}/object-export.o"
    RESULT_VARIABLE _irfq_object_ar_result
    ERROR_VARIABLE _irfq_object_ar_error)
  if(NOT _irfq_object_compile_result EQUAL 0 OR NOT _irfq_object_ar_result EQUAL 0)
    message(FATAL_ERROR "could not create object-export archive: ${_irfq_object_compile_error}${_irfq_object_ar_error}")
  endif()
  set(_irfq_object_export_common ${_irfq_self_common})
  list(TRANSFORM _irfq_object_export_common REPLACE
       "^-DIRFQ_PACKAGE_ARCHIVE=.*" "-DIRFQ_PACKAGE_ARCHIVE=${_irfq_input_dir}/object-export.a")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_object_export_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_object_export_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_object_export_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a public data object as a C ABI function")
  endif()

  file(WRITE "${_irfq_input_dir}/pugixml-license-drift.txt" "drift\n")
  set(_irfq_license_drift_common ${_irfq_self_common})
  list(TRANSFORM _irfq_license_drift_common REPLACE
       "^-DIRFQ_PACKAGE_PUGIXML_LICENSE=.*"
       "-DIRFQ_PACKAGE_PUGIXML_LICENSE=${_irfq_input_dir}/pugixml-license-drift.txt")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_license_drift_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_license_drift_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_license_drift_result EQUAL 0)
    message(FATAL_ERROR "packager accepted third-party license drift")
  endif()

  set(_irfq_contradictory_command_common ${_irfq_self_common})
  list(TRANSFORM _irfq_contradictory_command_common REPLACE
       "^-DIRFQ_PACKAGE_BUILD_COMMAND=.*"
       "-DIRFQ_PACKAGE_BUILD_COMMAND=${_irfq_self_build_command} -DHAVE_SSL=ON")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_contradictory_command_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_contradictory_command_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_contradictory_command_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a contradictory build command")
  endif()

  set(_irfq_extra_flags_common ${_irfq_self_common})
  list(TRANSFORM _irfq_extra_flags_common REPLACE
       "^-DIRFQ_PACKAGE_CXX_FLAGS=$" "-DIRFQ_PACKAGE_CXX_FLAGS=-funroll-loops")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_extra_flags_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_extra_flags_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_extra_flags_result EQUAL 0)
    message(FATAL_ERROR "packager accepted non-governed compiler flags")
  endif()

  set(_irfq_pic_common ${_irfq_self_common})
  list(TRANSFORM _irfq_pic_common REPLACE
       "^-DIRFQ_PACKAGE_POSITION_INDEPENDENT_CODE=$"
       "-DIRFQ_PACKAGE_POSITION_INDEPENDENT_CODE=ON")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_pic_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_pic_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_pic_result EQUAL 0)
    message(FATAL_ERROR "packager accepted non-governed position-independent code")
  endif()

  set(_irfq_archive_output_common ${_irfq_self_common})
  list(TRANSFORM _irfq_archive_output_common REPLACE
       "^-DIRFQ_PACKAGE_ARCHIVE_OUTPUT_DIR=/build/out$"
       "-DIRFQ_PACKAGE_ARCHIVE_OUTPUT_DIR=/build/alternate")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_archive_output_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_archive_output_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_archive_output_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a non-governed archive output directory")
  endif()

  set(_irfq_ranlib_path_common ${_irfq_self_common})
  list(TRANSFORM _irfq_ranlib_path_common REPLACE
       "^-DIRFQ_PACKAGE_RANLIB=.*" "-DIRFQ_PACKAGE_RANLIB=/bin/false")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_irfq_ranlib_path_common} -P "${_irfq_package_script}"
    RESULT_VARIABLE _irfq_ranlib_path_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_irfq_ranlib_path_result EQUAL 0)
    message(FATAL_ERROR "packager accepted a non-governed ranlib path")
  endif()

  set(_irfq_self_verify_common
      "-DIRFQ_EXPECTED_ARCHIVE=${_irfq_input_dir}/libquickfix.a"
      "-DIRFQ_EXPECTED_HEADER=${_irfq_input_dir}/InfiniteFrameAdapter.h"
      "-DIRFQ_EXPECTED_ABI_FIXTURE=${_irfq_input_dir}/infinite-frame-adapter-abi.v2.tsv"
      "-DIRFQ_EXPECTED_QUICKFIX_LICENSE=${_irfq_self_quickfix_license}"
      "-DIRFQ_EXPECTED_APACHE_LICENSE=${_irfq_self_apache_license}"
      "-DIRFQ_EXPECTED_DOUBLE_CONVERSION_LICENSE=${_irfq_self_double_conversion_license}"
      "-DIRFQ_EXPECTED_PUGIXML_LICENSE=${_irfq_self_pugixml_license}"
      "-DIRFQ_EXPECTED_SCOPE_GUARD_LICENSE=${_irfq_self_scope_guard_license}"
      "-DIRFQ_EXPECTED_SOURCE_CHECKOUT=${_irfq_self_source_checkout}"
      "-DIRFQ_EXPECTED_SOURCE_RELEASE_BASE=386ce46e917ae494ab6e90b1be90fd421cdbe3f9"
      "-DIRFQ_EXPECTED_SELF_TEST_RELEASE_BASE=${_irfq_self_source_base}"
      "-DIRFQ_EXPECTED_GIT=${_irfq_self_git}"
      "-DIRFQ_EXPECTED_SHA256SUM=${_irfq_self_sha256sum}"
      "-DIRFQ_VERIFY_SELF_TEST=ON"
      "-DIRFQ_EXPECTED_SOURCE_COMMIT=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
      "-DIRFQ_EXPECTED_SOURCE_TREE=ffffffffffffffffffffffffffffffffffffffff"
      "-DIRFQ_EXPECTED_SOURCE_MERGE_BASE=9999999999999999999999999999999999999999"
      "-DIRFQ_EXPECTED_SOURCE_DIFF_SHA256=8888888888888888888888888888888888888888888888888888888888888888"
      "-DIRFQ_EXPECTED_BUILD_COMMAND=${_irfq_self_build_command}")

  function(_irfq_self_verify directory expect_success label)
    execute_process(
      COMMAND "${CMAKE_COMMAND}"
              "-DIRFQ_PACKAGE_DIR=${directory}"
              ${_irfq_self_verify_common}
              -P "${CMAKE_CURRENT_LIST_FILE}"
      RESULT_VARIABLE _result
      OUTPUT_VARIABLE _output
      ERROR_VARIABLE _error)
    if(expect_success AND NOT _result EQUAL 0)
      message(FATAL_ERROR "${label}: verifier unexpectedly rejected the package:\n${_output}${_error}")
    elseif(NOT expect_success AND _result EQUAL 0)
      message(FATAL_ERROR "${label}: verifier unexpectedly accepted the package")
    endif()
  endfunction()

  function(_irfq_self_copy_case name)
    set(_case "${_irfq_self_root}/${name}")
    file(MAKE_DIRECTORY "${_case}")
    foreach(_file IN LISTS _irfq_package_files)
      file(COPY_FILE "${_irfq_output_dir}/${_file}" "${_case}/${_file}")
    endforeach()
    set(IRFQ_SELF_CASE "${_case}" PARENT_SCOPE)
  endfunction()

  _irfq_self_verify("${_irfq_output_dir}" TRUE positive)

  set(_irfq_escape_root "${_irfq_self_root}-verifier-escape")
  set(_irfq_escape_package "${_irfq_escape_root}/package")
  file(MAKE_DIRECTORY "${_irfq_escape_package}")
  foreach(_file IN LISTS _irfq_package_files)
    file(COPY_FILE "${_irfq_output_dir}/${_file}" "${_irfq_escape_package}/${_file}")
  endforeach()
  _irfq_self_verify("${_irfq_escape_package}" FALSE self-test-root-escape)
  set(_irfq_escape_link "${_irfq_self_root}/verifier-escape-parent")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink "${_irfq_escape_root}" "${_irfq_escape_link}"
                  COMMAND_ERROR_IS_FATAL ANY)
  _irfq_self_verify("${_irfq_escape_link}/package" FALSE self-test-symlink-root-escape)

  _irfq_self_copy_case(unknown-key)
  file(APPEND "${IRFQ_SELF_CASE}/manifest.sha256" "unknown=value\n")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE unknown-key)

  _irfq_self_copy_case(missing-key)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REGEX REPLACE "^schema=[^\n]*\n" "" _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE missing-key)

  _irfq_self_copy_case(duplicate-key)
  file(APPEND "${IRFQ_SELF_CASE}/manifest.sha256" "schema=irfq.infinite-fix-engine-artifact.v2\n")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE duplicate-key)

  _irfq_self_copy_case(reordered-key)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REGEX REPLACE
         "^schema=([^\n]*)\nsource_commit=([^\n]*)\n"
         "source_commit=\\2\nschema=\\1\n"
         _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE reordered-key)

  _irfq_self_copy_case(injected-ranlib)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REPLACE "ar=GNU ar 2.42\n" "ar=GNU ar 2.42\nranlib=GNU ranlib 2.42\n" _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE injected-ranlib)

  _irfq_self_copy_case(changed-source-diff)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REGEX REPLACE
         "source_diff_sha256=[0-9a-f]+"
         "source_diff_sha256=7777777777777777777777777777777777777777777777777777777777777777"
         _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE changed-source-diff)

  _irfq_self_copy_case(empty-value)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REGEX REPLACE "^schema=[^\n]*" "schema=" _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE empty-value)

  _irfq_self_copy_case(cr-byte)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REPLACE "\n" "\r\n" _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE cr-byte)

  _irfq_self_copy_case(control-byte)
  string(ASCII 1 _control)
  file(APPEND "${IRFQ_SELF_CASE}/manifest.sha256" "${_control}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE control-byte)

  _irfq_self_copy_case(noncanonical-number)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REGEX REPLACE "archive_size=([0-9]+)" "archive_size=0\\1" _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE noncanonical-number)

  _irfq_self_copy_case(invalid-enum)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REPLACE "have_ssl=OFF" "have_ssl=NO" _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE invalid-enum)

  _irfq_self_copy_case(wrong-tool-version)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REPLACE "ninja=ninja 1.11.1" "ninja=ninja 1.12.0" _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE wrong-tool-version)

  _irfq_self_copy_case(invalid-path)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REPLACE "archive_path=libquickfix.a" "archive_path=../libquickfix.a" _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE invalid-path)

  _irfq_self_copy_case(invalid-hash)
  file(READ "${IRFQ_SELF_CASE}/manifest.sha256" _manifest)
  string(REGEX REPLACE "header_sha256=[0-9a-f]+" "header_sha256=ABC" _manifest "${_manifest}")
  file(WRITE "${IRFQ_SELF_CASE}/manifest.sha256" "${_manifest}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE invalid-hash)

  _irfq_self_copy_case(oversize-manifest)
  string(REPEAT x 17000 _oversize)
  file(APPEND "${IRFQ_SELF_CASE}/manifest.sha256" "${_oversize}")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE oversize-manifest)

  _irfq_self_copy_case(wrong-artifact-digest)
  file(APPEND "${IRFQ_SELF_CASE}/libquickfix.a" "drift")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE wrong-artifact-digest)

  _irfq_self_copy_case(wrong-license-bytes)
  file(APPEND "${IRFQ_SELF_CASE}/LICENSES.txt" "drift")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE wrong-license-bytes)

  _irfq_self_copy_case(missing-file)
  file(REMOVE "${IRFQ_SELF_CASE}/InfiniteFrameAdapter.h")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE missing-file)

  _irfq_self_copy_case(extra-file)
  file(WRITE "${IRFQ_SELF_CASE}/extra" "unexpected\n")
  _irfq_self_verify("${IRFQ_SELF_CASE}" FALSE extra-file)

  execute_process(COMMAND "${_irfq_self_git}" -C "${_irfq_self_source_checkout}"
                          worktree remove "${_irfq_source_escape}" COMMAND_ERROR_IS_FATAL ANY)
  file(REMOVE "${_irfq_escape_link}")
  file(REMOVE_RECURSE "${_irfq_escape_root}")
  message(STATUS "Infinite adapter package self-test passed")
  return()
endif()

foreach(_required IN ITEMS
    IRFQ_PACKAGE_DIR
    IRFQ_EXPECTED_ARCHIVE
    IRFQ_EXPECTED_HEADER
    IRFQ_EXPECTED_ABI_FIXTURE
    IRFQ_EXPECTED_QUICKFIX_LICENSE
    IRFQ_EXPECTED_APACHE_LICENSE
    IRFQ_EXPECTED_DOUBLE_CONVERSION_LICENSE
    IRFQ_EXPECTED_PUGIXML_LICENSE
    IRFQ_EXPECTED_SCOPE_GUARD_LICENSE
    IRFQ_EXPECTED_SOURCE_CHECKOUT
    IRFQ_EXPECTED_SOURCE_RELEASE_BASE
    IRFQ_EXPECTED_GIT
    IRFQ_EXPECTED_SHA256SUM
    IRFQ_EXPECTED_BUILD_COMMAND)
  _irfq_require(${_required})
endforeach()

if(NOT IRFQ_EXPECTED_GIT STREQUAL "/usr/bin/git" OR NOT IRFQ_EXPECTED_SHA256SUM STREQUAL "/usr/bin/sha256sum")
  message(FATAL_ERROR "package verification requires the governed Git and SHA-256 tools")
endif()
set(_irfq_verify_self_test FALSE)
if(DEFINED IRFQ_VERIFY_SELF_TEST)
  if(IRFQ_VERIFY_SELF_TEST STREQUAL "ON" OR IRFQ_VERIFY_SELF_TEST STREQUAL "TRUE")
    set(_irfq_verify_self_test TRUE)
  elseif(NOT IRFQ_VERIFY_SELF_TEST STREQUAL "OFF" AND NOT IRFQ_VERIFY_SELF_TEST STREQUAL "FALSE")
    message(FATAL_ERROR "IRFQ_VERIFY_SELF_TEST must be a canonical boolean")
  endif()
endif()
set(_irfq_verify_self_test_base "")
if(DEFINED IRFQ_EXPECTED_SELF_TEST_RELEASE_BASE)
  set(_irfq_verify_self_test_base "${IRFQ_EXPECTED_SELF_TEST_RELEASE_BASE}")
endif()
if(_irfq_verify_self_test)
  file(REAL_PATH "${IRFQ_EXPECTED_SOURCE_CHECKOUT}" _irfq_verify_self_test_source)
  if(NOT _irfq_verify_self_test_source MATCHES "^/build/irfq-package-self-test-[A-Za-z0-9_.-]+/source$")
    message(FATAL_ERROR "verifier self-test source must be isolated under /build")
  endif()
  cmake_path(GET _irfq_verify_self_test_source PARENT_PATH _irfq_verify_self_test_root)
  file(REAL_PATH "${IRFQ_PACKAGE_DIR}" _irfq_verify_self_test_package)
  string(FIND "${_irfq_verify_self_test_package}/" "${_irfq_verify_self_test_root}/" _irfq_self_test_prefix)
  if(_irfq_verify_self_test_package STREQUAL _irfq_verify_self_test_root OR NOT _irfq_self_test_prefix EQUAL 0)
    message(FATAL_ERROR "verifier self-test package and source must share the exact isolated root")
  endif()
endif()
_irfq_verify_source_provenance(
  "${IRFQ_EXPECTED_SOURCE_CHECKOUT}"
  "${IRFQ_EXPECTED_SOURCE_RELEASE_BASE}"
  "${_irfq_verify_self_test_base}"
  "${_irfq_verify_self_test}")

if(NOT IS_ABSOLUTE "${IRFQ_PACKAGE_DIR}" OR NOT IS_DIRECTORY "${IRFQ_PACKAGE_DIR}" OR IS_SYMLINK "${IRFQ_PACKAGE_DIR}")
  message(FATAL_ERROR "IRFQ_PACKAGE_DIR must be an absolute non-symlink directory")
endif()

file(GLOB _irfq_entries RELATIVE "${IRFQ_PACKAGE_DIR}"
     LIST_DIRECTORIES TRUE "${IRFQ_PACKAGE_DIR}/*" "${IRFQ_PACKAGE_DIR}/.*")
list(SORT _irfq_entries)
if(NOT "${_irfq_entries}" STREQUAL "${_irfq_package_files}")
  message(FATAL_ERROR "package must contain exactly the five governed files; got: ${_irfq_entries}")
endif()
foreach(_file IN LISTS _irfq_package_files)
  _irfq_require_regular_file("${IRFQ_PACKAGE_DIR}/${_file}" "package entry ${_file}")
  file(SIZE "${IRFQ_PACKAGE_DIR}/${_file}" _size)
  if(_size EQUAL 0)
    message(FATAL_ERROR "package entry must not be empty: ${_file}")
  endif()
endforeach()

set(_irfq_manifest_path "${IRFQ_PACKAGE_DIR}/manifest.sha256")
file(SIZE "${_irfq_manifest_path}" _irfq_manifest_size)
if(_irfq_manifest_size GREATER 16384)
  message(FATAL_ERROR "manifest exceeds 16384 bytes")
endif()
file(READ "${_irfq_manifest_path}" _irfq_manifest_hex HEX)
string(REGEX REPLACE "(0a|[2-6][0-9a-f]|7[0-9a-e])" "" _irfq_invalid_hex "${_irfq_manifest_hex}")
if(NOT _irfq_invalid_hex STREQUAL "")
  message(FATAL_ERROR "manifest must contain only printable ASCII and LF bytes")
endif()
if(NOT _irfq_manifest_hex MATCHES "0a$" OR _irfq_manifest_hex MATCHES "0a0a$")
  message(FATAL_ERROR "manifest must end in exactly one LF")
endif()

file(READ "${_irfq_manifest_path}" _irfq_manifest)
string(REPLACE ";" "\\;" _irfq_manifest "${_irfq_manifest}")
string(REGEX REPLACE "\n$" "" _irfq_manifest "${_irfq_manifest}")
string(REPLACE "\n" ";" _irfq_lines "${_irfq_manifest}")
list(LENGTH _irfq_lines _irfq_line_count)
list(LENGTH _irfq_manifest_keys _irfq_key_count)
if(NOT _irfq_line_count EQUAL _irfq_key_count)
  message(FATAL_ERROR "manifest must contain exactly ${_irfq_key_count} lines")
endif()

set(_irfq_index 0)
foreach(_line IN LISTS _irfq_lines)
  list(GET _irfq_manifest_keys ${_irfq_index} _expected_key)
  string(FIND "${_line}" "=" _separator)
  if(_separator LESS 1)
    message(FATAL_ERROR "manifest line ${_irfq_index} is not key=value")
  endif()
  string(SUBSTRING "${_line}" 0 ${_separator} _key)
  math(EXPR _value_offset "${_separator} + 1")
  string(SUBSTRING "${_line}" ${_value_offset} -1 _value)
  if(NOT _key STREQUAL _expected_key)
    message(FATAL_ERROR "manifest key ${_irfq_index}: expected ${_expected_key}, got ${_key}")
  endif()
  if(_value STREQUAL "")
    message(FATAL_ERROR "manifest value must not be empty: ${_key}")
  endif()
  set("_irfq_value_${_key}" "${_value}")
  math(EXPR _irfq_index "${_irfq_index} + 1")
endforeach()

_irfq_require_equal("${_irfq_value_schema}" "irfq.infinite-fix-engine-artifact.v2" schema)
foreach(_git_field IN ITEMS source_commit source_tree source_merge_base specification_commit specification_tree)
  _irfq_require_hex("${_irfq_value_${_git_field}}" 40 "${_git_field}")
endforeach()
foreach(_sha_field IN ITEMS
    source_diff_sha256 specification_bundle_sha256 build_image_sha256
    transport_dictionary_sha256 application_dictionary_sha256
    dictionary_provenance_sha256 dictionary_license_inventory_sha256
    c_abi_exports_sha256 archive_sha256 header_sha256 abi_fixture_sha256 licenses_sha256)
  _irfq_require_hex("${_irfq_value_${_sha_field}}" 64 "${_sha_field}")
endforeach()

_irfq_require_equal("${_irfq_value_source_commit}" "${IRFQ_VERIFIED_SOURCE_COMMIT}" source_commit)
_irfq_require_equal("${_irfq_value_source_tree}" "${IRFQ_VERIFIED_SOURCE_TREE}" source_tree)
_irfq_require_equal("${_irfq_value_source_merge_base}" "${IRFQ_VERIFIED_SOURCE_MERGE_BASE}" source_merge_base)
_irfq_require_equal("${_irfq_value_source_diff_sha256}" "${IRFQ_VERIFIED_SOURCE_DIFF_SHA256}" source_diff_sha256)
_irfq_require_equal("${_irfq_value_specification_commit}" "cb12d80aecd90f52b7fbcc009246dab640ba7a7b" specification_commit)
_irfq_require_equal("${_irfq_value_specification_tree}" "bae719cd3d4001329f6efbe5e32d92be3549b5f9" specification_tree)
_irfq_require_equal("${_irfq_value_specification_bundle_sha256}" "4a35891736502ffe2413de8d25690748879b88e4938abf89a5bda835a3ad7ab9" specification_bundle_sha256)
_irfq_require_equal("${_irfq_value_target}" "x86_64-unknown-linux-gnu" target)
_irfq_require_equal("${_irfq_value_build_image}" "irfq-task2e-cpp:2026-08-30" build_image)
_irfq_require_equal("${_irfq_value_build_image_sha256}" "c333c04c1f5ca0496016b43996fbdeed30c3b1b91f5e1581a418bf68a518139c" build_image_sha256)
_irfq_require_equal("${_irfq_value_cc}" "gcc 13.3.0" cc)
_irfq_require_equal("${_irfq_value_cxx}" "g++ 13.3.0" cxx)
_irfq_require_equal("${_irfq_value_cmake}" "cmake 3.28.3" cmake)
_irfq_require_equal("${_irfq_value_ninja}" "ninja 1.11.1" ninja)
_irfq_require_equal("${_irfq_value_objcopy}" "GNU objcopy 2.42" objcopy)
_irfq_require_equal("${_irfq_value_linker}" "GNU ld 2.42" linker)
_irfq_require_equal("${_irfq_value_ar}" "GNU ar 2.42" ar)
_irfq_require_equal("${_irfq_value_build_type}" "Release" build_type)
_irfq_require_equal("${_irfq_value_have_ssl}" "OFF" have_ssl)
_irfq_require_equal("${_irfq_value_quickfix_shared_libs}" "OFF" quickfix_shared_libs)
_irfq_require_equal("${_irfq_value_quickfix_examples}" "OFF" quickfix_examples)
_irfq_require_equal("${_irfq_value_quickfix_tests}" "ON" quickfix_tests)
_irfq_require_equal("${_irfq_value_irfq_infinite_embed_dictionaries}" "ON" irfq_infinite_embed_dictionaries)
_irfq_require_equal("${_irfq_value_build_command}" "${IRFQ_EXPECTED_BUILD_COMMAND}" build_command)
set(_irfq_canonical_build_command
    "cmake -S /src -B /build -G Ninja -DCMAKE_C_COMPILER=/usr/bin/gcc-13 -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 -DCMAKE_BUILD_TYPE=Release -DHAVE_SSL=OFF -DQUICKFIX_SHARED_LIBS=OFF -DQUICKFIX_EXAMPLES=OFF -DQUICKFIX_TESTS=ON -DIRFQ_INFINITE_EMBED_DICTIONARIES=ON -DQUICKFIX_LIB_OUTPUT_DIR=/build/out && cmake --build /build --parallel 2 --target infinite_adapter_package")
_irfq_require_equal("${_irfq_value_build_command}" "${_irfq_canonical_build_command}" build_command)
string(LENGTH "${_irfq_value_build_command}" _irfq_build_command_length)
if(_irfq_build_command_length GREATER 4096
   OR _irfq_value_build_command MATCHES "/home/|/Users/|/tmp/")
  message(FATAL_ERROR "build_command must be at most 4096 bytes and contain no host-specific path")
endif()

_irfq_require_equal("${_irfq_value_transport_dictionary_id}" "INFINITE-FIXT11" transport_dictionary_id)
_irfq_require_equal("${_irfq_value_transport_dictionary_size}" "9242" transport_dictionary_size)
_irfq_require_equal("${_irfq_value_transport_dictionary_sha256}" "75ecae3957f5f5b0cc8613ac8976bb33dfe3e2edf012cdf36087b349ad5f85e5" transport_dictionary_sha256)
_irfq_require_equal("${_irfq_value_application_dictionary_id}" "INFINITE-RFQ-1.0.0-EP299" application_dictionary_id)
_irfq_require_equal("${_irfq_value_application_dictionary_size}" "79971" application_dictionary_size)
_irfq_require_equal("${_irfq_value_application_dictionary_sha256}" "d9ce75d206573a391dbcb83a61665f3844916cfd63006d6ab99d645bac6d2551" application_dictionary_sha256)
_irfq_require_equal("${_irfq_value_dictionary_provenance_sha256}" "18a671069a156f19a9898dcbe7a7e4b283f063116cc20d756bbcf9ef840d2af4" dictionary_provenance_sha256)
_irfq_require_equal("${_irfq_value_dictionary_license_inventory_sha256}" "f76489fe025d794f38f394f34569d899235af925ad24444ebba555f09710de31" dictionary_license_inventory_sha256)
_irfq_require_equal("${_irfq_value_c_abi_exports}" "irfq_infinite_abort_v2,irfq_infinite_apply_committed_v2,irfq_infinite_destroy_v2,irfq_infinite_prepare_v2,irfq_infinite_resume_v2,irfq_infinite_scan_v2,irfq_infinite_session_create_v2" c_abi_exports)
_irfq_require_equal("${_irfq_value_c_abi_exports_sha256}" "90a3b98861d07a4fcc219e098d75bbaf99ba644f8b996c7725bffd5b28e8286e" c_abi_exports_sha256)

_irfq_require_equal("${_irfq_value_archive_path}" "libquickfix.a" archive_path)
_irfq_require_equal("${_irfq_value_header_path}" "InfiniteFrameAdapter.h" header_path)
_irfq_require_equal("${_irfq_value_abi_fixture_path}" "infinite-frame-adapter-abi.v2.tsv" abi_fixture_path)
_irfq_require_equal("${_irfq_value_licenses_path}" "LICENSES.txt" licenses_path)
_irfq_require_artifact(
  "${IRFQ_PACKAGE_DIR}/libquickfix.a" "${IRFQ_EXPECTED_ARCHIVE}"
  "${_irfq_value_archive_size}" "${_irfq_value_archive_sha256}" archive)
_irfq_require_artifact(
  "${IRFQ_PACKAGE_DIR}/InfiniteFrameAdapter.h" "${IRFQ_EXPECTED_HEADER}"
  "${_irfq_value_header_size}" "${_irfq_value_header_sha256}" header)
_irfq_require_artifact(
  "${IRFQ_PACKAGE_DIR}/infinite-frame-adapter-abi.v2.tsv" "${IRFQ_EXPECTED_ABI_FIXTURE}"
  "${_irfq_value_abi_fixture_size}" "${_irfq_value_abi_fixture_sha256}" "ABI fixture")

_irfq_require_regular_file("${IRFQ_EXPECTED_QUICKFIX_LICENSE}" "QuickFIX license")
_irfq_require_regular_file("${IRFQ_EXPECTED_APACHE_LICENSE}" "Apache license")
_irfq_require_regular_file("${IRFQ_EXPECTED_DOUBLE_CONVERSION_LICENSE}" "double-conversion license")
_irfq_require_regular_file("${IRFQ_EXPECTED_PUGIXML_LICENSE}" "pugixml license")
_irfq_require_regular_file("${IRFQ_EXPECTED_SCOPE_GUARD_LICENSE}" "scope_guard license")
file(READ "${IRFQ_EXPECTED_QUICKFIX_LICENSE}" _irfq_quickfix_license)
file(READ "${IRFQ_EXPECTED_APACHE_LICENSE}" _irfq_apache_license)
file(READ "${IRFQ_EXPECTED_DOUBLE_CONVERSION_LICENSE}" _irfq_double_conversion_license)
file(READ "${IRFQ_EXPECTED_PUGIXML_LICENSE}" _irfq_pugixml_license)
file(READ "${IRFQ_EXPECTED_SCOPE_GUARD_LICENSE}" _irfq_scope_guard_license)
set(_irfq_expected_licenses
    "===== QuickFIX Software License 1.0 =====\n${_irfq_quickfix_license}===== FIX Trading Community dictionary source: Apache-2.0 =====\n${_irfq_apache_license}===== double-conversion: BSD-3-Clause =====\n${_irfq_double_conversion_license}===== pugixml: MIT =====\n${_irfq_pugixml_license}===== scope_guard: Unlicense =====\n${_irfq_scope_guard_license}")
file(READ "${IRFQ_PACKAGE_DIR}/LICENSES.txt" _irfq_actual_licenses)
if(NOT _irfq_actual_licenses STREQUAL _irfq_expected_licenses)
  message(FATAL_ERROR "LICENSES.txt does not preserve the governed license bytes and separators")
endif()
file(SIZE "${IRFQ_PACKAGE_DIR}/LICENSES.txt" _irfq_licenses_size)
file(SHA256 "${IRFQ_PACKAGE_DIR}/LICENSES.txt" _irfq_licenses_hash)
_irfq_require_positive_decimal("${_irfq_value_licenses_size}" "licenses size")
_irfq_require_hex("${_irfq_value_licenses_sha256}" 64 "licenses SHA-256")
_irfq_require_equal("${_irfq_licenses_size}" "${_irfq_value_licenses_size}" "licenses manifest size")
_irfq_require_equal("${_irfq_licenses_hash}" "${_irfq_value_licenses_sha256}" "licenses manifest SHA-256")

message(STATUS "Infinite adapter package verified: ${IRFQ_PACKAGE_DIR}")
