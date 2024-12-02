# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import logging
from cStringIO import StringIO
from zipfile import BadZipfile, ZipFile

import appengine_blobstore as blobstore
from appengine_url_fetcher import AppEngineUrlFetcher
from appengine_wrappers import urlfetch
from file_system import FileNotFoundError, FileSystem, StatInfo
from future import Future
from object_store_creator import ObjectStoreCreator
import url_constants

_GITHUB_REPOS_NAMESPACE = 'GithubRepos'


def _LoadCredentials(object_store_creator):
  '''Returns (username, password) from |password_store|.
  '''
  password_store = object_store_creator.Create(
      GithubFileSystem,
      app_version=None,
      category='password',
      start_empty=False)
  # return 'test_username', 'test_password'
  password_data = password_store.GetMulti(('username', 'password')).Get()
  return password_data.get('username'), password_data.get('password')


class _Gettable(object):
  '''Wrap a callable |f| such that calling .Get on a _Gettable is the same as
  calling |f| directly.
  '''
  def __init__(self, f, *args):
    self._g = lambda: f(*args)
  def Get(self):
    return self._g()


class GithubFileSystem(FileSystem):
  '''Allows reading from a github.com repository.
  '''
  @staticmethod
  def Create(owner, repo, object_store_creator):
    '''Creates a GithubFileSystem that corresponds to a single github repository
    specified by |owner| and |repo|.
    '''
    return GithubFileSystem(
        url_constants.NEW_GITHUB_URL,
        owner,
        repo,
        object_store_creator,
        AppEngineUrlFetcher)

  @staticmethod
  def ForTest(repo, fake_fetcher, path=None):
    '''Creates a GithubFIleSystem that can be used for testing. It reads zip
    files and commit data from server2/test_data/github_file_system/test_owner
    instead of github.com. It reads from files specified by |repo|.
    '''
    return GithubFileSystem(
        path if path is not None else 'test_data/github_file_system',
        'test_owner',
        repo,
        ObjectStoreCreator.ForTest(),
        fake_fetcher)

  def __init__(self, base_url, owner, repo, object_store_creator, Fetcher):
    self._repo_key = '%s/%s' % (owner, repo)
    self._repo_url = '%s/%s/%s' % (base_url, owner, repo)

    self._blobstore = blobstore.AppEngineBlobstore()
    # Lookup the chrome github api credentials.
    self._username, self._password = _LoadCredentials(object_store_creator)
    self._fetcher = Fetcher(self._repo_url)

    self._stat_cache = object_store_creator.Create(
        GithubFileSystem, category='stat-cache')

    self._repo_zip = Future(value=None)

  def _GetNamelist(self):
    '''Returns a list of all file names in a repository zip file.
    '''
    zipfile = self._repo_zip.Get()
    if zipfile is None:
      return []

    return zipfile.namelist()

  def _GetVersion(self):
    '''Returns the currently cached version of the repository. The version is a
    'sha' hash value.
    '''
    return self._stat_cache.Get(self._repo_key).Get()

  def _FetchLiveVersion(self):
    '''Fetches the current repository version from github.com and returns it.
    The version is a 'sha' hash value.
    '''
    result = self._fetcher.Fetch(
        'commits/HEAD', username=self._username, password=self._password)

    return json.loads(result.content)['commit']['tree']['sha']

  def Refresh(self):
    '''Compares the cached and live stat versions to see if the cached
    repository is out of date. If it is, an async fetch is started and a
    Future is returned. When this Future is evaluated, the fetch will be
    completed and the results cached.

    If no update is needed, None will be returned.
    '''
    version = self._FetchLiveVersion()
    repo_zip_url = self._repo_url + '/zipball'

    def persist_fetch(fetch):
      '''Completes |fetch| and stores the results in blobstore.
      '''
      try:
        blob = fetch.Get().content
      except urlfetch.DownloadError:
        logging.error(
            '%s: Failed to download zip file from repository %s' % repo_zip_url)
      else:
        try:
          zipfile = ZipFile(StringIO(blob))
        except BadZipfile as error:
          logging.error(
              '%s: Bad zip file returned from url %s' % (error, repo_zip_url))
        else:
          self._blobstore.Set(repo_zip_url, blob, _GITHUB_REPOS_NAMESPACE)
          self._repo_zip = Future(value=zipfile)
          self._stat_cache.Set(self._repo_key, version)

    # If the cached and live stat versions are different fetch the new repo.
    if version != self._stat_cache.Get('stat').Get():
      fetch = self._fetcher.FetchAsync(
          'zipball', username=self._username, password=self._password)
      return Future(delegate=_Gettable(lambda: persist_fetch(fetch)))

    return Future(value=None)

  def Read(self, paths, binary=False):
    '''Returns a directory mapping |paths| to the contents of the file at each
    path. If path ends with a '/', it is treated as a directory and is mapped to
    a list of filenames in that directory.

    |binary| is ignored.
    '''
    names = self._GetNamelist()
    if not names:
      # No files in this repository.
      raise FileNotFoundError('No paths can be found, repository is empty')
    else:
      prefix = names[0].split('/')[0]

    reads = {}
    for path in paths:
      full_path = prefix + path
      if path.endswith('/'):  # If path is a directory...
        trimmed_paths = []
        for f in filter(lambda s: s.startswith(full_path), names):
          if not '/' in f[len(full_path):-1] and not f == full_path:
            trimmed_paths.append(f[len(full_path):])
        reads[path] = trimmed_paths
      else:
        try:
          reads[path] = self._repo_zip.Get().read(full_path)
        except KeyError as error:
          raise FileNotFoundError(error)

    return Future(value=reads)

  def Stat(self, path):
    '''Stats |path| returning its version as as StatInfo object. If |path| ends
    with a '/', it is assumed to be a directory and the StatInfo object returned
    includes child_versions for all paths in the directory.

    File paths do not include the name of the zip file, which is arbitrary and
    useless to consumers.

    Because the repository will only be downloaded once per server version, all
    stat versions are always 0.
    '''
    # Trim off the zip file's name.
    trimmed = ['/' + f.split('/', 1)[1] for f in self._GetNamelist()]

    if path not in trimmed:
      raise FileNotFoundError("No stat found for '%s'" % path)

    version = self._GetVersion()
    child_paths = {}
    if path.endswith('/'):
      # Deal with a directory
      for f in filter(lambda s: s.startswith(path), trimmed):
        filename = f[len(path):]
        if not '/' in filename and not f == path:
          child_paths[filename] = StatInfo(version)

    return StatInfo(version, child_paths or None)

  def GetIdentity(self):
    return '%s(%s)' % (self.__class__.__name__, self._repo_key)
