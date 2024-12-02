// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_INDEXED_DB_PROXY_WEBIDBDATABASE_IMPL_H_
#define CONTENT_COMMON_INDEXED_DB_PROXY_WEBIDBDATABASE_IMPL_H_

#include "base/basictypes.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebIDBDatabase.h"

namespace WebKit {
class WebIDBCallbacks;
class WebIDBDatabaseCallbacks;
class WebString;
class WebIDBTransaction;
}

namespace content {

class RendererWebIDBDatabaseImpl : public WebKit::WebIDBDatabase {
 public:
  explicit RendererWebIDBDatabaseImpl(int32 idb_database_id);
  virtual ~RendererWebIDBDatabaseImpl();

  // TODO(alecflett): Remove this when it is removed from webkit:
  // https://bugs.webkit.org/show_bug.cgi?id=98085
  static const long long AutogenerateObjectStoreId = -1;

  // WebKit::WebIDBDatabase
  virtual WebKit::WebIDBMetadata metadata() const;
  virtual WebKit::WebIDBObjectStore* createObjectStore(
      long long objectstore_id,
      const WebKit::WebString& name,
      const WebKit::WebIDBKeyPath& key_path,
      bool auto_increment,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode& ec);
  virtual void deleteObjectStore(
      const WebKit::WebString& name,
      const WebKit::WebIDBTransaction& transaction,
      WebKit::WebExceptionCode& ec);
  virtual void setVersion(
      const WebKit::WebString& version, WebKit::WebIDBCallbacks* callbacks,
      WebKit::WebExceptionCode& ec);
  virtual WebKit::WebIDBTransaction* transaction(
      const WebKit::WebDOMStringList& names,
      unsigned short mode,
      WebKit::WebExceptionCode& ec);
  virtual void close();

 private:
  int32 idb_database_id_;
};

}  // namespace content

#endif  // CONTENT_COMMON_INDEXED_DB_PROXY_WEBIDBDATABASE_IMPL_H_
