// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_UTIL_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_UTIL_H_
#pragma once

#include <string>
#include <vector>

#include "base/file_path.h"
#include "chrome/browser/chromeos/gdata/gdata_operation_registry.h"
#include "chrome/browser/ui/select_file_dialog.h"
#include "googleurl/src/gurl.h"

class Profile;

namespace base {
class ListValue;
}

extern const char kFileBrowserDomain[];

// File manager helper methods.
namespace file_manager_util {

// Gets base file browser url.
GURL GetFileBrowserExtensionUrl();
GURL GetFileBrowserUrl();
GURL GetMediaPlayerUrl();
GURL GetMediaPlayerPlaylistUrl();

// Converts |full_file_path| into external filesystem: url. Returns false
// if |full_file_path| is not managed by the external filesystem provider.
bool ConvertFileToFileSystemUrl(Profile* profile,
    const FilePath& full_file_path, const GURL& origin_url, GURL* url);

// Converts |full_file_path| into |relative_path| within the external provider
// in File API. Returns false if |full_file_path| is not managed by the
// external filesystem provider.
bool ConvertFileToRelativeFileSystemPath(Profile* profile,
    const FilePath& full_file_path, FilePath* relative_path);

// Gets base file browser url for.
GURL GetFileBrowserUrlWithParams(
    SelectFileDialog::Type type,
    const string16& title,
    const FilePath& default_virtual_path,
    const SelectFileDialog::FileTypeInfo* file_types,
    int file_type_index,
    const FilePath::StringType& default_extension);

// Get file dialog title string from its type.
string16 GetTitleFromType(SelectFileDialog::Type type);

// Opens file browser UI on its own tab on drive location defined with
// |dir|. Automatically closes this tab on |dir| unmount.
void ViewRemovableDrive(const FilePath& dir);

// Opens file browser UI in its own tab on file system location defined with
// |dir|.
void ViewFolder(const FilePath& dir);

// Opens file in the browser.
// TODO(kaznacheev): remove the obsolete enqueue parameter.
void ViewFile(const FilePath& full_path, bool enqueue);

// Tries to open |file| directly in the browser. Returns false if the browser
// can't directly handle this type of file.
bool TryViewingFile(const FilePath& file);

void InstallCRX(Profile* profile, const FilePath& full_path);

bool ShouldBeOpenedWithPdfPlugin(const char* file_extension);

// Converts the vector of progress status to their JSON (Value) form.
base::ListValue* ProgressStatusVectorToListValue(
    Profile* profile, const GURL& origin_url,
    const std::vector<gdata::GDataOperationRegistry::ProgressStatus>& list);

}  // namespace file_manager_util

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_UTIL_H_
