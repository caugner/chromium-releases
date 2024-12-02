{
  # Disabled pnacl for now because it warns on using the language extension
  # typeof(...)
  #'TOOLS': ['newlib', 'glibc', 'pnacl', 'win'],
  'TOOLS': ['newlib', 'glibc', 'win'],
  'SEARCH': [
    '.',
    '../utils'
  ],
  'TARGETS': [
    {
      'NAME' : 'nacl_mounts',
      'TYPE' : 'lib',
      'SOURCES' : [
        "kernel_handle.cc",
        "kernel_intercept.cc",
        "kernel_object.cc",
        "kernel_proxy.cc",
        "kernel_wrap.cc",
        "mount.cc",
        "mount_mem.cc",
        "mount_node.cc",
        "mount_node_dir.cc",
        "mount_node_mem.cc",
        "path.cc",
      ],
    }
  ],
  'HEADERS': [
    {
      'FILES': [
        "kernel_handle.h",
        "kernel_intercept.h",
        "kernel_object.h",
        "kernel_proxy.h",
        "kernel_wrap.h",
        "mount.h",
        "mount_mem.h",
        "mount_node.h",
        "mount_node_dir.h",
        "mount_node_mem.h",
        "osdirent.h",
        "osstat.h",
        "ostypes.h",
        "path.h"
      ],
      'DEST': 'include/nacl_mounts',
    },
    {
      'FILES': [
        "auto_lock.h",
        "macros.h",
        "ref_object.h"
      ],
      'DEST': 'include/utils',
    }
  ],
  'DATA': [
    "kernel_wrap_glibc.cc",
    "kernel_wrap_newlib.cc",
    "kernel_wrap_win.cc",
  ],
  'DEST': 'src',
  'NAME': 'nacl_mounts',
}
