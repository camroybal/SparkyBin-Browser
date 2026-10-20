envs = [
  'G_TEST_SRCDIR=' + meson.current_source_dir(),
  'G_TEST_BUILDDIR=' + meson.current_build_dir(),
  'GSETTINGS_SCHEMA_DIR=' + join_paths(meson.project_build_root(), 'data'),
  'GSETTINGS_BACKEND=memory',
  'GTK_IM_MODULE=none',
  'PATH=' + join_paths(meson.project_build_root(), 'src') + ':' + run_command('printenv', 'PATH', check: true).stdout().strip(),
]

test_cargs = ['-UG_DISABLE_ASSERT']

# Four tests depend on a working desktop session rather than on the code:
# File helpers assumes that it cannot write to /root (so it fails when run as
# root); Shell, Embed shell and Snapshot service need the portals, WebKit's
# sandbox and a writable /root that a normal desktop provides.  They fail the
# same way on an untouched tree in a container, so they live in the
# "environmental" suite instead of being deleted or marked should_fail (which
# would report an unexpected pass as a failure).  Run the normal suite with:
#
#   meson test --no-suite environmental
#
# and run the environmental ones deliberately with:
#
#   meson test --suite environmental
#
# A plain `meson test` still runs both, so nothing is hidden from it.

if get_option('unit_tests').enabled()
  libephytestutils = static_library('ephytestutils',
    'ephy-test-utils.c',
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )

  ephytestutils_dep = declare_dependency(
    link_with: libephytestutils
  )

  # FIXME: https://bugzilla.gnome.org/show_bug.cgi?id=778153
  # download_test = executable('test-ephy-download',
  #   'ephy-download-test.c',
  #   dependencies: ephymain_dep,
  #   c_args: test_cargs,
  # )
  # test('Download test',
  #      download_test,
  #      env: envs
  # )

  embed_shell_test = executable('test-ephy-embed-shell',
    'ephy-embed-shell-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs + ['-DTEST_DIR="' + meson.current_source_dir() + '"'],
  )
  test('Embed shell test',
       embed_shell_test,
       env: envs,
       suite: 'environmental'
  )

  embed_utils_test = executable('test-ephy-embed-utils',
    'ephy-embed-utils-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )
  test('Embed utils test',
       embed_utils_test,
       env: envs
  )

  encodings_test = executable('test-ephy-encodings',
    'ephy-encodings-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )
  test('Encodings test',
       encodings_test,
       env: envs
  )

  file_helpers_test = executable('test-ephy-file-helpers',
    'ephy-file-helpers-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )
  test('File helpers test',
       file_helpers_test,
       env: envs,
       suite: 'environmental'
  )

  filters_compiler_test = executable('test-ephy-filters-compiler',
    'test-ephy-filters-compiler.c',
    dependencies: ephymisc_dep,
    c_args: test_cargs,
  )
  test('Filters compiler test',
       filters_compiler_test,
       env: envs
  )

  filters_block_test = executable('test-ephy-filters-block',
    'test-ephy-filters-block.c',
    dependencies: ephymisc_dep,
    c_args: test_cargs,
  )
  test('Filters block test',
       filters_block_test,
       env: envs
  )

  test('Extension filter cache test',
       find_program('python3'),
       args: [
         join_paths(meson.current_source_dir(), 'test-extension-filter-cache.py'),
         '--browser', join_paths(meson.project_build_root(), 'src', 'sparkybin-browser'),
         '--fixture', join_paths(meson.current_source_dir(), 'data', 'extension-filter-cache'),
       ],
       env: envs,
       timeout: 240
  )

  filters_compiler_web_test = executable('test-ephy-filters-compiler-web',
    'test-ephy-filters-compiler-web.c',
    dependencies: ephymisc_dep,
    c_args: test_cargs,
  )

  history_test = executable('test-ephy-history',
    'ephy-history-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )
  test('History test',
       history_test,
       env: envs
  )

  location_entry_test = executable('test-location-entry',
    'ephy-location-entry-test.c',
    resources,
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )
  test('Location entry test',
       location_entry_test,
       env: envs
  )

  migration_test = executable('test-ephy-migration',
    'ephy-migration-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )
  test('Migration test',
    migration_test,
    env: envs,
    depends: ephy_profile_migrator
  )

  search_engine_manager_test = executable('test-ephy-search-engine-manager',
    'ephy-search-engine-manager-test.c',
    dependencies: ephymisc_dep,
    c_args: test_cargs,
  )
  test('Search engine manager test',
    search_engine_manager_test,
    env: envs,
  )

  # FIXME: https://bugzilla.gnome.org/show_bug.cgi?id=707220
  # session_test = executable('test-ephy-session',
  #   'ephy-session-test.c',
  #   dependencies: ephytestutils_dep,
  #    c_args: test_cargs,
  # )
  # test('Session test',
  #      session_test,
  #      env: envs
  # )

  shell_test = executable('test-ephy-shell',
    'ephy-shell-test.c',
    resources,
    readability_resources,
    dependencies: [ephymain_dep, ephytestutils_dep],
    c_args: test_cargs,
  )
  test('Shell test',
       shell_test,
       env: envs,
       suite: 'environmental'
  )

  snapshot_service_test = executable('test-ephy-snapshot-service',
    'ephy-snapshot-service-test.c',
    dependencies: [ephymain_dep, ephytestutils_dep],
    c_args: test_cargs,
  )
  test('Snapshot service test',
       snapshot_service_test,
       env: envs,
       suite: 'environmental'
  )

  sqlite_test = executable('test-ephy-sqlite',
    'ephy-sqlite-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )
  test('SQLite test',
       sqlite_test,
       env: envs
  )

  output_encoding_test = executable('test-ephy-output-encoding',
    'ephy-output-encoding-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )
  test('Output encoding test',
       output_encoding_test,
       env: envs
  )

  string_test = executable('test-ephy-string',
    'ephy-string-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )
  test('String test',
       string_test,
       env: envs
  )

  uri_helpers_test = executable('test-ephy-uri-helpers',
    'ephy-uri-helpers-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs,
  )
  test('URI helpers test',
    uri_helpers_test,
    env: envs
  )

  web_view_test = executable('test-ephy-web-view',
    'ephy-web-view-test.c',
    resources,
    readability_resources,
    dependencies: ephymain_dep
  )
  test('Web view test',
       web_view_test,
       env: envs
  )

  web_extension_test = executable('test-ephy-web-extension',
    'ephy-web-extension-test.c',
    dependencies: ephymain_dep,
    c_args: test_cargs
  )
  test('Web extension test',
        web_extension_test,
        env: envs
  )

  password_manager_test = executable('test-ephy-password-manager',
    'ephy-password-manager-test.c',
    dependencies: [ephymain_dep, ephysync_dep],
    c_args: test_cargs,
  )
  test('Password manager test',
       password_manager_test,
       env: envs
  )
endif
