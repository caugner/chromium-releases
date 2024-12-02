// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

/**
 * Utilities for FileOperationManager.
 */
var fileOperationUtil = {};

/**
 * Simple wrapper for util.deduplicatePath. On error, this method translates
 * the FileError to FileOperationManager.Error object.
 *
 * @param {DirectoryEntry} dirEntry The target directory entry.
 * @param {string} relativePath The path to be deduplicated.
 * @param {function(string)} successCallback Callback run with the deduplicated
 *     path on success.
 * @param {function(FileOperationManager.Error)} errorCallback Callback run on
 *     error.
 */
fileOperationUtil.deduplicatePath = function(
    dirEntry, relativePath, successCallback, errorCallback) {
  util.deduplicatePath(
      dirEntry, relativePath, successCallback,
      function(err) {
        var onFileSystemError = function(error) {
          errorCallback(new FileOperationManager.Error(
              util.FileOperationErrorType.FILESYSTEM_ERROR, error));
        };

        if (err.code == FileError.PATH_EXISTS_ERR) {
          // Failed to uniquify the file path. There should be an existing
          // entry, so return the error with it.
          util.resolvePath(
              dirEntry, relativePath,
              function(entry) {
                errorCallback(new FileOperationManager.Error(
                    util.FileOperationErrorType.TARGET_EXISTS, entry));
              },
              onFileSystemError);
          return;
        }
        onFileSystemError(err);
      });
};

/**
 * Sets last modified date to the entry.
 * @param {Entry} entry The entry to which the last modified is set.
 * @param {Date} modificationTime The last modified time.
 */
fileOperationUtil.setLastModified = function(entry, modificationTime) {
  chrome.fileBrowserPrivate.setLastModified(
      entry.toURL(), '' + Math.round(modificationTime.getTime() / 1000));
};

/**
 * CAUTION: THIS IS STILL UNDER DEVELOPMENT. DO NOT USE.
 * This will replace copyRecursively defined below.
 *
 * Copies source to parent with the name newName recursively.
 * This should work very similar to FileSystem API's copyTo. The difference is;
 * - The progress callback is supported.
 * - The cancellation is supported.
 *
 * @param {Entry} source The entry to be copied.
 * @param {DirectoryEntry} parent The entry of the destination directory.
 * @param {string} newName The name of copied file.
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function(Entry, number)} progressCallback Callback invoked
 *     periodically during the copying. It takes source and the number of
 *     copied bytes since the last invocation.
 * @param {function(Entry)} successCallback Callback invoked when the copy
 *     is successfully done with the entry of the created entry.
 * @param {function(FileError)} errorCallback Callback invoked when an error
 *     is found.
 * @return {function()} Callback to cancel the current file copy operation.
 *     When the cancel is done, errorCallback will be called. The returned
 *     callback must not be called more than once.
 */
fileOperationUtil.copyTo = function(
    source, parent, newName, entryChangedCallback, progressCallback,
    successCallback, errorCallback) {
  var copyId = null;
  var pendingCallbacks = [];
  var resolveQueue = new AsyncUtil.Queue();
  var lastProgressSize = 0;

  var onCopyProgress = function(progressCopyId, status) {
    if (copyId == null) {
      // If the copyId is not yet available, wait for it.
      pendingCallbacks.push(
          onCopyProgress.bind(null, progressCopyId, status));
      return;
    }

    // This is not what we're interested in.
    if (progressCopyId != copyId)
      return;

    switch (status.type) {
      case 'begin_entry_copy':
        resolveQueue.run(function(callback) {
          webkitResolveFileSystemURL(status.url, function(entry) {
            // Notify progress to update the status.
            // This will be used to update the entry currently being copied.
            lastProgressSize = 0;
            progressCallback(entry, 0);
            callback();
          });
        });
        break;

      case 'end_entry_copy':
        resolveQueue.run(function(callback) {
          webkitResolveFileSystemURL(status.url, function(entry) {
            // The entry is created.
            entryChangedCallback(util.EntryChangedKind.CREATED, entry);
            callback();
          });
        });
        break;

      case 'progress':
        resolveQueue.run(function(callback) {
          webkitResolveFileSystemURL(status.url, function(entry) {
            // For the backword compatibility, here the reported size is the
            // diff since the last invocation.
            // TODO(hidehiko): Fix this to report the size directly.
            var updateSize = status.size - lastProgressSize;
            lastProgressSize = status.size;
            progressCallback(entry, updateSize);
            callback();
          });
        });
        break;

      case 'success':
        chrome.fileBrowserPrivate.onCopyProgress.removeListener(onCopyProgress);
        resolveQueue.run(function(callback) {
          webkitResolveFileSystemURL(status.url, function(entry) {
            successCallback(entry);
            // Do not call callback here, because this is the completion
            // callback.
          });
        });
        break;

      case 'error':
        chrome.fileBrowserPrivate.onCopyProgress.removeListener(onCopyProgress);
        resolveQueue.run(function(callback) {
          errorCallback(util.createFileError(status.error));
          // Do not call callback here, because this is the completion
          // callback.
        });
        break;

      default:
        // Found unknown state. Cancel the task, and return an error.
        console.error('Unknown progress type: ' + status.type);
        chrome.fileBrowserPrivate.onCopyProgress.removeListener(onCopyProgress);
        chrome.fileBrowserPrivate.cancelCopy(copyId);
        resolveQueue.run(function(callback) {
          errorCallback(util.createFileError(FileError.INVALID_STATE_ERR));
        });

    }
  };

  // Register the listener before calling startCopy. Otherwise some events
  // would be lost.
  chrome.fileBrowserPrivate.onCopyProgress.addListener(onCopyProgress);

  // Then starts the copy.
  chrome.fileBrowserPrivate.startCopy(
      source.toURL(), parent.toURL(), newName, function(startCopyId) {
        // last error contains the FileError code on error.
        if (chrome.runtime.lastError) {
          // Unsubscribe the progress listener.
          chrome.fileBrowserPrivate.onCopyProgress.removeListener(
              onCopyProgress);
          errorCallback(util.createFileError(
              Integer.parseInt(chrome.runtime.lastError, 10)));
          return;
        }

        copyId = startCopyId;
        for (var i = 0; i < pendingCallbacks.length; i++) {
          pendingCallbacks[i]();
        }
      });

  return function() {
    // If copyId is not yet available, wait for it.
    if (copyId == null) {
      pendingCallbacks.push(function() {
        chrome.fileBrowserPrivate.cancelCopy(copyId);
      });
      return;
    }

    chrome.fileBrowserPrivate.cancelCopy(copyId);
  };
};

