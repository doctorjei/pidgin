subdir('disco')
subdir('gestures')
subdir('xmppconsole')

if PLUGINS
	gtkbuddynote = library('gtkbuddynote', 'gtkbuddynote.c',
	    dependencies : [libpurple_dep, libpidgin_dep, glib],
	    name_prefix : '',
	    install : true, install_dir : PIDGIN_PLUGINDIR)

	iconaway = library('iconaway', 'iconaway.c',
	    dependencies : [libpurple_dep, libpidgin_dep, glib],
	    name_prefix : '',
	    install : true, install_dir : PIDGIN_PLUGINDIR)

	notify = library('notify', 'notify.c',
	    dependencies : [libpurple_dep, libpidgin_dep, glib],
	    name_prefix : '',
	    build_by_default: false,
	    install : false, install_dir : PIDGIN_PLUGINDIR)

	relnot = library('relnot', 'relnot.c',
	    dependencies : [libpurple_dep, libpidgin_dep, libsoup, glib],
	    name_prefix : '',
	    install : true, install_dir : PIDGIN_PLUGINDIR)

	spellchk = library('spellchk', 'spellchk.c',
	    dependencies : [libpurple_dep, libpidgin_dep, glib],
	    name_prefix : '',
	    build_by_default: false,
	    install : false, install_dir : PIDGIN_PLUGINDIR)

	if IS_WIN32
		transparency = library('transparency', 'transparency.c',
			dependencies : [libpurple_dep, libpidgin_dep, glib],
			name_prefix : '',
			build_by_default : false,
			install : false, install_dir : PIDGIN_PLUGINDIR)
	endif

	if enable_unity
		unity = library('unity', 'unity.c',
		    dependencies : [UNITY, libpurple_dep, libpidgin_dep, glib],
		    name_prefix : '',
		    install : true, install_dir : PIDGIN_PLUGINDIR)
	endif

endif # PLUGINS
