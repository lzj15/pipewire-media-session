doxyfile_conf = configuration_data()
doxyfile_conf.set('PACKAGE_NAME', meson.project_name())
doxyfile_conf.set('PACKAGE_VERSION', meson.project_version())
doxyfile_conf.set('output_directory', meson.current_build_dir())

dot_found = find_program('dot', required: false).found()
summary({'dot (used with doxygen)': dot_found}, bool_yn: true, section: 'Optional programs')
if dot_found
  doxyfile_conf.set('HAVE_DOT', 'YES')
else
  doxyfile_conf.set('HAVE_DOT', 'NO')
endif

# Note: order here is how doxygen will expose the pages in the sidebar
# api-tree.dox should be first to determine ordering of Modules.
extra_docs = [
  'index.dox',
]

inputs = []
foreach extra : extra_docs
  inputs += meson.project_source_root() / 'doc' / extra
endforeach
foreach h : media_session_sources
  inputs += meson.project_source_root() / 'src' / h
endforeach

path_prefixes = [
  meson.project_source_root() / 'src',
  meson.project_source_root(),
]

cssfiles = [
  meson.project_source_root() / 'doc' / 'doxygen-awesome.css',
  meson.project_source_root() / 'doc' / 'custom.css'
]

doxyfile_conf.set('inputs', ' '.join(inputs))
doxyfile_conf.set('cssfiles', ' '.join(cssfiles))
doxyfile_conf.set('path_prefixes', ' '.join(path_prefixes))
doxyfile_conf.set('c_input_filter', meson.project_source_root() / 'doc' / 'input-filter.sh')
doxyfile_conf.set('h_input_filter', meson.project_source_root() / 'doc' / 'input-filter-h.sh')

doxyfile = configure_file(input: 'Doxyfile.in',
                          output: 'Doxyfile',
                          configuration: doxyfile_conf)

docdir = get_option('docdir')
if docdir == ''
  docdir = media_session_datadir / 'doc' / meson.project_name()
endif

html_target = custom_target('pipewire-docs',
                            input: [ doxyfile ] + inputs + cssfiles,
                            output: [ 'html' ],
                            command: [ doxygen, doxyfile ],
                            install: true,
                            install_dir: docdir)