/**
 * Copies source to parent with the name newName recursively.
 *
 * @param {Entry} source The entry to be copied.
 * @param {DirectoryEntry} parent The entry of the destination directory.
 * @param {string} newName The name of copied file.
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function(Entry, number)} progressCallback Callback invoked
 *     periodically during the copying. It takes source and the number of
 *     copied bytes since the last invocation.
 * @param {function(Entry)} successCallback Callback invoked when the copy
 *     is successfully done with the entry of the created entry.
 * @param {function(FileError)} errorCallback Callback invoked when an error
 *     is found.
 * @return {function()} Callback to cancel the current file copy operation.
 *     When the cancel is done, errorCallback will be called. The returned
 *     callback must not be called more than once.
 */
fileOperationUtil.copyRecursively = function(
    source, parent, newName, entryChangedCallback, progressCallback,
    successCallback, errorCallback) {
  // Notify that the copy begins for each entry.
  progressCallback(source, 0);

  // If the entry is a file, redirect it to copyFile_().
  if (source.isFile) {
    return fileOperationUtil.copyFile_(
        source, parent, newName, progressCallback,
        function(entry) {
          entryChangedCallback(util.EntryChangedKind.CREATED, entry);
          successCallback(entry);
        },
        errorCallback);
  }

  // Hereafter, the source is directory.
  var cancelRequested = false;
  var cancelCallback = null;

  // First, we create the directory copy.
  parent.getDirectory(
      newName, {create: true, exclusive: true},
      function(dirEntry) {
        entryChangedCallback(util.EntryChangedKind.CREATED, dirEntry);
        if (cancelRequested) {
          errorCallback(util.createFileError(FileError.ABORT_ERR));
          return;
        }

        // Iterate on children, and copy them recursively.
        util.forEachDirEntry(
            source,
            function(child, callback) {
              if (cancelRequested) {
                errorCallback(util.createFileError(FileError.ABORT_ERR));
                return;
              }

              cancelCallback = fileOperationUtil.copyRecursively(
                  child, dirEntry, child.name, entryChangedCallback,
                  progressCallback,
                  function() {
                    cancelCallback = null;
                    callback();
                  },
                  function(error) {
                    cancelCallback = null;
                    errorCallback(error);
                  });
            },
            function() {
              successCallback(dirEntry);
            },
            errorCallback);
      },
      errorCallback);

  return function() {
    cancelRequested = true;
    if (cancelCallback) {
      cancelCallback();
      cancelCallback = null;
    }
  };
};

/**
 * Copies a file from source to the parent directory with newName.
 * See also copyFileByStream_ and copyFileOnDrive_ for the implementation
 * details.
 *
 * @param {FileEntry} source The file entry to be copied.
 * @param {DirectoryEntry} parent The entry of the destination directory.
 * @param {string} newName The name of copied file.
 * @param {function(FileEntry, number)} progressCallback Callback invoked
 *     periodically during the file writing. It takes source and the number of
 *     copied bytes since the last invocation. This is also called just before
 *     starting the operation (with '0' bytes) and just after the finishing the
 *     operation (with the total copied size).
 * @param {function(FileEntry)} successCallback Callback invoked when the copy
 *     is successfully done with the entry of the created file.
 * @param {function(FileError)} errorCallback Callback invoked when an error
 *     is found.
 * @return {function()} Callback to cancel the current file copy operation.
 *     When the cancel is done, errorCallback will be called. The returned
 *     callback must not be called more than once.
 * @private
 */
fileOperationUtil.copyFile_ = function(
    source, parent, newName, progressCallback, successCallback, errorCallback) {
  if (!PathUtil.isDriveBasedPath(source.fullPath) &&
      !PathUtil.isDriveBasedPath(parent.fullPath)) {
    // Copying a file between non-Drive file systems.
    return fileOperationUtil.copyFileByStream_(
        source, parent, newName, progressCallback, successCallback,
        errorCallback);
  } else {
    // Copying related to the Drive file system.
    return fileOperationUtil.copyFileOnDrive_(
        source, parent, newName, progressCallback, successCallback,
        errorCallback);
  }
};

/**
 * Copies a file by using File and FileWriter objects.
 *
 * This is a js-implementation of FileEntry.copyTo(). Unfortunately, copyTo
 * doesn't support periodical progress updating nor cancelling. To support
 * these operations, this method implements copyTo by streaming way in
 * JavaScript.
 *
 * Note that this is designed for file copying on local file system. We have
 * some special cases about copying on Drive file system. See also
 * copyFileOnDrive_() for more details.
 *
 * @param {FileEntry} source The file entry to be copied.
 * @param {DirectoryEntry} parent The entry of the destination directory.
 * @param {string} newName The name of copied file.
 * @param {function(FileEntry, number)} progressCallback Callback invoked
 *     periodically during the file writing. It takes source and the number of
 *     copied bytes since the last invocation. This is also called just before
 *     starting the operation (with '0' bytes) and just after the finishing the
 *     operation (with the total copied size).
 * @param {function(FileEntry)} successCallback Callback invoked when the copy
 *     is successfully done with the entry of the created file.
 * @param {function(FileError)} errorCallback Callback invoked when an error
 *     is found.
 * @return {function()} Callback to cancel the current file copy operation.
 *     When the cancel is done, errorCallback will be called. The returned
 *     callback must not be called more than once.
 * @private
 */
fileOperationUtil.copyFileByStream_ = function(
    source, parent, newName, progressCallback, successCallback, errorCallback) {
  // Set to true when cancel is requested.
  var cancelRequested = false;

  source.file(function(file) {
    if (cancelRequested) {
      errorCallback(util.createFileError(FileError.ABORT_ERR));
      return;
    }

    parent.getFile(newName, {create: true, exclusive: true}, function(target) {
      if (cancelRequested) {
        errorCallback(util.createFileError(FileError.ABORT_ERR));
        return;
      }

      target.createWriter(function(writer) {
        if (cancelRequested) {
          errorCallback(util.createFileError(FileError.ABORT_ERR));
          return;
        }

        writer.onerror = writer.onabort = function(progress) {
          errorCallback(cancelRequested ?
              util.createFileError(FileError.ABORT_ERR) :
                  writer.error);
        };

        var reportedProgress = 0;
        writer.onprogress = function(progress) {
          if (cancelRequested) {
            // If the copy was cancelled, we should abort the operation.
            // The errorCallback will be called by writer.onabort after the
            // termination.
            writer.abort();
            return;
          }

          // |progress.loaded| will contain total amount of data copied by now.
          // |progressCallback| expects data amount delta from the last progress
          // update.
          progressCallback(target, progress.loaded - reportedProgress);
          reportedProgress = progress.loaded;
        };

        writer.onwrite = function() {
          if (cancelRequested) {
            errorCallback(util.createFileError(FileError.ABORT_ERR));
            return;
          }

          source.getMetadata(function(metadata) {
            if (cancelRequested) {
              errorCallback(util.createFileError(FileError.ABORT_ERR));
              return;
            }

            fileOperationUtil.setLastModified(
                target, metadata.modificationTime);
            successCallback(target);
          }, errorCallback);
        };

        writer.write(file);
      }, errorCallback);
    }, errorCallback);
  }, errorCallback);

  return function() { cancelRequested = true; };
};

