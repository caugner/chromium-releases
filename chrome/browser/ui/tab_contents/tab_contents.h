// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TAB_CONTENTS_TAB_CONTENTS_H_
#define CHROME_BROWSER_UI_TAB_CONTENTS_TAB_CONTENTS_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/web_contents_observer.h"

class Browser;
class BrowserCommandsTabContentsCreator;
class BrowserLauncherItemControllerContentsCreator;
class BrowserTabstripTabContentsCreator;
class ChromeWebContentsHandler;
class ConstrainedWebDialogDelegateBase;
class ExtensionTabUtil;
class ExternalTabContainerWin;
class InstantLoader;
class OffscreenTabContentsCreator;
class PanelHost;
class Profile;
class TabStripModel;
class TabStripModelContentsCreator;

namespace chromeos {
class SimpleWebViewDialog;
class WebUILoginView;
}

namespace extensions {
class WebAuthFlow;
}

namespace prerender {
class PrerenderContents;
}

// Wraps WebContents and all of its supporting objects in order to control
// their ownership and lifetime.
//
// WARNING: Not every place where HTML can run has a TabContents. This class is
// *only* used in a visible, actual, tab inside a browser. Examples of things
// that do not have a TabContents include:
// - Extension background pages and popup bubbles
// - HTML notification bubbles
// - Screensavers on Chrome OS
// - Other random places we decide to display HTML over time
//
// Consider carefully whether your feature is something that makes sense only
// when a tab is displayed, or could make sense in other cases we use HTML. It
// may makes sense to push down into WebContents and make configurable, or at
// least to make easy for other WebContents hosts to include and support.
class TabContents : public content::WebContentsObserver {
 public:
  class Factory {
   private:
    // TabContents is going away <http://crbug.com/107201> so don't allow any
    // more code to construct instances. Explicitly befriend those who currently
    // do so.

    friend class Browser;
    friend class BrowserCommandsTabContentsCreator;
    friend class BrowserLauncherItemControllerContentsCreator;
    friend class BrowserTabstripTabContentsCreator;
    friend class chromeos::SimpleWebViewDialog;
    friend class chromeos::WebUILoginView;
    friend class ChromeWebContentsHandler;
    friend class ConstrainedWebDialogDelegateBase;
    friend class extensions::WebAuthFlow;
    friend class ExtensionTabUtil;
    friend class ExternalTabContainerWin;
    friend class InstantLoader;
    friend class OffscreenTabContentsCreator;
    friend class PanelHost;
    friend class prerender::PrerenderContents;
    // See crbug.com/153587
    friend class TabAndroid;
    friend class TabStripModel;
    friend class TabStripModelContentsCreator;
    FRIEND_TEST_ALL_PREFIXES(SessionRestoreTest, SessionStorageAfterTabReplace);

    static TabContents* CreateTabContents(content::WebContents* contents);
    static TabContents* CloneTabContents(TabContents* contents);
  };

  virtual ~TabContents();

  // Helper to retrieve the existing instance that owns a given WebContents.
  // Returns NULL if there is no such existing instance.
  // NOTE: This is not intended for general use. It is intended for situations
  // like callbacks from content/ where only a WebContents is available. In the
  // general case, please do NOT use this; plumb TabContents through the chrome/
  // code instead of WebContents.
  static TabContents* FromWebContents(content::WebContents* contents);
  static const TabContents* FromWebContents(
      const content::WebContents* contents);

  // Returns the WebContents that this owns.
  content::WebContents* web_contents() const;

  // Returns the Profile that is associated with this TabContents.
  Profile* profile() const;

  // True if this TabContents is being torn down.
  bool in_destructor() const { return in_destructor_; }

  // Overrides -----------------------------------------------------------------

  // content::WebContentsObserver overrides:
  virtual void WebContentsDestroyed(content::WebContents* tab) OVERRIDE;

 private:
  friend class TabContentsFactory;

  // Takes ownership of |contents|, which must be heap-allocated (as it lives
  // in a scoped_ptr) and can not be NULL.
  explicit TabContents(content::WebContents* contents);

  // Create a TabContents with the same state as this one. The returned
  // heap-allocated pointer is owned by the caller.
  TabContents* Clone();

  // WebContents (MUST BE LAST) ------------------------------------------------

  // If true, we're running the destructor.
  bool in_destructor_;

  // The supporting objects need to outlive the WebContents dtor (as they may
  // be called upon during its execution). As a result, this must come last
  // in the list.
  scoped_ptr<content::WebContents> web_contents_;

  DISALLOW_COPY_AND_ASSIGN(TabContents);
};

#endif  // CHROME_BROWSER_UI_TAB_CONTENTS_TAB_CONTENTS_H_
