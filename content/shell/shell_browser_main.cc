// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/shell_browser_main.h"

#include <iostream>

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/sys_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/utf_string_conversions.h"
#include "content/public/browser/browser_main_runner.h"
#include "content/shell/shell_switches.h"
#include "content/shell/webkit_test_runner_host.h"
#include "net/base/net_util.h"
#include "webkit/support/webkit_support.h"

namespace {

GURL GetURLForLayoutTest(const std::string& test_name,
                         bool* enable_pixel_dumping,
                         std::string* expected_pixel_hash) {
  // A test name is formated like file:///path/to/test'--pixel-test'pixelhash
  std::string path_or_url = test_name;
  std::string pixel_switch;
  std::string pixel_hash;
  std::string::size_type separator_position = path_or_url.find('\'');
  if (separator_position != std::string::npos) {
    pixel_switch = path_or_url.substr(separator_position + 1);
    path_or_url.erase(separator_position);
  }
  separator_position = pixel_switch.find('\'');
  if (separator_position != std::string::npos) {
    pixel_hash = pixel_switch.substr(separator_position + 1);
    pixel_switch.erase(separator_position);
  }
  if (enable_pixel_dumping) {
    *enable_pixel_dumping =
        (pixel_switch == "--pixel-test" || pixel_switch == "-p");
  }
  if (expected_pixel_hash)
    *expected_pixel_hash = pixel_hash;
  GURL test_url(path_or_url);
  if (!(test_url.is_valid() && test_url.has_scheme())) {
#if defined(OS_WIN)
    std::wstring wide_path_or_url =
        base::SysNativeMBToWide(path_or_url);
    test_url = net::FilePathToFileURL(FilePath(wide_path_or_url));
#else
    test_url = net::FilePathToFileURL(FilePath(path_or_url));
#endif
  }
  FilePath local_path;
  if (net::FileURLToFilePath(test_url, &local_path)) {
    // We're outside of the message loop here, and this is a test.
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    file_util::SetCurrentDirectory(local_path.DirName());
  }
  return test_url;
}

bool GetNextTest(const CommandLine::StringVector& args,
                 size_t* position,
                 std::string* test) {
  if (*position >= args.size())
    return false;
  if (args[*position] == FILE_PATH_LITERAL("-"))
    return !!std::getline(std::cin, *test, '\n');
#if defined(OS_WIN)
  *test = WideToUTF8(args[(*position)++]);
#else
  *test = args[(*position)++];
#endif
  return true;
}

}  // namespace

// Main routine for running as the Browser process.
int ShellBrowserMain(const content::MainFunctionParams& parameters) {
  scoped_ptr<content::BrowserMainRunner> main_runner_(
      content::BrowserMainRunner::Create());

  int exit_code = main_runner_->Initialize(parameters);

  if (exit_code >= 0)
    return exit_code;

  if (CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kCheckLayoutTestSysDeps)) {
    return 0;
  }

  bool layout_test_mode =
      CommandLine::ForCurrentProcess()->HasSwitch(switches::kDumpRenderTree);

  if (layout_test_mode) {
    content::WebKitTestController test_controller;
    std::string test_string;
    CommandLine::StringVector args =
        CommandLine::ForCurrentProcess()->GetArgs();
    size_t command_line_position = 0;

#if defined(OS_ANDROID)
    std::cout << "#READY\n";
    std::cout.flush();
#endif

    while (GetNextTest(args, &command_line_position, &test_string)) {
      if (test_string.empty())
        continue;
      if (test_string == "QUIT")
        break;

      bool enable_pixel_dumps;
      std::string pixel_hash;
      GURL test_url = GetURLForLayoutTest(
          test_string, &enable_pixel_dumps, &pixel_hash);
      if (!content::WebKitTestController::Get()->PrepareForLayoutTest(
              test_url, enable_pixel_dumps, pixel_hash)) {
        break;
      }

      main_runner_->Run();

      if (!content::WebKitTestController::Get()->ResetAfterLayoutTest())
        break;
    }
    exit_code = 0;
  } else {
    exit_code = main_runner_->Run();
  }

  main_runner_->Shutdown();

  return exit_code;
}