/**
 * Copies a file a) from Drive to local, b) from local to Drive, or c) from
 * Drive to Drive.
 * Currently, we need to take care about following two things for Drive:
 *
 * 1) Copying hosted document.
 * In theory, it is impossible to actual copy a hosted document to other
 * file system. Thus, instead, Drive file system backend creates a JSON file
 * referring to the hosted document. Also, when it is uploaded by copyTo,
 * the hosted document is copied on the server. Note that, this doesn't work
 * when a user creates a file by FileWriter (as copyFileEntry_ does).
 *
 * 2) File transfer between local and Drive server.
 * There are two directions of file transfer; from local to Drive and from
 * Drive to local.
 * The file transfer from local to Drive is done as a part of file system
 * background sync (kicked after the copy operation is done). So we don't need
 * to take care about it here. To copy the file from Drive to local (or Drive
 * to Drive with GData WAPI), we need to download the file content (if it is
 * not locally cached). During the downloading, we can listen the periodical
 * updating and cancel the downloding via private API.
 *
 * This function supports progress updating and cancelling partially.
 * Unfortunately, FileEntry.copyTo doesn't support progress updating nor
 * cancelling, so we support them only during file downloading.
 *
 * Note: we're planning to move copyTo logic into c++ side. crbug.com/261492
 *
 * @param {FileEntry} source The entry of the file to be copied.
 * @param {DirectoryEntry} parent The entry of the destination directory.
 * @param {string} newName The name of the copied file.
 * @param {function(FileEntry, number)} progressCallback Callback periodically
 *     invoked during file transfer with the source and the number of
 *     transferred bytes from the last call.
 * @param {function(FileEntry)} successCallback Callback invoked when the
 *     file copy is successfully done with the entry of the copied file.
 * @param {function(FileError)} errorCallback Callback invoked when an error
 *     is found.
 * @return {function()} Callback to cancel the current file copy operation.
 *     When the cancel is done, errorCallback will be called. The returned
 *     callback must not be called more than once.
 * @private
 */
fileOperationUtil.copyFileOnDrive_ = function(
    source, parent, newName, progressCallback, successCallback, errorCallback) {
  // Set to true when cancel is requested.
  var cancelRequested = false;
  var cancelCallback = null;

  var onCopyToCompleted = null;

  // Progress callback.
  // Because the uploading the file from local cache to Drive server will be
  // done as a part of background Drive file system sync, so for this copy
  // operation, what we need to take care about is only file downloading.
  var numTransferredBytes = 0;
  if (PathUtil.isDriveBasedPath(source.fullPath)) {
    var sourceUrl = source.toURL();
    var sourcePath = util.extractFilePath(sourceUrl);
    var onFileTransfersUpdated = function(statusList) {
      for (var i = 0; i < statusList.length; i++) {
        var status = statusList[i];

        // Comparing urls is unreliable, since they may use different
        // url encoding schemes (eg. rfc2396 vs. rfc3986).
        var filePath = util.extractFilePath(status.fileUrl);
        if (filePath == sourcePath) {
          var processed = status.processed;
          if (processed > numTransferredBytes) {
            progressCallback(source, processed - numTransferredBytes);
            numTransferredBytes = processed;
          }
          return;
        }
      }
    };

    // Subscribe to listen file transfer updating notifications.
    chrome.fileBrowserPrivate.onFileTransfersUpdated.addListener(
        onFileTransfersUpdated);

    // Currently, we do NOT upload the file during the copy operation.
    // It will be done as a part of file system sync after copy operation.
    // So, we can cancel only file downloading.
    cancelCallback = function() {
      chrome.fileBrowserPrivate.cancelFileTransfers(
          [sourceUrl], function() {});
    };

    // We need to clean up on copyTo completion regardless if it is
    // successfully done or not.
    onCopyToCompleted = function() {
      cancelCallback = null;
      chrome.fileBrowserPrivate.onFileTransfersUpdated.removeListener(
          onFileTransfersUpdated);
    };
  }

  source.copyTo(
      parent, newName,
      function(entry) {
        if (onCopyToCompleted)
          onCopyToCompleted();

        if (cancelRequested) {
          errorCallback(util.createFileError(FileError.ABORT_ERR));
          return;
        }

        entry.getMetadata(function(metadata) {
          if (metadata.size > numTransferredBytes)
            progressCallback(source, metadata.size - numTransferredBytes);
          successCallback(entry);
        }, errorCallback);
      },
      function(error) {
        if (onCopyToCompleted)
          onCopyToCompleted();

        errorCallback(error);
      });

  return function() {
    cancelRequested = true;
    if (cancelCallback) {
      cancelCallback();
      cancelCallback = null;
    }
  };
};

/**
 * Thin wrapper of chrome.fileBrowserPrivate.zipSelection to adapt its
 * interface similar to copyTo().
 *
 * @param {Array.<Entry>} sources The array of entries to be archived.
 * @param {DirectoryEntry} parent The entry of the destination directory.
 * @param {string} newName The name of the archive to be created.
 * @param {function(FileEntry)} successCallback Callback invoked when the
 *     operation is successfully done with the entry of the created archive.
 * @param {function(FileError)} errorCallback Callback invoked when an error
 *     is found.
 */
fileOperationUtil.zipSelection = function(
    sources, parent, newName, successCallback, errorCallback) {
  chrome.fileBrowserPrivate.zipSelection(
      parent.toURL(),
      sources.map(function(e) { return e.toURL(); }),
      newName, function(success) {
        if (!success) {
          // Failed to create a zip archive.
          errorCallback(
              util.createFileError(FileError.INVALID_MODIFICATION_ERR));
          return;
        }

        // Returns the created entry via callback.
        parent.getFile(
            newName, {create: false}, successCallback, errorCallback);
      });
};

/**
 * @constructor
 */
function FileOperationManager() {
  this.copyTasks_ = [];
  this.deleteTasks_ = [];
  this.cancelObservers_ = [];
  this.cancelRequested_ = false;
  this.cancelCallback_ = null;
  this.unloadTimeout_ = null;

  this.eventRouter_ = new FileOperationManager.EventRouter();
}

