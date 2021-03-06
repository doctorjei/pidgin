DOC_MODULE = 'finch'

# Header files or dirs to ignore when scanning. Use base file/dir names
ignore_hfiles = [
    'plugins',
]

# Extra options to supply to gtkdoc-scan.
scan_args = [
    '--deprecated-guards=PURPLE_DISABLE_DEPRECATED|GNT_DISABLE_DEPRECATED|FINCH_DISABLE_DEPRECATED',
    '--rebuild-types',
    '--rebuild-sections',
    '--ignore-headers=' + ' '.join(ignore_hfiles),
]

# Extra options to supply to gtkdoc-mkdb.
mkdb_args = [
    '--ignore-files=' + ' '.join(ignore_hfiles),
]

# Extra options to supply to fixxrefs
fixxref_args = [
    '--extra-dir=@0@'.format(libgnt_docpath),
    '--extra-dir=../libpurple',
]

configure_file(
    input : 'version.xml.in',
    output : 'version.xml',
    configuration : version_conf)

finch_doc = gnome.gtkdoc(DOC_MODULE,
    main_xml : DOC_MODULE + '-docs.xml',
    src_dir : libfinch_inc,
    dependencies : libfinch_dep,
    install : true,
    scan_args : scan_args,
    mkdb_args : mkdb_args,
    fixxref_args : fixxref_args,
    gobject_typesfile : DOC_MODULE + '.types')
