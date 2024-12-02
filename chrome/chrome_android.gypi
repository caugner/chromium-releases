# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'variables': {
    'chromium_code': 1,
  },
  'includes': [
    'chrome_android_paks.gypi', # Included for the list of pak resources.
  ],
  'targets': [
    {
      'target_name': 'libchromiumtestshell',
      'type': 'shared_library',
      'dependencies': [
        'chrome_android_core',
        'chromium_testshell_jni_headers',
      ],
      'sources': [
        'android/testshell/chrome_main_delegate_testshell_android.cc',
        'android/testshell/chrome_main_delegate_testshell_android.h',
        'android/testshell/tab_manager.cc',
        'android/testshell/tab_manager.h',
        'android/testshell/testshell_entry_point.cc',
        'android/testshell/testshell_stubs.cc',
      ],
      'include_dirs': [
        '<(SHARED_INTERMEDIATE_DIR)/android',
        '<(SHARED_INTERMEDIATE_DIR)/chromium_testshell',
        '../skia/config',
      ],
      'conditions': [
        [ 'order_profiling!=0', {
          'conditions': [
            [ 'OS=="android"', {
              'dependencies': [ '../tools/cygprofile/cygprofile.gyp:cygprofile', ],
            }],
          ],
        }],
      ],
    },
    {
      'target_name': 'chromium_testshell',
      'type': 'none',
      'dependencies': [
        '../media/media.gyp:media_java',
        'chrome.gyp:chrome_java',
        'chrome_android_paks',
        'libchromiumtestshell',
      ],
      'variables': {
        'package_name': 'chromium_testshell',
        'apk_name': 'ChromiumTestShell',
        'manifest_package_name': 'org.chromium.chrome.testshell',
        'java_in_dir': 'android/testshell/java',
        'resource_dir': '../res',
        'asset_location': '<(ant_build_out)/../assets/chrome',
        'native_libs_paths': [ '<(SHARED_LIB_DIR)/libchromiumtestshell.so', ],
        'additional_input_paths': [ '<@(chrome_android_pak_output_resources)', ],
      },
      'includes': [ '../build/java_apk.gypi', ],
    },
    {
      'target_name': 'chromium_testshell_jni_headers',
      'type': 'none',
      'sources': [
        'android/testshell/java/src/org/chromium/chrome/testshell/TabManager.java',
      ],
      'variables': {
        'jni_gen_dir': 'chromium_testshell',
      },
      'includes': [ '../build/jni_generator.gypi' ],
    },
    {
      # chromium_testshell creates a .jar as a side effect. Any java targets
      # that need that .jar in their classpath should depend on this target,
      # chromium_testshell_java.
      'target_name': 'chromium_testshell_java',
      'type': 'none',
      'dependencies': [
        '../media/media.gyp:media_java',
        'chrome.gyp:chrome_java',
        'chromium_testshell',
      ],
      'variables': {
        'output_jar': '<(PRODUCT_DIR)/lib.java/chromium_apk_chromium_testshell.jar'
      },
      'outputs': ['<(output_jar)'],
      # This all_dependent_settings is used for java targets only. This will add
      # the chromium_testshell jar to the classpath of dependent java targets.
      'all_dependent_settings': {
        'variables': {
          'input_jars_paths': ['<(output_jar)'],
        },
      },
      # Add an action with the appropriate output. This allows the generated
      # buildfiles to determine which target the output corresponds to.
      'actions': [
        {
          'action_name': 'fake_generate_jar',
          'inputs': [],
          'outputs': ['<(output_jar)'],
          'action': [],
        },
      ],
    },
    {
      'target_name': 'chrome_android_core',
      'type': 'static_library',
      'dependencies': [
        'chrome.gyp:browser',
        'chrome.gyp:plugin',
        'chrome.gyp:renderer',
        'chrome.gyp:utility',
        '../content/content.gyp:content',
      ],
      'include_dirs': [
        '..',
        '<(SHARED_INTERMEDIATE_DIR)/android',
        '<(SHARED_INTERMEDIATE_DIR)/chrome',
        '<(android_ndk_include)',
      ],
      'sources': [
        'app/android/chrome_android_initializer.cc',
        'app/android/chrome_android_initializer.h',
        'app/android/chrome_main_delegate_android.cc',
        'app/android/chrome_main_delegate_android.h',
        'app/chrome_main_delegate.cc',
        'app/chrome_main_delegate.h',
      ],
      'link_settings': {
        'libraries': [
          '-landroid',
          '-ljnigraphics',
        ],
      },
    },
    {
      'target_name': 'chrome_android_paks',
      'type': 'none',
      'dependencies': [
        '<(DEPTH)/chrome/chrome_resources.gyp:packed_resources',
        '<(DEPTH)/chrome/chrome_resources.gyp:packed_extra_resources',
      ],
      'copies': [
        {
          'destination': '<(chrome_android_pak_output_folder)',
          'files': [ '<@(chrome_android_pak_input_resources)' ],
        }
      ],
    },
  ],
}