/**
 * Get FileOperationManager instance. In case is hasn't been initialized, a new
 * instance is created.
 *
 * @return {FileOperationManager} A FileOperationManager instance.
 */
FileOperationManager.getInstance = function() {
  if (!FileOperationManager.instance_)
    FileOperationManager.instance_ = new FileOperationManager();

  return FileOperationManager.instance_;
};

/**
 * Manages cr.Event dispatching.
 * Currently this can send three types of events: "copy-progress",
 * "copy-operation-completed" and "delete".
 *
 * TODO(hidehiko): Reorganize the event dispatching mechanism.
 * @constructor
 * @extends {cr.EventTarget}
 */
FileOperationManager.EventRouter = function() {
};

/**
 * Extends cr.EventTarget.
 */
FileOperationManager.EventRouter.prototype.__proto__ = cr.EventTarget.prototype;

/**
 * Dispatches a simple "copy-progress" event with reason and current
 * FileOperationManager status. If it is an ERROR event, error should be set.
 *
 * @param {string} reason Event type. One of "BEGIN", "PROGRESS", "SUCCESS",
 *     "ERROR" or "CANCELLED". TODO(hidehiko): Use enum.
 * @param {Object} status Current FileOperationManager's status. See also
 *     FileOperationManager.getStatus().
 * @param {FileOperationManager.Error=} opt_error The info for the error. This
 *     should be set iff the reason is "ERROR".
 */
FileOperationManager.EventRouter.prototype.sendProgressEvent = function(
    reason, status, opt_error) {
  var event = new cr.Event('copy-progress');
  event.reason = reason;
  event.status = status;
  if (opt_error)
    event.error = opt_error;
  this.dispatchEvent(event);
};

/**
 * Dispatches an event to notify that an entry is changed (created or deleted).
 * @param {util.EntryChangedKind} kind The enum to represent if the entry is
 *     created or deleted.
 * @param {Entry} entry The changed entry.
 */
FileOperationManager.EventRouter.prototype.sendEntryChangedEvent = function(
    kind, entry) {
  var event = new cr.Event('entry-changed');
  event.kind = kind;
  event.entry = entry;
  this.dispatchEvent(event);
};

/**
 * Dispatches an event to notify entries are changed for delete task.
 *
 * @param {string} reason Event type. One of "BEGIN", "PROGRESS", "SUCCESS",
 *     or "ERROR". TODO(hidehiko): Use enum.
 * @param {Array.<string>} urls An array of URLs which are affected by delete
 *     operation.
 */
FileOperationManager.EventRouter.prototype.sendDeleteEvent = function(
    reason, urls) {
  var event = new cr.Event('delete');
  event.reason = reason;
  event.urls = urls;
  this.dispatchEvent(event);
};

/**
 * A record of a queued copy operation.
 *
 * Multiple copy operations may be queued at any given time.  Additional
 * Tasks may be added while the queue is being serviced.  Though a
 * cancel operation cancels everything in the queue.
 *
 * @param {Array.<Entry>} sourceEntries Array of source entries.
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @constructor
 */
FileOperationManager.Task = function(sourceEntries, targetDirEntry) {
  this.sourceEntries = sourceEntries;
  this.targetDirEntry = targetDirEntry;

  // TODO(hidehiko): When we support recursive copy, we should be able to
  // rely on originalEntries. Then remove this.
  this.entries = [];

  /**
   * The number of entries, whose processing is completed.
   * @type {number}
   */
  this.numCompletedEntries = 0;
  this.totalBytes = 0;
  this.completedBytes = 0;

  /**
   * The entry currently being processed.
   * @type {Entry}
   */
  this.processingEntry = null;

  this.deleteAfterCopy = false;
  this.move = false;
  this.zip = false;

  /**
   * Set to true when cancel is requested.
   * @private {boolean}
   */
  this.cancelRequested_ = false;

  /**
   * Callback to cancel the running process.
   * @private {function()}
   */
  this.cancelCallback_ = null;

  // TODO(hidehiko): After we support recursive copy, we don't need this.
  // If directory already exists, we try to make a copy named 'dir (X)',
  // where X is a number. When we do this, all subsequent copies from
  // inside the subtree should be mapped to the new directory name.
  // For example, if 'dir' was copied as 'dir (1)', then 'dir\file.txt' should
  // become 'dir (1)\file.txt'.
  this.renamedDirectories_ = [];
};

/**
 * @param {function()} callback When entries resolved.
 */
FileOperationManager.Task.prototype.initialize = function(callback) {
  // When moving directories, FileEntry.moveTo() is used if both source
  // and target are on Drive. There is no need to recurse into directories.
  util.recurseAndResolveEntries(
      this.sourceEntries, !this.move,
      function(result) {
        if (this.move) {
          // This may be moving from search results, where it fails if we
          // move parent entries earlier than child entries. We should
          // process the deepest entry first. Since move of each entry is
          // done by a single moveTo() call, we don't need to care about the
          // recursive traversal order.
          this.entries = result.dirEntries.concat(result.fileEntries).sort(
              function(entry1, entry2) {
                return entry2.fullPath.length - entry1.fullPath.length;
              });
        } else {
          // Copying tasks are recursively processed. So, directories must be
          // processed earlier than their child files. Since
          // util.recurseAndResolveEntries is already listing entries in the
          // recursive traversal order, we just keep the ordering.
          this.entries = result.dirEntries.concat(result.fileEntries);
        }

        this.totalBytes = result.fileBytes;
        callback();
      }.bind(this));
};

/**
 * Updates copy progress status for the entry.
 *
 * @param {number} size Number of bytes that has been copied since last update.
 */
FileOperationManager.Task.prototype.updateFileCopyProgress = function(size) {
  this.completedBytes += size;
};

/**
 * @param {string} fromName Old name.
 * @param {string} toName New name.
 */
FileOperationManager.Task.prototype.registerRename = function(
    fromName, toName) {
  this.renamedDirectories_.push({from: fromName + '/', to: toName + '/'});
};

/**
 * @param {string} path A path.
 * @return {string} Path after renames.
 */
FileOperationManager.Task.prototype.applyRenames = function(path) {
  // Directories are processed in pre-order, so we will store only the first
  // renaming point:
  // x   -> x (1)    -- new directory created.
  // x\y -> x (1)\y  -- no more renames inside the new directory, so
  //                    this one will not be stored.
  // x\y\a.txt       -- only one rename will be applied.
  for (var index = 0; index < this.renamedDirectories_.length; ++index) {
    var rename = this.renamedDirectories_[index];
    if (path.indexOf(rename.from) == 0) {
      path = rename.to + path.substr(rename.from.length);
    }
  }
  return path;
};

