#  
#  To use this DEPS file to re-create a Chromium release you
#  need the tools from http://dev.chromium.org installed.
#  
#  This DEPS file corresponds to Chromium 4.0.223.9
#  
#  
#  
deps_os = {
   'win': {
      'src/third_party/xulrunner-sdk':
         '/trunk/deps/third_party/xulrunner-sdk@17887',
      'src/chrome_frame/tools/test/reference_build/chrome':
         '/trunk/deps/reference_builds/chrome_frame@27181',
      'src/third_party/cygwin':
         '/trunk/deps/third_party/cygwin@11984',
      'src/third_party/python_24':
         '/trunk/deps/third_party/python_24@22967',
      'src/third_party/ffmpeg/binaries/chromium/win/ia32':
         '/trunk/deps/third_party/ffmpeg/binaries/win@28488',
   },
   'mac': {
      'src/third_party/GTM':
         'http://google-toolbox-for-mac.googlecode.com/svn/trunk@230',
      'src/third_party/pdfsqueeze':
         'http://pdfsqueeze.googlecode.com/svn/trunk@2',
      'src/third_party/WebKit/WebKit/mac':
         'http://svn.webkit.org/repository/webkit/trunk/WebKit/mac@49830',
      'src/third_party/WebKit/WebKitLibraries':
         'http://svn.webkit.org/repository/webkit/trunk/WebKitLibraries@49830',
      'src/third_party/ffmpeg/binaries/chromium/mac/ia32_dbg':
         '/trunk/deps/third_party/ffmpeg/binaries/mac_dbg@28488',
      'src/third_party/ffmpeg/binaries/chromium/mac/ia32':
         '/trunk/deps/third_party/ffmpeg/binaries/mac@28488',
   },
   'unix': {
      'src/chrome/installer/linux':
         '/installer/linux@3529',
      'src/third_party/xdg-utils':
         '/trunk/deps/third_party/xdg-utils@29103',
      'src/third_party/ffmpeg/binaries/chromium/linux/x64_dbg':
         '/trunk/deps/third_party/ffmpeg/binaries/linux_64_dbg@28488',
      'src/third_party/ffmpeg/binaries/chromium/linux/ia32':
         '/trunk/deps/third_party/ffmpeg/binaries/linux@28488',
      'src/third_party/ffmpeg/binaries/chromium/linux/ia32_dbg':
         '/trunk/deps/third_party/ffmpeg/binaries/linux_dbg@28488',
      'src/third_party/ffmpeg/binaries/chromium/linux/x64':
         '/trunk/deps/third_party/ffmpeg/binaries/linux_64@28488',
   },
}

deps = {
   'src/chrome/test/data/layout_tests/LayoutTests/fast/events':
      'http://svn.webkit.org/repository/webkit/trunk/LayoutTests/fast/events@49830',
   'src/third_party/WebKit/WebKit/chromium':
      'http://svn.webkit.org/repository/webkit/trunk/WebKit/chromium@49830',
   'src/third_party/pywebsocket':
      'http://pywebsocket.googlecode.com/svn/trunk/src@45',
   'src/third_party/WebKit/WebCore':
      'http://svn.webkit.org/repository/webkit/trunk/WebCore@49830',
   'src/breakpad/src':
      'http://google-breakpad.googlecode.com/svn/trunk/src@417',
   'src/chrome/test/data/layout_tests/LayoutTests/fast/workers':
      'http://svn.webkit.org/repository/webkit/trunk/LayoutTests/fast/workers@49830',
   'src/third_party/tcmalloc/tcmalloc':
      'http://google-perftools.googlecode.com/svn/trunk@74',
   'src/googleurl':
      'http://google-url.googlecode.com/svn/trunk@120',
   'src/chrome/test/data/layout_tests/LayoutTests/http/tests/xmlhttprequest':
      'http://svn.webkit.org/repository/webkit/trunk/LayoutTests/http/tests/xmlhttprequest@49830',
   'src/third_party/protobuf2/src':
      'http://protobuf.googlecode.com/svn/trunk@219',
   'src/chrome/test/data/layout_tests/LayoutTests/http/tests/workers':
      'http://svn.webkit.org/repository/webkit/trunk/LayoutTests/http/tests/workers@49830',
   'src/tools/gyp':
      'http://gyp.googlecode.com/svn/trunk@707',
   'src/sdch/open-vcdiff':
      'http://open-vcdiff.googlecode.com/svn/trunk@28',
   'src/third_party/WebKit/JavaScriptCore':
      'http://svn.webkit.org/repository/webkit/trunk/JavaScriptCore@49830',
   'src/chrome/test/data/layout_tests/LayoutTests/fast/js/resources':
      'http://svn.webkit.org/repository/webkit/trunk/LayoutTests/fast/js/resources@49830',
   'src/native_client':
      'http://nativeclient.googlecode.com/svn/trunk/src/native_client@868',
   'src/tools/page_cycler/acid3':
      '/trunk/deps/page_cycler/acid3@19546',
   'src/build/util/support':
      '/trunk/deps/support@29627',
   'src/chrome/test/data/layout_tests/LayoutTests/storage/domstorage':
      'http://svn.webkit.org/repository/webkit/trunk/LayoutTests/storage/domstorage@49830',
   'src/chrome/tools/test/reference_build':
      '/trunk/deps/reference_builds@25513',
   'src/v8':
      'http://v8.googlecode.com/svn/trunk@3082',
   'src/chrome/test/data/layout_tests/LayoutTests/http/tests/resources':
      'http://svn.webkit.org/repository/webkit/trunk/LayoutTests/http/tests/resources@49830',
   'src/third_party/WebKit/LayoutTests':
      'http://svn.webkit.org/repository/webkit/trunk/LayoutTests@49830',
   'src':
      '/branches/223/src@29618',
   'src/third_party/icu':
      '/trunk/deps/third_party/icu42@29177',
   'src/chrome/app/theme/google_chrome':
      '/theme@3451',
   'src/third_party/WebKit':
      '/trunk/deps/third_party/WebKit@27313',
   'src/third_party/hunspell':
      '/trunk/deps/third_party/hunspell128@28983',
   'src/testing/gtest':
      'http://googletest.googlecode.com/svn/trunk@329',
   'src/third_party/skia':
      'http://skia.googlecode.com/svn/trunk@376',
}

skip_child_includes =  ['breakpad', 'chrome_frame', 'gears', 'native_client', 'o3d', 'sdch', 'skia', 'testing', 'third_party', 'v8'] 

hooks =  [{'action': ['python', 'src/build/gyp_chromium'], 'pattern': '.'}, {'action': ['python', 'src/build/win/clobber_generated_headers.py', '$matching_files'], 'pattern': '\\.grd$'}, {'action': ['python', 'src/build/mac/clobber_generated_headers.py'], 'pattern': '.'}] 

include_rules =  ['+base', '+build', '+ipc', '+unicode', '+testing', '+webkit/port/platform/graphics/skia/public']