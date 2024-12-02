// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser.test.util;

import android.graphics.Rect;
import android.test.ActivityInstrumentationTestCase2;
import android.test.InstrumentationTestCase;
import android.util.JsonReader;

import java.io.IOException;
import java.io.StringReader;

import junit.framework.Assert;

import org.chromium.content.browser.ContentView;

/**
 * Collection of DOM-based utilities.
 */
public class DOMUtils {

    /**
     * Returns the rect boundaries for a node by its id.
     */
    public static Rect getNodeBounds(InstrumentationTestCase test, final ContentView view,
            TestCallbackHelperContainer viewClient, String nodeId) throws Throwable {
        StringBuilder sb = new StringBuilder();
        sb.append("(function() {");
        sb.append("  var node = document.getElementById('" + nodeId + "');");
        sb.append("  if (!node) return null;");
        sb.append("  var width = node.offsetWidth;");
        sb.append("  var height = node.offsetHeight;");
        sb.append("  var x = -window.scrollX;");
        sb.append("  var y = -window.scrollY;");
        sb.append("  do {");
        sb.append("    x += node.offsetLeft;");
        sb.append("    y += node.offsetTop;");
        sb.append("  } while (node = node.offsetParent);");
        sb.append("  return [ x, y, width, height ];");
        sb.append("})();");

        String jsonText = JavaScriptUtils.executeJavaScriptAndWaitForResult(test, view, viewClient,
                sb.toString());

        Assert.assertFalse("Failed to retrieve bounds for " + nodeId,
                jsonText.trim().equalsIgnoreCase("null"));

        JsonReader jsonReader = new JsonReader(new StringReader(jsonText));
        int[] bounds = new int[4];
        try {
            jsonReader.beginArray();
            int i = 0;
            while (jsonReader.hasNext()) {
                bounds[i++] = jsonReader.nextInt();
            }
            jsonReader.endArray();
            Assert.assertEquals("Invalid bounds returned.", 4, i);
        } catch (IOException exception) {
            Assert.fail("Failed to evaluate JavaScript: " + jsonText + "\n" + exception);
        }
        return new Rect(bounds[0], bounds[1], bounds[0] + bounds[2], bounds[1] + bounds[3]);
    }

    /**
     * Click a DOM node by its id.
     */
    public static void clickNode(ActivityInstrumentationTestCase2 activityTestCase,
            final ContentView view, TestCallbackHelperContainer viewClient, String nodeId)
            throws Throwable {
        Rect bounds = getNodeBounds(activityTestCase, view, viewClient, nodeId);
        Assert.assertNotNull("Failed to get DOM element bounds of '" + nodeId + "'.'", bounds);

        // TODO(leandrogracia): make this use view.getScale() once the correct value is available.
        // WARNING: this will only work with a viewport fixed scale value of 1.0.
        float scale = getDevicePixelRatio(activityTestCase, view, viewClient);
        int clickX = (int)(bounds.exactCenterX() * scale + 0.5);
        int clickY = (int)(bounds.exactCenterY() * scale + 0.5);

        TouchCommon touchCommon = new TouchCommon(activityTestCase);
        touchCommon.singleClickView(view, clickX, clickY);
    }

    /**
     * Long-press a DOM node by its id.
     */
    public static void longPressNode(ActivityInstrumentationTestCase2 activityTestCase,
            final ContentView view, TestCallbackHelperContainer viewClient, String nodeId)
            throws Throwable {
        Rect bounds = getNodeBounds(activityTestCase, view, viewClient, nodeId);
        Assert.assertNotNull("Failed to get DOM element bounds of '" + nodeId + "'.'", bounds);

        // TODO(leandrogracia): make this use view.getScale() once the correct value is available.
        // WARNING: this will only work with a viewport fixed scale value of 1.0.
        float scale = getDevicePixelRatio(activityTestCase, view, viewClient);
        int clickX = (int)(bounds.exactCenterX() * scale + 0.5);
        int clickY = (int)(bounds.exactCenterY() * scale + 0.5);

        TouchCommon touchCommon = new TouchCommon(activityTestCase);
        touchCommon.longPressView(view, clickX, clickY);
    }

    /**
     * Scrolls the view to ensure that the required DOM node is visible.
     */
    public static void scrollNodeIntoView(InstrumentationTestCase test, final ContentView view,
            TestCallbackHelperContainer viewClient, String nodeId) throws Throwable {
        JavaScriptUtils.executeJavaScriptAndWaitForResult(test, view, viewClient,
                "document.getElementById('" + nodeId + "').scrollIntoView()");
    }

    // This is a temporary workaround to make clickNode and longPressNode work under fixed viewport
    // scale settings of 1.0 until the new compositor correctly provides the ContentView page scale.
    private static float getDevicePixelRatio(InstrumentationTestCase test, final ContentView view,
            TestCallbackHelperContainer viewClient) throws Throwable {
        return Float.valueOf(JavaScriptUtils.executeJavaScriptAndWaitForResult(test, view,
                viewClient, "window.devicePixelRatio"));
    }
}
