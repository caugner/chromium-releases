# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""End to end tests for ChromeDriver."""

import ctypes
import os
import sys
import unittest

import chromedriver


class ChromeDriverTest(unittest.TestCase):
  """End to end tests for ChromeDriver."""

  def testStartStop(self):
    driver = chromedriver.ChromeDriver(_CHROMEDRIVER_LIB)
    driver.Quit()

if __name__ == '__main__':
  if len(sys.argv) != 2:
    print 'Usage: %s <path_to_chromedriver_so>' % __file__
    sys.exit(1)
  global _CHROMEDRIVER_LIB
  _CHROMEDRIVER_LIB = os.path.abspath(sys.argv[1])
  all_tests_suite = unittest.defaultTestLoader.loadTestsFromModule(
      sys.modules[__name__])
  unittest.TextTestRunner().run(all_tests_suite)