/**
 * Requests cancellation of this task.
 * When the cancellation is done, it is notified via callbacks of run().
 */
FileOperationManager.Task.prototype.requestCancel = function() {
  this.cancelRequested_ = true;
  if (this.cancelCallback_) {
    this.cancelCallback_();
    this.cancelCallback_ = null;
  }
};

/**
 * Runs the task. Sub classes must implement this method.
 *
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function()} progressCallback Callback invoked periodically during
 *     the operation.
 * @param {function()} successCallback Callback run on success.
 * @param {function(FileOperationManager.Error)} errorCallback Callback run on
 *     error.
 */
FileOperationManager.Task.prototype.run = function(
    entryChangedCallback, progressCallback, successCallback, errorCallback) {
};

/**
 * Task to copy entries.
 *
 * @param {Array.<Entry>} sourceEntries Array of source entries.
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @constructor
 * @extends {FileOperationManager.Task}
 */
FileOperationManager.CopyTask = function(sourceEntries, targetDirEntry) {
  FileOperationManager.Task.call(this, sourceEntries, targetDirEntry);
};

/**
 * Extends FileOperationManager.Task.
 */
FileOperationManager.CopyTask.prototype.__proto__ =
    FileOperationManager.Task.prototype;

/**
 * Copies all entries to the target directory.
 * Note: this method contains also the operation of "Move" due to historical
 * reason.
 *
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function()} progressCallback Callback invoked periodically during
 *     the copying.
 * @param {function()} successCallback On success.
 * @param {function(FileOperationManager.Error)} errorCallback On error.
 * @override
 */
FileOperationManager.CopyTask.prototype.run = function(
    entryChangedCallback, progressCallback, successCallback, errorCallback) {
  // TODO(hidehiko): We should be able to share the code to iterate on entries
  // with serviceMoveTask_().
  if (this.entries.length == 0) {
    successCallback();
    return;
  }

  // TODO(hidehiko): Delete after copy is the implementation of Move.
  // Migrate the part into MoveTask.run().
  var deleteOriginals = function() {
    var count = this.sourceEntries.length;

    var onEntryDeleted = function(entry) {
      entryChangedCallback(util.EntryChangedKind.DELETED, entry);
      count--;
      if (!count)
        successCallback();
    };

    var onFilesystemError = function(err) {
      errorCallback(new FileOperationManager.Error(
          util.FileOperationErrorType.FILESYSTEM_ERROR, err));
    };

    for (var i = 0; i < this.sourceEntries.length; i++) {
      var entry = this.sourceEntries[i];
      util.removeFileOrDirectory(
          entry, onEntryDeleted.bind(null, entry), onFilesystemError);
    }
  }.bind(this);

  AsyncUtil.forEach(
      this.sourceEntries,
      function(callback, entry, index) {
        if (this.cancelRequested_) {
          errorCallback(new FileOperationManager.Error(
              util.FileOperationErrorType.FILESYSTEM_ERROR,
              util.createFileError(FileError.ABORT_ERR)));
          return;
        }
        progressCallback();
        this.cancelCallback_ = FileOperationManager.CopyTask.processEntry_(
            entry, this.targetDirEntry,
            function(type, entry) {
              this.numCompletedEntries++;
              entryChangedCallback(type, entry);
            }.bind(this),
            function(entry, size) {
              this.processingEntry = entry;
              this.updateFileCopyProgress(size);
              progressCallback();
            }.bind(this),
            function() {
              this.cancelCallback_ = null;
              callback();
            }.bind(this),
            function(error) {
              this.cancelCallback_ = null;
              errorCallback(error);
            }.bind(this));
      },
      function() {
        if (this.deleteAfterCopy) {
          deleteOriginals();
        } else {
          successCallback();
        }
      }.bind(this),
      this);
};

/**
 * Copies the source entry to the target directory.
 *
 * @param {Entry} sourceEntry An entry to be copied.
 * @param {DirectoryEntry} destinationEntry The entry which will contain the
 *     copied entry.
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function(Entry, number)} progressCallback Callback invoked
 *     periodically during the copying.
 * @param {function()} successCallback On success.
 * @param {function(FileOperationManager.Error)} errorCallback On error.
 * @return {function()} Callback to cancel the current file copy operation.
 *     When the cancel is done, errorCallback will be called. The returned
 *     callback must not be called more than once.
 * @private
 */
FileOperationManager.CopyTask.processEntry_ = function(
    sourceEntry, destinationEntry, entryChangedCallback, progressCallback,
    successCallback, errorCallback) {
  var cancelRequested = false;
  var cancelCallback = null;
  fileOperationUtil.deduplicatePath(
      destinationEntry, sourceEntry.name,
      function(destinationName) {
        if (cancelRequested) {
          errorCallback(new FileOperationManager.Error(
              util.FileOperationErrorType.FILESYSTEM_ERROR,
              util.createFileError(FileError.ABORT_ERR)));
          return;
        }

        cancelCallback = fileOperationUtil.copyRecursively(
            sourceEntry, destinationEntry, destinationName,
            entryChangedCallback, progressCallback,
            function(entry) {
              cancelCallback = null;
              successCallback();
            },
            function(error) {
              cancelCallback = null;
              errorCallback(new FileOperationManager.Error(
                  util.FileOperationErrorType.FILESYSTEM_ERROR, error));
            });
      },
      errorCallback);

  return function() {
    cancelRequested = true;
    if (cancelCallback) {
      cancelCallback();
      cancelCallback = null;
    }
  };
};

/**
 * Task to move entries.
 *
 * @param {Array.<Entry>} sourceEntries Array of source entries.
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @constructor
 * @extends {FileOperationManager.Task}
 */
FileOperationManager.MoveTask = function(sourceEntries, targetDirEntry) {
  FileOperationManager.Task.call(this, sourceEntries, targetDirEntry);
  // TODO(hidehiko): We should handle dispatching copy/move/zip more nicely.
  this.move = true;
};

/**
 * Extends FileOperationManager.Task.
 */
FileOperationManager.MoveTask.prototype.__proto__ =
    FileOperationManager.Task.prototype;

/**
 * Moves all entries in the task.
 *
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function()} progressCallback Callback invoked periodically during
 *     the moving.
 * @param {function()} successCallback On success.
 * @param {function(FileOperationManager.Error)} errorCallback On error.
 * @override
 */
