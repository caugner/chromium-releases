// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_WALLPAPER_API_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_WALLPAPER_API_H_

#include "ash/desktop_background/desktop_background_controller.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/chromeos/extensions/wallpaper_function_base.h"

// Implementation of chrome.wallpaper.setWallpaper API.
// After this API being called, a jpeg encoded wallpaper will be saved to
// /home/chronos/custom_wallpaper/{resolution}/{username}/file_name. The
// wallpaper can then persistent after Chrome restart. New call to this API
// will replace the previous saved wallpaper with new one.
// Note: For security reason, the original encoded wallpaper image is not saved
// directly. It is decoded and re-encoded to jpeg format before saved to file
// system.
class WallpaperSetWallpaperFunction : public WallpaperFunctionBase {
 public:
  DECLARE_EXTENSION_FUNCTION("wallpaper.setWallpaper",
                             WALLPAPER_SETWALLPAPER)

  WallpaperSetWallpaperFunction();

 protected:
  virtual ~WallpaperSetWallpaperFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  virtual void OnWallpaperDecoded(const gfx::ImageSkia& wallpaper) OVERRIDE;

  // Generates thumbnail of custom wallpaper. A simple STRETCH is used for
  // generating thumbnail.
  void GenerateThumbnail(const base::FilePath& thumbnail_path,
                         scoped_ptr<gfx::ImageSkia> image);

  // Thumbnail is ready. Calls api function javascript callback.
  void ThumbnailGenerated(base::RefCountedBytes* data);

  // Layout of the downloaded wallpaper.
  ash::WallpaperLayout layout_;

  // True if need to generate thumbnail and pass to callback.
  bool generate_thumbnail_;

  // Unique file name of the custom wallpaper.
  std::string file_name_;

  // Email address of logged in user.
  // TODO(bshe): User's email should not be used as part of wallpaper file path.
  // http://crbug.com/287020
  std::string email_;

  // String representation of downloaded wallpaper.
  std::string image_data_;

  // Sequence token associated with wallpaper operations. Shared with
  // WallpaperManager.
  base::SequencedWorkerPool::SequenceToken sequence_token_;
};

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_WALLPAPER_API_H_

