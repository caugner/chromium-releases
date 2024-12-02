// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RateLimiter_h
#define RateLimiter_h

#include "base/memory/ref_counted.h"

namespace WebKit {
class WebGraphicsContext3D;
}

namespace cc {

class RateLimiterClient {
public:
    virtual void rateLimit() = 0;
};

// A RateLimiter can be used to make sure that a single context does not dominate all execution time.
// To use, construct a RateLimiter class around the context and call start() whenever calls are made on the
// context outside of normal flow control. RateLimiter will block if the context is too far ahead of the
// compositor.
class RateLimiter : public base::RefCounted<RateLimiter> {
public:
    static scoped_refptr<RateLimiter> create(WebKit::WebGraphicsContext3D*, RateLimiterClient*);

    void start();

    // Context and client will not be accessed after stop().
    void stop();

private:
    RateLimiter(WebKit::WebGraphicsContext3D*, RateLimiterClient*);
    ~RateLimiter();
    friend class base::RefCounted<RateLimiter>;

    class Task;
    friend class Task;
    void rateLimitContext();

    WebKit::WebGraphicsContext3D* m_context;
    bool m_active;
    RateLimiterClient *m_client;
};

}
#endif
