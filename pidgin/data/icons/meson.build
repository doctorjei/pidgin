ICONS = [
	'im.pidgin.Pidgin3'
]

foreach icon : ICONS
	foreach size : [16, 22, 24, 32, 48]
		path = 'hicolor/@0@x@0@/apps'.format(size)
		install_data(path / '@0@.png'.format(icon),
		    install_dir : get_option('datadir') / 'icons' / path)
	endforeach
	path = 'hicolor/scalable/apps'
	install_data(path / '@0@.svg'.format(icon),
	    install_dir : get_option('datadir') / 'icons' / path)
endforeach

EXCLUDE_FILES = [
	'16x16/apps/im.pidgin.Pidgin3.png',
	'22x22/apps/im.pidgin.Pidgin3.png',
	'48x48/apps/im.pidgin.Pidgin3.png',
	'scalable/apps/im.pidgin.Pidgin3.svg',
]

EXCLUDE_DIRS = [
	'16x16/actions/scalable',
	'22x22/actions/scalable',
	'32x32/actions/scalable',
	'48x48/actions/scalable',
	'16x16/apps/scalable',
	'22x22/apps/scalable',
	'24x24',
	'32x32/apps',
	'16x16/status/scalable',
]

install_subdir('hicolor',
    exclude_files : EXCLUDE_FILES,
    exclude_directories : EXCLUDE_DIRS,
    install_dir : get_option('datadir') / 'pidgin/icons')
