PROGS = [
    'account_option',
    'attention_type',
    'circular_buffer',
    'credential_manager',
    'credential_provider',
    'image',
    'keyvaluepair',
    'markup',
    'protocol_action',
    'protocol_attention',
    'protocol_xfer',
    'queued_output_stream',
    'smiley',
    'smiley_list',
    'trie',
    'util',
    'xmlnode'
]

test_ui = static_library(
    'test-ui',
    'test_ui.c',
    'test_ui.h',
    c_args: [
        '-DTEST_DATA_DIR="@0@/data"'.format(meson.current_source_dir())
    ],
    dependencies: [libpurple_dep, glib]
)

foreach prog : PROGS
    e = executable('test_' + prog, 'test_@0@.c'.format(prog),
                   c_args : [
                       '-DTEST_DATA_DIR="@0@/data"'.format(meson.current_source_dir())
                   ],
                   dependencies : [libpurple_dep, glib],
                   link_with: test_ui,
    )
    test(prog, e)
endforeach