FileOperationManager.MoveTask.prototype.run = function(
    entryChangedCallback, progressCallback, successCallback, errorCallback) {
  if (this.entries.length == 0) {
    successCallback();
    return;
  }

  AsyncUtil.forEach(
      this.entries,
      function(callback, entry, index) {
        if (this.cancelRequested_) {
          errorCallback(new FileOperationManager.Error(
              util.FileOperationErrorType.FILESYSTEM_ERROR,
              util.createFileError(FileError.ABORT_ERR)));
          return;
        }
        this.processingEntry = entry;
        progressCallback();
        FileOperationManager.MoveTask.processEntry_(
            entry, this.targetDirEntry, entryChangedCallback,
            function() {
              this.numCompletedEntries++;
              callback();
            }.bind(this),
            errorCallback);
      },
      function() {
        successCallback();
      }.bind(this),
      this);
};

/**
 * Moves the sourceEntry to the targetDirEntry in this task.
 *
 * @param {Entry} sourceEntry An entry to be moved.
 * @param {DirectoryEntry} destinationEntry The entry of the destination
 *     directory.
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function()} successCallback On success.
 * @param {function(FileOperationManager.Error)} errorCallback On error.
 * @private
 */
FileOperationManager.MoveTask.processEntry_ = function(
    sourceEntry, destinationEntry, entryChangedCallback, successCallback,
    errorCallback) {
  fileOperationUtil.deduplicatePath(
      destinationEntry,
      sourceEntry.name,
      function(destinationName) {
        sourceEntry.moveTo(
            destinationEntry, destinationName,
            function(movedEntry) {
              entryChangedCallback(util.EntryChangedKind.CREATED, movedEntry);
              entryChangedCallback(util.EntryChangedKind.DELETED, sourceEntry);
              successCallback();
            },
            function(error) {
              errorCallback(new FileOperationManager.Error(
                  util.FileOperationErrorType.FILESYSTEM_ERROR, error));
            });
      },
      errorCallback);
};

/**
 * Task to create a zip archive.
 *
 * @param {Array.<Entry>} sourceEntries Array of source entries.
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @param {DirectoryEntry} zipBaseDirEntry Base directory dealt as a root
 *     in ZIP archive.
 * @constructor
 * @extends {FileOperationManager.Task}
 */
FileOperationManager.ZipTask = function(
    sourceEntries, targetDirEntry, zipBaseDirEntry) {
  FileOperationManager.Task.call(this, sourceEntries, targetDirEntry);
  this.zipBaseDirEntry = zipBaseDirEntry;
  this.zip = true;
};

/**
 * Extends FileOperationManager.Task.
 */
FileOperationManager.ZipTask.prototype.__proto__ =
    FileOperationManager.Task.prototype;

/**
 * Runs a zip file creation task.
 *
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function()} progressCallback Callback invoked periodically during
 *     the moving.
 * @param {function()} successCallback On complete.
 * @param {function(FileOperationManager.Error)} errorCallback On error.
 * @override
 */
FileOperationManager.ZipTask.prototype.run = function(
    entryChangedCallback, progressCallback, successCallback, errorCallback) {
  // TODO(hidehiko): we should localize the name.
  var destName = 'Archive';
  if (this.sourceEntries.length == 1) {
    var entryPath = this.sourceEntries[0].fullPath;
    var i = entryPath.lastIndexOf('/');
    var basename = (i < 0) ? entryPath : entryPath.substr(i + 1);
    i = basename.lastIndexOf('.');
    destName = ((i < 0) ? basename : basename.substr(0, i));
  }

  fileOperationUtil.deduplicatePath(
      this.targetDirEntry, destName + '.zip',
      function(destPath) {
        // TODO: per-entry zip progress update with accurate byte count.
        // For now just set completedBytes to same value as totalBytes so
        // that the progress bar is full.
        this.completedBytes = this.totalBytes;
        progressCallback();

        fileOperationUtil.zipSelection(
            this.entries,
            this.zipBaseDirEntry,
            destPath,
            function(entry) {
              entryChangedCallback(util.EntryChangedKind.CREATE, entry);
              successCallback();
            },
            function(error) {
              errorCallback(new FileOperationManager.Error(
                  util.FileOperationErrorType.FILESYSTEM_ERROR, error));
            });
      }.bind(this),
      errorCallback);
};

/**
 * Error class used to report problems with a copy operation.
 * If the code is UNEXPECTED_SOURCE_FILE, data should be a path of the file.
 * If the code is TARGET_EXISTS, data should be the existing Entry.
 * If the code is FILESYSTEM_ERROR, data should be the FileError.
 *
 * @param {util.FileOperationErrorType} code Error type.
 * @param {string|Entry|FileError} data Additional data.
 * @constructor
 */
FileOperationManager.Error = function(code, data) {
  this.code = code;
  this.data = data;
};

// FileOperationManager methods.

/**
 * Called before a new method is run in the manager. Prepares the manager's
 * state for running a new method.
 */
FileOperationManager.prototype.willRunNewMethod = function() {
  // Cancel any pending close actions so the file copy manager doesn't go away.
  if (this.unloadTimeout_)
    clearTimeout(this.unloadTimeout_);
  this.unloadTimeout_ = null;
};

/**
 * @return {Object} Status object.
 */
FileOperationManager.prototype.getStatus = function() {
  // TODO(hidehiko): Reorganize the structure when delete queue is merged
  // into copy task queue.
  var rv = {
    totalItems: 0,
    completedItems: 0,

    totalBytes: 0,
    completedBytes: 0,

    pendingCopies: 0,
    pendingMoves: 0,
    pendingZips: 0,

    // In case the number of the incompleted entry is exactly one.
    filename: '',
  };

  var pendingEntry = null;
  for (var i = 0; i < this.copyTasks_.length; i++) {
    var task = this.copyTasks_[i];
    rv.totalItems += task.entries.length;
    rv.completedItems += task.numCompletedEntries;

    rv.totalBytes += task.totalBytes;
    rv.completedBytes += task.completedBytes;

    var numPendingEntries = task.entries.length - task.numCompletedEntries;
    if (task.zip) {
      rv.pendingZips += numPendingEntries;
    } else if (task.move || task.deleteAfterCopy) {
      rv.pendingMoves += numPendingEntries;
    } else {
      rv.pendingCopies += numPendingEntries;
    }

    if (task.processingEntry)
      pendingEntry = task.processingEntry;
  }

  if (rv.totalItems - rv.completedItems == 1 && pendingEntry)
    rv.filename = pendingEntry.name;

  return rv;
};

/**
 * Adds an event listener for the tasks.
 * @param {string} type The name of the event.
 * @param {function(cr.Event)} handler The handler for the event.
 *     This is called when the event is dispatched.
 */
FileOperationManager.prototype.addEventListener = function(type, handler) {
  this.eventRouter_.addEventListener(type, handler);
};

