pidginpixmapdir = get_option('datadir') / 'pixmaps/pidgin'

subdir('emotes')

if INSTALL_PIXMAPS
	install_data('logo.png', 'arrow-down.xpm', 'arrow-left.xpm', 'arrow-right.xpm', 'arrow-up.xpm',
	    install_dir : pidginpixmapdir)

	# Some of these don't use install_subdir because it deletes the target,
	# and some target directories probably have something in them, for those
	# installing somewhere with existing stuff. Additionally, we have
	# extra stuff in our directories we don't want to install.
	install_data('edit.png', 'info.png', 'pause.png',
	    install_dir : pidginpixmapdir / 'buttons')
	subdir('tray')
endif
