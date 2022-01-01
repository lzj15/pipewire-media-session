conf_config = configuration_data()
conf_config.set('VERSION', '@0@'.format(media_session_version))
conf_config.set('MEDIA_SESSION_CONFIG_DIR', media_session_configdir)

conf_files = [
  [ 'access-default.conf', 'access-default.conf' ],
  [ 'bluez-monitor.conf', 'bluez-monitor.conf' ],
  [ 'v4l2-monitor.conf', 'v4l2-monitor.conf' ],
  [ 'media-session.conf', 'media-session.conf' ],
  [ 'alsa-monitor.conf', 'alsa-monitor.conf' ],
]

foreach opt : get_option('with-module-sets')
  conf_files += [ [ 'with-module-set.in', 'with-@0@'.format(opt) ] ]
endforeach

foreach c : conf_files
  configure_file(input : c.get(0),
                 output : c.get(1),
                 configuration : conf_config,
                 install_dir : media_session_confdatadir / 'media-session.d')
endforeach