/**
 * Removes an event listener for the tasks.
 * @param {string} type The name of the event.
 * @param {function(cr.Event)} handler The handler to be removed.
 */
FileOperationManager.prototype.removeEventListener = function(type, handler) {
  this.eventRouter_.removeEventListener(type, handler);
};

/**
 * Says if there are any tasks in the queue.
 * @return {boolean} True, if there are any tasks.
 */
FileOperationManager.prototype.hasQueuedTasks = function() {
  return this.copyTasks_.length > 0 || this.deleteTasks_.length > 0;
};

/**
 * Unloads the host page in 5 secs of idleing. Need to be called
 * each time this.copyTasks_.length or this.deleteTasks_.length
 * changed.
 *
 * @private
 */
FileOperationManager.prototype.maybeScheduleCloseBackgroundPage_ = function() {
  if (!this.hasQueuedTasks()) {
    if (this.unloadTimeout_ === null)
      this.unloadTimeout_ = setTimeout(maybeCloseBackgroundPage, 5000);
  } else if (this.unloadTimeout_) {
    clearTimeout(this.unloadTimeout_);
    this.unloadTimeout_ = null;
  }
};

/**
 * Completely clear out the copy queue, either because we encountered an error
 * or completed successfully.
 *
 * @private
 */
FileOperationManager.prototype.resetQueue_ = function() {
  for (var i = 0; i < this.cancelObservers_.length; i++)
    this.cancelObservers_[i]();

  this.copyTasks_ = [];
  this.cancelObservers_ = [];
  this.maybeScheduleCloseBackgroundPage_();
};

/**
 * Request that the current copy queue be abandoned.
 *
 * @param {function()=} opt_callback On cancel.
 */
FileOperationManager.prototype.requestCancel = function(opt_callback) {
  this.cancelRequested_ = true;
  if (this.cancelCallback_) {
    this.cancelCallback_();
    this.cancelCallback_ = null;
  }
  if (opt_callback)
    this.cancelObservers_.push(opt_callback);

  // If there is any active task it will eventually call maybeCancel_.
  // Otherwise call it right now.
  if (this.copyTasks_.length == 0)
    this.doCancel_();
  else
    this.copyTasks_[0].requestCancel();
};

/**
 * Perform the bookkeeping required to cancel.
 *
 * @private
 */
FileOperationManager.prototype.doCancel_ = function() {
  this.resetQueue_();
  this.cancelRequested_ = false;
  this.eventRouter_.sendProgressEvent('CANCELLED', this.getStatus());
};

/**
 * Used internally to check if a cancel has been requested, and handle
 * it if so.
 *
 * @return {boolean} If canceled.
 * @private
 */
FileOperationManager.prototype.maybeCancel_ = function() {
  if (!this.cancelRequested_)
    return false;

  this.doCancel_();
  return true;
};

/**
 * Kick off pasting.
 *
 * @param {Array.<string>} sourcePaths Path of the source files.
 * @param {string} targetPath The destination path of the target directory.
 * @param {boolean} isMove True if the operation is "move", otherwise (i.e.
 *     if the operation is "copy") false.
 */
FileOperationManager.prototype.paste = function(
    sourcePaths, targetPath, isMove) {
  // Do nothing if sourcePaths is empty.
  if (sourcePaths.length == 0)
    return;

  var errorCallback = function(error) {
    this.eventRouter_.sendProgressEvent(
        'ERROR',
        this.getStatus(),
        new FileOperationManager.Error(
            util.FileOperationErrorType.FILESYSTEM_ERROR, error));
  }.bind(this);

  var targetEntry = null;
  var entries = [];

  // Resolve paths to entries.
  var resolveGroup = new AsyncUtil.Group();
  resolveGroup.add(function(callback) {
    webkitResolveLocalFileSystemURL(
        util.makeFilesystemUrl(targetPath),
        function(entry) {
          if (!entry.isDirectory) {
            // Found a non directory entry.
            errorCallback(util.createFileError(FileError.TYPE_MISMATCH_ERR));
            return;
          }

          targetEntry = entry;
          callback();
        },
        errorCallback);
  });

  for (var i = 0; i < sourcePaths.length; i++) {
    resolveGroup.add(function(sourcePath, callback) {
      webkitResolveLocalFileSystemURL(
          util.makeFilesystemUrl(sourcePath),
          function(entry) {
            entries.push(entry);
            callback();
          },
          errorCallback);
    }.bind(this, sourcePaths[i]));
  }

  resolveGroup.run(function() {
    if (isMove) {
      // Moving to the same directory is a redundant operation.
      entries = entries.filter(function(entry) {
        return targetEntry.fullPath + '/' + entry.name != entry.fullPath;
      });

      // Do nothing, if we have no entries to be moved.
      if (entries.length == 0)
        return;
    }

    this.queueCopy_(targetEntry, entries, isMove);
  }.bind(this));
};

/**
 * Checks if the move operation is avaiable between the given two locations.
 *
 * @param {DirectoryEntry} sourceEntry An entry from the source.
 * @param {DirectoryEntry} targetDirEntry Directory entry for the target.
 * @return {boolean} Whether we can move from the source to the target.
 */
FileOperationManager.prototype.isMovable = function(sourceEntry,
                                               targetDirEntry) {
  return (PathUtil.isDriveBasedPath(sourceEntry.fullPath) &&
          PathUtil.isDriveBasedPath(targetDirEntry.fullPath)) ||
         (PathUtil.getRootPath(sourceEntry.fullPath) ==
          PathUtil.getRootPath(targetDirEntry.fullPath));
};

/**
 * Initiate a file copy.
 *
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @param {Array.<Entry>} entries Entries to copy.
 * @param {boolean} isMove In case of move.
 * @return {FileOperationManager.Task} Copy task.
 * @private
 */
FileOperationManager.prototype.queueCopy_ = function(
    targetDirEntry, entries, isMove) {
  // When copying files, null can be specified as source directory.
  var task;
  if (isMove) {
    if (this.isMovable(entries[0], targetDirEntry)) {
      task = new FileOperationManager.MoveTask(entries, targetDirEntry);
    } else {
      task = new FileOperationManager.CopyTask(entries, targetDirEntry);
      task.deleteAfterCopy = true;
    }
  } else {
    task = new FileOperationManager.CopyTask(entries, targetDirEntry);
  }

  task.initialize(function() {
    this.copyTasks_.push(task);
    this.maybeScheduleCloseBackgroundPage_();
    if (this.copyTasks_.length == 1) {
      // Assume this.cancelRequested_ == false.
      // This moved us from 0 to 1 active tasks, let the servicing begin!
      this.serviceAllTasks_();
    } else {
      // Force to update the progress of butter bar when there are new tasks
      // coming while servicing current task.
      this.eventRouter_.sendProgressEvent('PROGRESS', this.getStatus());
    }
  }.bind(this));

  return task;
};

