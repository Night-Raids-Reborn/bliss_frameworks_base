/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.server.biometrics.sensors.fingerprint;

import android.content.Context;
import android.hardware.fingerprint.Fingerprint;
import android.util.AtomicFile;
import android.util.Slog;
import android.util.TypedXmlPullParser;
import android.util.TypedXmlSerializer;
import android.util.Xml;

import com.android.internal.annotations.GuardedBy;
import com.android.server.biometrics.sensors.BiometricUserState;

import libcore.io.IoUtils;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;

/**
 * Class managing the set of fingerprint per user across device reboots.
 * @hide
 */
public class FingerprintUserState extends BiometricUserState<Fingerprint> {

    private static final String TAG = "FingerprintState";

    private static final String TAG_FINGERPRINTS = "fingerprints";
    private static final String TAG_FINGERPRINT = "fingerprint";
    private static final String ATTR_NAME = "name";
    private static final String ATTR_GROUP_ID = "groupId";
    private static final String ATTR_FINGER_ID = "fingerId";
    private static final String ATTR_DEVICE_ID = "deviceId";

    public FingerprintUserState(Context context, int userId, String fileName) {
        super(context, userId, fileName);
    }

    @Override
    protected String getBiometricsTag() {
        return TAG_FINGERPRINTS;
    }

    @Override
    protected int getNameTemplateResource() {
        return com.android.internal.R.string.fingerprint_name_template;
    }

    @Override
    protected ArrayList<Fingerprint> getCopy(ArrayList<Fingerprint> array) {
        final ArrayList<Fingerprint> result = new ArrayList<>();
        for (Fingerprint fp : array) {
            result.add(new Fingerprint(fp.getName(), fp.getGroupId(), fp.getBiometricId(),
                    fp.getDeviceId()));
        }
        return result;
    }

    @Override
    protected void doWriteState() {
        AtomicFile destination = new AtomicFile(mFile);

        ArrayList<Fingerprint> fingerprints;

        synchronized (this) {
            fingerprints = getCopy(mBiometrics);
        }

        FileOutputStream out = null;
        try {
            out = destination.startWrite();

            TypedXmlSerializer serializer = Xml.resolveSerializer(out);
            serializer.setFeature("http://xmlpull.org/v1/doc/features.html#indent-output", true);
            serializer.startDocument(null, true);
            serializer.startTag(null, TAG_FINGERPRINTS);

            final int count = fingerprints.size();
            for (int i = 0; i < count; i++) {
                Fingerprint fp = fingerprints.get(i);
                serializer.startTag(null, TAG_FINGERPRINT);
                serializer.attributeInt(null, ATTR_FINGER_ID, fp.getBiometricId());
                serializer.attribute(null, ATTR_NAME, fp.getName().toString());
                serializer.attributeInt(null, ATTR_GROUP_ID, fp.getGroupId());
                serializer.attributeLong(null, ATTR_DEVICE_ID, fp.getDeviceId());
                serializer.endTag(null, TAG_FINGERPRINT);
            }

            serializer.endTag(null, TAG_FINGERPRINTS);
            serializer.endDocument();
            destination.finishWrite(out);

            // Any error while writing is fatal.
        } catch (Throwable t) {
            Slog.wtf(TAG, "Failed to write settings, restoring backup", t);
            destination.failWrite(out);
            throw new IllegalStateException("Failed to write fingerprints", t);
        } finally {
            IoUtils.closeQuietly(out);
        }
    }

    @GuardedBy("this")
    @Override
    protected void parseBiometricsLocked(TypedXmlPullParser parser)
            throws IOException, XmlPullParserException {

        final int outerDepth = parser.getDepth();
        int type;
        while ((type = parser.next()) != XmlPullParser.END_DOCUMENT
                && (type != XmlPullParser.END_TAG || parser.getDepth() > outerDepth)) {
            if (type == XmlPullParser.END_TAG || type == XmlPullParser.TEXT) {
                continue;
            }

            String tagName = parser.getName();
            if (tagName.equals(TAG_FINGERPRINT)) {
                String name = parser.getAttributeValue(null, ATTR_NAME);
                String groupId = parser.getAttributeValue(null, ATTR_GROUP_ID);
                String fingerId = parser.getAttributeValue(null, ATTR_FINGER_ID);
                String deviceId = parser.getAttributeValue(null, ATTR_DEVICE_ID);
                mBiometrics.add(new Fingerprint(name, Integer.parseInt(groupId),
                        Integer.parseInt(fingerId), Long.parseLong(deviceId)));
            }
        }
    }
}
