DOC_MODULE = 'pidgin'

# Header files or dirs to ignore when scanning. Use base file/dir names
ignore_hfiles = [
    'pixmaps',
    'plugins',
    'themes',
    'win32',
    'gtkinternal.h',
    'pidgin.gresource.h'
]

# Extra options to supply to gtkdoc-scan.
scan_args = [
    '--deprecated-guards=PURPLE_DISABLE_DEPRECATED|PIDGIN_DISABLE_DEPRECATED',
    '--rebuild-types',
    '--rebuild-sections',
    '--ignore-headers=' + ' '.join(ignore_hfiles),
]

# Extra options to supply to gtkdoc-mkdb.
mkdb_args = [
    '--ignore-files=' + ' '.join(ignore_hfiles),
]

# Extra options to supply to fixxref
fixxref_args = [
    '--extra-dir=@0@'.format(gplugin_gtk_docpath),
    '--extra-dir=@0@'.format(talkatu_docpath),
    '--extra-dir=../libpurple',
]

configure_file(
    input : 'version.xml.in',
    output : 'version.xml',
    configuration : version_conf)

# Extra SGML files that are included by $(DOC_MAIN_XML_FILE).
content_files = [
    'signals_gtkaccount.xml',
    'signals_gtkblist.xml',
    'signals_gtkconv.xml',
    'signals_gtklog.xml',
]

pidgin_doc = gnome.gtkdoc(DOC_MODULE,
    main_xml : DOC_MODULE + '-docs.xml',
    src_dir : libpidgin_inc,
    dependencies : libpidgin_dep,
    install : true,
    scan_args : scan_args,
    mkdb_args : mkdb_args,
    fixxref_args : fixxref_args,
    gobject_typesfile : DOC_MODULE + '.types',
    content_files : content_files)