/**
 * Service all pending tasks, as well as any that might appear during the
 * copy.
 *
 * @private
 */
FileOperationManager.prototype.serviceAllTasks_ = function() {
  var self = this;

  var onTaskProgress = function() {
    self.eventRouter_.sendProgressEvent('PROGRESS', self.getStatus());
  };

  var onEntryChanged = function(kind, entry) {
    self.eventRouter_.sendEntryChangedEvent(kind, entry);
  };

  var onTaskError = function(err) {
    if (self.maybeCancel_())
      return;
    self.eventRouter_.sendProgressEvent('ERROR', self.getStatus(), err);
    self.resetQueue_();
  };

  var onTaskSuccess = function() {
    if (self.maybeCancel_())
      return;

    // The task at the front of the queue is completed. Pop it from the queue.
    self.copyTasks_.shift();
    self.maybeScheduleCloseBackgroundPage_();

    if (!self.copyTasks_.length) {
      // All tasks have been serviced, clean up and exit.
      self.eventRouter_.sendProgressEvent('SUCCESS', self.getStatus());
      self.resetQueue_();
      return;
    }

    // We want to dispatch a PROGRESS event when there are more tasks to serve
    // right after one task finished in the queue. We treat all tasks as one
    // big task logically, so there is only one BEGIN/SUCCESS event pair for
    // these continuous tasks.
    self.eventRouter_.sendProgressEvent('PROGRESS', self.getStatus());
    self.copyTasks_[0].run(
        onEntryChanged, onTaskProgress, onTaskSuccess, onTaskError);
  };

  // If the queue size is 1 after pushing our task, it was empty before,
  // so we need to kick off queue processing and dispatch BEGIN event.
  this.eventRouter_.sendProgressEvent('BEGIN', this.getStatus());
  this.copyTasks_[0].run(
      onEntryChanged, onTaskProgress, onTaskSuccess, onTaskError);
};

/**
 * Timeout before files are really deleted (to allow undo).
 */
FileOperationManager.DELETE_TIMEOUT = 30 * 1000;

/**
 * Schedules the files deletion.
 *
 * @param {Array.<Entry>} entries The entries.
 */
FileOperationManager.prototype.deleteEntries = function(entries) {
  var task = { entries: entries };
  this.deleteTasks_.push(task);
  this.maybeScheduleCloseBackgroundPage_();
  if (this.deleteTasks_.length == 1)
    this.serviceAllDeleteTasks_();
};

/**
 * Service all pending delete tasks, as well as any that might appear during the
 * deletion.
 *
 * Must not be called if there is an in-flight delete task.
 *
 * @private
 */
FileOperationManager.prototype.serviceAllDeleteTasks_ = function() {
  // Returns the urls of the given task's entries.
  var getTaskUrls = function(task) {
    return task.entries.map(function(entry) {
      return util.makeFilesystemUrl(entry.fullPath);
    });
  };

  var onTaskSuccess = function() {
    var urls = getTaskUrls(this.deleteTasks_.shift());
    if (!this.deleteTasks_.length) {
      // All tasks have been serviced, clean up and exit.
      this.eventRouter_.sendDeleteEvent('SUCCESS', urls);
      this.maybeScheduleCloseBackgroundPage_();
      return;
    }

    // We want to dispatch a PROGRESS event when there are more tasks to serve
    // right after one task finished in the queue. We treat all tasks as one
    // big task logically, so there is only one BEGIN/SUCCESS event pair for
    // these continuous tasks.
    this.eventRouter_.sendDeleteEvent('PROGRESS', urls);

    this.serviceDeleteTask_(this.deleteTasks_[0], onTaskSuccess, onTaskFailure);
  }.bind(this);

  var onTaskFailure = function(error) {
    var urls = getTaskUrls(this.deleteTasks_[0]);
    this.deleteTasks_ = [];
    this.eventRouter_.sendDeleteEvent('ERROR', urls);
    this.maybeScheduleCloseBackgroundPage_();
  }.bind(this);

  // If the queue size is 1 after pushing our task, it was empty before,
  // so we need to kick off queue processing and dispatch BEGIN event.
  this.eventRouter_.sendDeleteEvent('BEGIN', getTaskUrls(this.deleteTasks_[0]));
  this.serviceDeleteTask_(this.deleteTasks_[0], onTaskSuccess, onTaskFailure);
};

/**
 * Performs the deletion.
 *
 * @param {Object} task The delete task (see deleteEntries function).
 * @param {function()} successCallback Callback run on success.
 * @param {function(FileOperationManager.Error)} errorCallback Callback run on
 *     error.
 * @private
 */
FileOperationManager.prototype.serviceDeleteTask_ = function(
    task, successCallback, errorCallback) {
  var downcount = task.entries.length;
  if (downcount == 0) {
    successCallback();
    return;
  }

  var filesystemError = null;
  var onComplete = function() {
    if (--downcount > 0)
      return;

    // All remove operations are processed. Run callback.
    if (filesystemError) {
      errorCallback(new FileOperationManager.Error(
          util.FileOperationErrorType.FILESYSTEM_ERROR, filesystemError));
    } else {
      successCallback();
    }
  };

  for (var i = 0; i < task.entries.length; i++) {
    var entry = task.entries[i];
    util.removeFileOrDirectory(
        entry,
        function(currentEntry) {
          this.eventRouter_.sendEntryChangedEvent(
              util.EntryChangedKind.DELETED, currentEntry);
          onComplete();
        }.bind(this, entry),
        function(error) {
          if (!filesystemError)
            filesystemError = error;
          onComplete();
        });
  }
};

/**
 * Creates a zip file for the selection of files.
 *
 * @param {Entry} dirEntry The directory containing the selection.
 * @param {Array.<Entry>} selectionEntries The selected entries.
 */
FileOperationManager.prototype.zipSelection = function(
    dirEntry, selectionEntries) {
  var self = this;
  var zipTask = new FileOperationManager.ZipTask(
      selectionEntries, dirEntry, dirEntry);
  zipTask.zip = true;
  zipTask.initialize(function() {
    self.copyTasks_.push(zipTask);
    if (self.copyTasks_.length == 1) {
      // Assume self.cancelRequested_ == false.
      // This moved us from 0 to 1 active tasks, let the servicing begin!
      self.serviceAllTasks_();
    } else {
      // Force to update the progress of butter bar when there are new tasks
      // coming while servicing current task.
      self.eventRouter_.sendProgressEvent('PROGRESS', self.getStatus());
    }
  });
};
