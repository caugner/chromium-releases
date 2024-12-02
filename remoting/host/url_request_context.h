// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_URL_REQUEST_CONTEXT_H_
#define REMOTING_HOST_URL_REQUEST_CONTEXT_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "net/proxy/proxy_config_service.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_context_storage.h"

namespace base {
class MessageLoopProxy;
}  // namespace base

namespace remoting {

// Subclass of net::URLRequestContext which can be used to store extra
// information for requests. This subclass is meant to be used in the
// remoting Me2Me host process where the profile is not available.
class URLRequestContext : public net::URLRequestContext {
 public:
  explicit URLRequestContext(net::ProxyConfigService* net_proxy_config_service);

 private:
  net::URLRequestContextStorage storage_;
};

class URLRequestContextGetter : public net::URLRequestContextGetter {
 public:
  URLRequestContextGetter(MessageLoop* io_message_loop,
                          MessageLoop* file_message_loop);
  virtual ~URLRequestContextGetter();

  // Overridden from net::URLRequestContextGetter:
  virtual net::URLRequestContext* GetURLRequestContext() OVERRIDE;
  virtual scoped_refptr<base::MessageLoopProxy>
      GetIOMessageLoopProxy() const OVERRIDE;

 private:
  scoped_refptr<net::URLRequestContext> url_request_context_;
  scoped_refptr<base::MessageLoopProxy> io_message_loop_proxy_;
  scoped_ptr<net::ProxyConfigService> proxy_config_service_;
};

}  // namespace remoting

#endif  // CHROME_SERVICE_NET_SERVICE_URL_REQUEST_CONTEXT_H_
