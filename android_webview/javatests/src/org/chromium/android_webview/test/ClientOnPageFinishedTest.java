// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test;

import android.test.FlakyTest;
import android.test.suitebuilder.annotation.MediumTest;

import org.chromium.android_webview.AwContents;
import org.chromium.android_webview.test.util.TestWebServer;
import org.chromium.base.test.util.Feature;
import org.chromium.content.browser.test.util.TestCallbackHelperContainer;

/**
 * Tests for the ContentViewClient.onPageFinished() method.
 */
public class ClientOnPageFinishedTest extends AndroidWebViewTestBase {

    private TestAwContentsClient mContentsClient;
    private AwContents mAwContents;

    @Override
    public void setUp() throws Exception {
        super.setUp();
        mContentsClient = new TestAwContentsClient();
        final AwTestContainerView testContainerView =
                createAwTestContainerViewOnMainSync(mContentsClient);
        mAwContents = testContainerView.getAwContents();
    }

    @MediumTest
    @Feature({"Android-WebView"})
    public void testOnPageFinishedPassesCorrectUrl() throws Throwable {
        TestCallbackHelperContainer.OnPageFinishedHelper onPageFinishedHelper =
                mContentsClient.getOnPageFinishedHelper();

        String html = "<html><body>Simple page.</body></html>";
        int currentCallCount = onPageFinishedHelper.getCallCount();
        loadDataAsync(mAwContents, html, "text/html", false);

        onPageFinishedHelper.waitForCallback(currentCallCount);
        assertEquals("data:text/html," + html, onPageFinishedHelper.getUrl());
    }

    //@MediumTest
    //@Feature({"Android-WebView"})
    // See crbug.com/148917
    @FlakyTest
    public void testOnPageFinishedCalledAfterError() throws Throwable {
        TestCallbackHelperContainer.OnReceivedErrorHelper onReceivedErrorHelper =
                mContentsClient.getOnReceivedErrorHelper();
        TestCallbackHelperContainer.OnPageFinishedHelper onPageFinishedHelper =
                mContentsClient.getOnPageFinishedHelper();

        assertEquals(0, onReceivedErrorHelper.getCallCount());

        String url = "http://man.id.be.really.surprised.if.this.address.existed.blah/";
        int onReceivedErrorCallCount = onReceivedErrorHelper.getCallCount();
        int onPageFinishedCallCount = onPageFinishedHelper.getCallCount();
        loadUrlAsync(mAwContents, url);

        onReceivedErrorHelper.waitForCallback(onReceivedErrorCallCount);
        onPageFinishedHelper.waitForCallback(onPageFinishedCallCount);
        assertEquals(1, onReceivedErrorHelper.getCallCount());
    }

    @MediumTest
    @Feature({"Android-WebView"})
    public void testOnPageFinishedNotCalledForValidSubresources() throws Throwable {
        TestCallbackHelperContainer.OnPageFinishedHelper onPageFinishedHelper =
                mContentsClient.getOnPageFinishedHelper();

        TestWebServer webServer = null;
        try {
            webServer = new TestWebServer(false);

            final String testHtml = "<html><head>Header</head><body>Body</body></html>";
            final String testPath = "/test.html";
            final String syncPath = "/sync.html";

            final String testUrl = webServer.setResponse(testPath, testHtml, null);
            final String syncUrl = webServer.setResponse(syncPath, testHtml, null);

            assertEquals(0, onPageFinishedHelper.getCallCount());
            final int pageWithSubresourcesCallCount = onPageFinishedHelper.getCallCount();
            loadDataAsync(mAwContents,
                          "<html><iframe src=\"" + testPath + "\" /></html>",
                          "text/html",
                          false);

            onPageFinishedHelper.waitForCallback(pageWithSubresourcesCallCount);

            // Rather than wait a fixed time to see that an onPageFinished callback isn't issued
            // we load another valid page. Since callbacks arrive sequentially if the next callback
            // we get is for the synchronizationUrl we know that the previous load did not schedule
            // a callback for the iframe.
            final int synchronizationPageCallCount = onPageFinishedHelper.getCallCount();
            loadUrlAsync(mAwContents, syncUrl);

            onPageFinishedHelper.waitForCallback(synchronizationPageCallCount);
            assertEquals(syncUrl, onPageFinishedHelper.getUrl());
            assertEquals(2, onPageFinishedHelper.getCallCount());

        } finally {
            if (webServer != null) webServer.shutdown();
        }
    }
}
