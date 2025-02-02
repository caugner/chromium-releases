// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** Content provider for testing content URLs. */
package org.chromium.android_webview.test;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.database.AbstractCursor;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.FileOutputStream;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

// Note: if you move this class, make sure you have also updated AndroidManifest.xml
public class TestContentProvider extends ContentProvider {
    private static final String AUTHORITY = "org.chromium.android_webview.test.TestContentProvider";
    private static final String CONTENT_SCHEME = "content://";
    private static final String CONTENT_IMAGE_TYPE = "image/png";
    private static final String CONTENT_IMAGE_TARGET = "image";
    private static final String GET_RESOURCE_REQUEST_COUNT = "get_resource_request_count";
    private static final String RESET_RESOURCE_REQUEST_COUNT = "reset_resource_request_count";
    private static final String TAG = "TestContentProvider";
    private static final int EXPECTED_COLUMN_INDEX = 0;
    private static final Map<String, String> REGISTERED_CONTENT_TYPE =
            new HashMap<String, String>();
    private static final Map<String, byte[]> REGISTERED_RESPONSE = new HashMap<String, byte[]>();
    private final Map<String, Integer> mResourceRequestCount;

    // 1x1 black dot png image.
    private static final byte[] IMAGE = {
        (byte) 0x89,
        0x50,
        0x4e,
        0x47,
        0x0d,
        0x0a,
        0x1a,
        0x0a,
        0x00,
        0x00,
        0x00,
        0x0d,
        0x49,
        0x48,
        0x44,
        0x52,
        0x00,
        0x00,
        0x00,
        0x01,
        0x00,
        0x00,
        0x00,
        0x01,
        0x08,
        0x00,
        0x00,
        0x00,
        0x00,
        0x3a,
        0x7e,
        (byte) 0x9b,
        0x55,
        0x00,
        0x00,
        0x00,
        0x01,
        0x73,
        0x52,
        0x47,
        0x42,
        0x00,
        (byte) 0xae,
        (byte) 0xce,
        0x1c,
        (byte) 0xe9,
        0x00,
        0x00,
        0x00,
        0x0d,
        0x49,
        0x44,
        0x41,
        0x54,
        0x08,
        0x1d,
        0x01,
        0x02,
        0x00,
        (byte) 0xfd,
        (byte) 0xff,
        0x00,
        0x00,
        0x00,
        0x02,
        0x00,
        0x01,
        (byte) 0xcd,
        (byte) 0xe3,
        (byte) 0xd1,
        0x2b,
        0x00,
        0x00,
        0x00,
        0x00,
        0x49,
        0x45,
        0x4e,
        0x44,
        (byte) 0xae,
        0x42,
        0x60,
        (byte) 0x82
    };

    public static String createContentUrl(String target) {
        return CONTENT_SCHEME + AUTHORITY + "/" + target;
    }

    private static Uri createRequestUri(final String target, String resource) {
        return Uri.parse(createContentUrl(target) + "?" + resource);
    }

    public static int getResourceRequestCount(Context context, String resource) {
        Uri uri = createRequestUri(GET_RESOURCE_REQUEST_COUNT, resource);
        final Cursor cursor = context.getContentResolver().query(uri, null, null, null, null);
        try {
            cursor.moveToFirst();
            return cursor.getInt(EXPECTED_COLUMN_INDEX);
        } finally {
            cursor.close();
        }
    }

    public static void resetResourceRequestCount(Context context, String resource) {
        Uri uri = createRequestUri(RESET_RESOURCE_REQUEST_COUNT, resource);
        // A null cursor is returned for this request.
        context.getContentResolver().query(uri, null, null, null, null);
    }

    public static void register(String target, String contentType, byte[] response) {
        REGISTERED_CONTENT_TYPE.put(target, contentType);
        REGISTERED_RESPONSE.put(target, response);
    }

