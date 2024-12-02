# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import logging
import os

import appengine_blobstore as blobstore
import object_store
from file_system import FileSystem, StatInfo, FileNotFoundError
from StringIO import StringIO
from future import Future
from zipfile import ZipFile, BadZipfile

ZIP_KEY = 'zipball'

def _MakeKey(version):
  return ZIP_KEY + '.' + str(version)

class _AsyncFetchFutureZip(object):
  def __init__(self, fetcher, blobstore, key_to_set, key_to_delete=None):
    self._fetch = fetcher.FetchAsync(ZIP_KEY)
    self._blobstore = blobstore
    self._key_to_set = key_to_set
    self._key_to_delete = key_to_delete

  def Get(self):
    try:
      blob = self._fetch.Get().content
    except FileNotFoundError as e:
      logging.error('Bad github zip file: %s' % e)
      return None
    self._blobstore.Set(_MakeKey(self._key_to_set),
                        blob,
                        blobstore.BLOBSTORE_GITHUB)
    if self._key_to_delete is not None:
      self._blobstore.Delete(_MakeKey(self._key_to_delete),
                             blobstore.BLOBSTORE_GITHUB)
    try:
      return ZipFile(StringIO(blob))
    except BadZipfile as e:
      logging.error('Bad github zip file: %s' % e)
      return None

class GithubFileSystem(FileSystem):
  """FileSystem implementation which fetches resources from github.
  """
  def __init__(self, fetcher, object_store, blobstore):
    self._fetcher = fetcher
    self._object_store = object_store
    self._blobstore = blobstore
    self._version = None
    self._GetZip(self.Stat(ZIP_KEY).version)

  def _GetZip(self, version):
    blob = self._blobstore.Get(_MakeKey(version), blobstore.BLOBSTORE_GITHUB)
    if blob is not None:
      try:
        self._zip_file = Future(value=ZipFile(StringIO(blob)))
      except BadZipfile as e:
        self._blobstore.Delete(_MakeKey(version), blobstore.BLOBSTORE_GITHUB)
        logging.error('Bad github zip file: %s' % e)
        self._zip_file = Future(value=None)
    else:
      self._zip_file = Future(
          delegate=_AsyncFetchFutureZip(self._fetcher,
                                        self._blobstore,
                                        version,
                                        key_to_delete=self._version))
    self._version = version

  def _ReadFile(self, path):
    zip_file = self._zip_file.Get()
    if zip_file is None:
      logging.error('Bad github zip file.')
      return ''
    prefix = zip_file.namelist()[0][:-1]
    return zip_file.read(prefix + path)

  def _ListDir(self, path):
    zip_file = self._zip_file.Get()
    if zip_file is None:
      logging.error('Bad github zip file.')
      return []
    filenames = zip_file.namelist()
    # Take out parent directory name (GoogleChrome-chrome-app-samples-c78a30f)
    filenames = [f[len(filenames[0]) - 1:] for f in filenames]
    # Remove the path of the directory we're listing from the filenames.
    filenames = [f[len(path):] for f in filenames
                 if f != path and f.startswith(path)]
    # Remove all files not directly in this directory.
    return [f for f in filenames if f[:-1].count('/') == 0]

  def Read(self, paths, binary=False):
    version = self.Stat(ZIP_KEY).version
    if version != self._version:
      self._GetZip(version)
    result = {}
    for path in paths:
      if path.endswith('/'):
        result[path] = self._ListDir(path)
      else:
        result[path] = self._ReadFile(path)
    return Future(value=result)

  def Stat(self, path):
    version = self._object_store.Get(path, object_store.GITHUB_STAT).Get()
    if version is not None:
      return StatInfo(version)
    version = (json.loads(
        self._fetcher.Fetch('commits/HEAD').content).get('commit', {})
                                                    .get('tree', {})
                                                    .get('sha', None))
    # Check if the JSON was valid, and set to 0 if not.
    if version is not None:
      self._object_store.Set(path, version, object_store.GITHUB_STAT)
    else:
      logging.warning('Problem fetching commit hash from github.')
      version = 0
      # Cache for a minute so we don't try to keep fetching bad data.
      self._object_store.Set(path, version, object_store.GITHUB_STAT, time=60)
    return StatInfo(version)
