#######################################################################
# Check for Internationalized Domain Name support
#######################################################################

JABBER_SOURCES = [
	'adhoccommands.c',
	'adhoccommands.h',
	'auth.c',
	'auth.h',
	'auth_digest_md5.c',
	'auth_digest_md5.h',
	'auth_plain.c',
	'auth_scram.c',
	'auth_scram.h',
	'auth_webex.c',
	'buddy.c',
	'buddy.h',
	'bosh.c',
	'bosh.h',
	'caps.c',
	'caps.h',
	'chat.c',
	'chat.h',
	'data.c',
	'data.h',
	'disco.c',
	'disco.h',
	'ibb.c',
	'ibb.h',
	'iq.c',
	'iq.h',
	'jabber.c',
	'jabber.h',
	'jingle/jingle.c',
	'jingle/jingle.h',
	'jingle/content.c',
	'jingle/content.h',
	'jingle/iceudp.c',
	'jingle/iceudp.h',
	'jingle/rawudp.c',
	'jingle/rawudp.h',
	'jingle/rtp.c',
	'jingle/rtp.h',
	'jingle/session.c',
	'jingle/session.h',
	'jingle/transport.c',
	'jingle/transport.h',
	'jutil.c',
	'jutil.h',
	'message.c',
	'message.h',
	'namespaces.h',
	'oob.c',
	'oob.h',
	'parser.c',
	'parser.h',
	'pep.c',
	'pep.h',
	'ping.c',
	'ping.h',
	'presence.c',
	'presence.h',
	'roster.c',
	'roster.h',
	'si.c',
	'si.h',
	'useravatar.c',
	'useravatar.h',
	'usermood.c',
	'usermood.h',
	'usernick.c',
	'usernick.h',
	'usertune.c',
	'usertune.h',
	'xdata.c',
	'xdata.h',
	'xmpp.c',
	'xmpp.h'
]

if sasl.found()
	JABBER_SOURCES += ['auth_cyrus.c']
endif

if IS_WIN32
	jabber_link_args = ['-Wl,--export-all-symbols']
else
	jabber_link_args = []
endif

if DYNAMIC_JABBER
	idn = dependency('libidn', version : '>= 0.0.0')

	jabber_prpl = shared_library('jabber', JABBER_SOURCES,
	    link_args : jabber_link_args,
	    dependencies : [gstreamer, idn, libxml, sasl, libpurple_dep, libsoup, glib, gio, math, nice, ws2_32],
	    install : true, install_dir : PURPLE_PLUGINDIR)

	subdir('tests')
endif