    public TestContentProvider() {
        super();
        mResourceRequestCount = new HashMap<String, Integer>();
        register(CONTENT_IMAGE_TARGET, CONTENT_IMAGE_TYPE, IMAGE);
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public AssetFileDescriptor openAssetFile(Uri uri, String mode) {
        String target = uri.getLastPathSegment();
        if (mResourceRequestCount.containsKey(target)) {
            mResourceRequestCount.put(target, mResourceRequestCount.get(target) + 1);
        } else {
            mResourceRequestCount.put(target, 1);
        }
        if (REGISTERED_RESPONSE.containsKey(target)) return createResponse(target);
        // Default to return the registered image content.
        return createResponse(CONTENT_IMAGE_TARGET);
    }

    @Override
    public String getType(Uri uri) {
        String target = uri.getLastPathSegment();
        if (REGISTERED_CONTENT_TYPE.containsKey(target)) return REGISTERED_CONTENT_TYPE.get(target);
        // Default to return the type for the registered image content.
        return CONTENT_IMAGE_TYPE;
    }

    @Override
    public int update(Uri uri, ContentValues values, String where, String[] whereArgs) {
        return 0;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    /** Cursor object for retrieving resource request counters. */
    private static class ProviderStateCursor extends AbstractCursor {
        private final int mResourceRequestCount;

        public ProviderStateCursor(int resourceRequestCount) {
            mResourceRequestCount = resourceRequestCount;
        }

        @Override
        public boolean isNull(int columnIndex) {
            return columnIndex != EXPECTED_COLUMN_INDEX;
        }

        @Override
        public int getCount() {
            return 1;
        }

        @Override
        public int getType(int columnIndex) {
            return columnIndex == EXPECTED_COLUMN_INDEX
                    ? Cursor.FIELD_TYPE_INTEGER
                    : Cursor.FIELD_TYPE_NULL;
        }

        private void unsupported() {
            throw new UnsupportedOperationException();
        }

        @Override
        public double getDouble(int columnIndex) {
            unsupported();
            return 0.0;
        }

        @Override
        public float getFloat(int columnIndex) {
            unsupported();
            return 0.0f;
        }

        @Override
        public int getInt(int columnIndex) {
            return columnIndex == EXPECTED_COLUMN_INDEX ? mResourceRequestCount : -1;
        }

        @Override
        public short getShort(int columnIndex) {
            unsupported();
            return 0;
        }

        @Override
        public long getLong(int columnIndex) {
            return getInt(columnIndex);
        }

        @Override
        public String getString(int columnIndex) {
            unsupported();
            return null;
        }

        @Override
        public String[] getColumnNames() {
            return new String[] {GET_RESOURCE_REQUEST_COUNT};
        }
    }

    @Override
    public Cursor query(
            Uri uri,
            String[] projection,
            String selection,
            String[] selectionArgs,
            String sortOrder) {
        String action = uri.getLastPathSegment();
        String resource = uri.getQuery();
        if (GET_RESOURCE_REQUEST_COUNT.equals(action)) {
            return new ProviderStateCursor(
                    mResourceRequestCount.containsKey(resource)
                            ? mResourceRequestCount.get(resource)
                            : 0);
        } else if (RESET_RESOURCE_REQUEST_COUNT.equals(action)) {
            mResourceRequestCount.put(resource, 0);
        }
        return null;
    }

    private static AssetFileDescriptor createResponse(String target) {
        ParcelFileDescriptor[] pfds = null;
        FileOutputStream fileOut = null;
        try {
            try {
                pfds = ParcelFileDescriptor.createPipe();
                fileOut = new FileOutputStream(pfds[1].getFileDescriptor());
                fileOut.write(REGISTERED_RESPONSE.get(target));
                fileOut.flush();
                return new AssetFileDescriptor(pfds[0], 0, -1);
            } finally {
                if (fileOut != null) fileOut.close();
                if (pfds != null && pfds[1] != null) pfds[1].close();
            }
        } catch (IOException e) {
            Log.e(TAG, e.getMessage(), e);
        }
        return null;
    }
}
