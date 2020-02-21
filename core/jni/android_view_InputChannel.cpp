/*
 * Copyright (C) 2010 The Android Open Source Project
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

#define LOG_TAG "InputChannel-JNI"

#include "android-base/stringprintf.h"
#include <nativehelper/JNIHelp.h>
#include "nativehelper/scoped_utf_chars.h"
#include <android_runtime/AndroidRuntime.h>
#include <binder/Parcel.h>
#include <utils/Log.h>
#include <input/InputTransport.h>
#include "android_view_InputChannel.h"
#include "android_os_Parcel.h"
#include "android_util_Binder.h"

#include "core_jni_helpers.h"

namespace android {

// ----------------------------------------------------------------------------

static struct {
    jclass clazz;

    jfieldID mPtr;   // native object attached to the DVM InputChannel
} gInputChannelClassInfo;

// ----------------------------------------------------------------------------

class NativeInputChannel {
public:
    explicit NativeInputChannel(const sp<InputChannel>& inputChannel);
    ~NativeInputChannel();

    inline sp<InputChannel> getInputChannel() { return mInputChannel; }

    void setDisposeCallback(InputChannelObjDisposeCallback callback, void* data);
    void invokeAndRemoveDisposeCallback(JNIEnv* env, jobject obj);

private:
    sp<InputChannel> mInputChannel;
    InputChannelObjDisposeCallback mDisposeCallback;
    void* mDisposeData;
};

// ----------------------------------------------------------------------------

NativeInputChannel::NativeInputChannel(const sp<InputChannel>& inputChannel) :
    mInputChannel(inputChannel), mDisposeCallback(nullptr) {
}

NativeInputChannel::~NativeInputChannel() {
}

void NativeInputChannel::setDisposeCallback(InputChannelObjDisposeCallback callback, void* data) {
    mDisposeCallback = callback;
    mDisposeData = data;
}

void NativeInputChannel::invokeAndRemoveDisposeCallback(JNIEnv* env, jobject obj) {
    if (mDisposeCallback) {
        mDisposeCallback(env, obj, mInputChannel, mDisposeData);
        mDisposeCallback = nullptr;
        mDisposeData = nullptr;
    }
}

// ----------------------------------------------------------------------------

static NativeInputChannel* android_view_InputChannel_getNativeInputChannel(JNIEnv* env,
        jobject inputChannelObj) {
    jlong longPtr = env->GetLongField(inputChannelObj, gInputChannelClassInfo.mPtr);
    return reinterpret_cast<NativeInputChannel*>(longPtr);
}

sp<InputChannel> android_view_InputChannel_getInputChannel(JNIEnv* env, jobject inputChannelObj) {
    NativeInputChannel* nativeInputChannel =
            android_view_InputChannel_getNativeInputChannel(env, inputChannelObj);
    return nativeInputChannel != nullptr ? nativeInputChannel->getInputChannel() : nullptr;
}

void android_view_InputChannel_setDisposeCallback(JNIEnv* env, jobject inputChannelObj,
        InputChannelObjDisposeCallback callback, void* data) {
    NativeInputChannel* nativeInputChannel =
            android_view_InputChannel_getNativeInputChannel(env, inputChannelObj);
    if (nativeInputChannel == nullptr) {
        ALOGW("Cannot set dispose callback because input channel object has not been initialized.");
    } else {
        nativeInputChannel->setDisposeCallback(callback, data);
    }
}

static jlong android_view_InputChannel_createInputChannel(JNIEnv* env,
        sp<InputChannel> inputChannel) {
    std::unique_ptr<NativeInputChannel> nativeInputChannel =
            std::make_unique<NativeInputChannel>(inputChannel);

    return reinterpret_cast<jlong>(nativeInputChannel.release());
}

static jlongArray android_view_InputChannel_nativeOpenInputChannelPair(JNIEnv* env,
        jclass clazz, jstring nameObj) {
    ScopedUtfChars nameChars(env, nameObj);
    std::string name = nameChars.c_str();

    sp<InputChannel> serverChannel;
    sp<InputChannel> clientChannel;
    status_t result = InputChannel::openInputChannelPair(name, serverChannel, clientChannel);

    if (result) {
        std::string message = android::base::StringPrintf(
                "Could not open input channel pair : %s", strerror(-result));
        jniThrowRuntimeException(env, message.c_str());
        return nullptr;
    }

    jlongArray channelPair = env->NewLongArray(2);
    if (channelPair == nullptr) {
        return nullptr;
    }

    jlong* outArray = env->GetLongArrayElements(channelPair, 0);
    outArray[0] = android_view_InputChannel_createInputChannel(env, serverChannel);
    if (env->ExceptionCheck()) {
        return nullptr;
    }

    outArray[1] = android_view_InputChannel_createInputChannel(env, clientChannel);
    if (env->ExceptionCheck()) {
        return nullptr;
    }
    env->ReleaseLongArrayElements(channelPair, outArray, 0);

    return channelPair;
}

static void InputChannel_nativeDestroy(void *rawInputChannel) {
    NativeInputChannel* nativeInputChannel =
            reinterpret_cast<NativeInputChannel *>(rawInputChannel);
    if (nativeInputChannel) {
        delete nativeInputChannel;
    }
}

static jlong android_view_InputChannel_getNativeFinalizer(JNIEnv* env, jobject obj) {
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(&InputChannel_nativeDestroy));
}

static void android_view_InputChannel_nativeDispose(JNIEnv* env, jobject obj, jlong channel) {
    NativeInputChannel* nativeInputChannel =
            reinterpret_cast<NativeInputChannel*>(channel);

    if (nativeInputChannel) {
        nativeInputChannel->invokeAndRemoveDisposeCallback(env, obj);
    }
}

static jlong android_view_InputChannel_nativeReadFromParcel(JNIEnv* env, jobject obj,
        jobject parcelObj) {
    Parcel* parcel = parcelForJavaObject(env, parcelObj);
    if (parcel) {
        bool isInitialized = parcel->readInt32();
        if (isInitialized) {
            sp<InputChannel> inputChannel = InputChannel::read(*parcel);
            NativeInputChannel* nativeInputChannel = new NativeInputChannel(inputChannel);
            return reinterpret_cast<jlong>(nativeInputChannel);
        }
    }
    return 0;
}

static void android_view_InputChannel_nativeWriteToParcel(JNIEnv* env, jobject obj,
        jobject parcelObj, jlong channel) {
    Parcel* parcel = parcelForJavaObject(env, parcelObj);
    if (parcel == nullptr) {
        ALOGE("Could not obtain parcel for Java object");
        return;
    }
    NativeInputChannel* nativeInputChannel =
                reinterpret_cast<NativeInputChannel*>(channel);

    if (!nativeInputChannel) {
        parcel->writeInt32(0); // not initialized
        return;
    }
    parcel->writeInt32(1); // initialized
    nativeInputChannel->getInputChannel()->write(*parcel);
}

static jstring android_view_InputChannel_nativeGetName(JNIEnv* env, jobject obj, jlong channel) {
    NativeInputChannel* nativeInputChannel =
                reinterpret_cast<NativeInputChannel*>(channel);
    if (! nativeInputChannel) {
        return nullptr;
    }

    jstring name = env->NewStringUTF(nativeInputChannel->getInputChannel()->getName().c_str());
    return name;
}

static jlong android_view_InputChannel_nativeDup(JNIEnv* env, jobject obj, jlong channel) {
    NativeInputChannel* nativeInputChannel =
                reinterpret_cast<NativeInputChannel*>(channel);

    if (nativeInputChannel == nullptr) {
        jniThrowRuntimeException(env, "InputChannel has no valid NativeInputChannel");
        return 0;
    }

    sp<InputChannel> inputChannel = nativeInputChannel->getInputChannel();
    if (inputChannel == nullptr) {
        jniThrowRuntimeException(env, "NativeInputChannel has no corresponding InputChannel");
        return 0;
    }
    sp<InputChannel> dupInputChannel = inputChannel->dup();
    if (dupInputChannel == nullptr) {
        std::string message = android::base::StringPrintf(
                "Could not duplicate input channel %s", inputChannel->getName().c_str());
        jniThrowRuntimeException(env, message.c_str());
    }
    return reinterpret_cast<jlong>(new NativeInputChannel(dupInputChannel));
}

static jobject android_view_InputChannel_nativeGetToken(JNIEnv* env, jobject obj, jlong channel) {
    NativeInputChannel* nativeInputChannel =
                reinterpret_cast<NativeInputChannel*>(channel);
    if (nativeInputChannel) {
        return javaObjectForIBinder(env,
                nativeInputChannel->getInputChannel()->getConnectionToken());
    }
    return 0;
}

// ----------------------------------------------------------------------------

static const JNINativeMethod gInputChannelMethods[] = {
    /* name, signature, funcPtr */
    { "nativeOpenInputChannelPair", "(Ljava/lang/String;)[J",
            (void*)android_view_InputChannel_nativeOpenInputChannelPair },
    { "nativeGetFinalizer", "()J",
            (void*)android_view_InputChannel_getNativeFinalizer },
    { "nativeDispose", "(J)V",
            (void*)android_view_InputChannel_nativeDispose },
    { "nativeReadFromParcel", "(Landroid/os/Parcel;)J",
            (void*)android_view_InputChannel_nativeReadFromParcel },
    { "nativeWriteToParcel", "(Landroid/os/Parcel;J)V",
            (void*)android_view_InputChannel_nativeWriteToParcel },
    { "nativeGetName", "(J)Ljava/lang/String;",
            (void*)android_view_InputChannel_nativeGetName },
    { "nativeDup", "(J)J",
            (void*)android_view_InputChannel_nativeDup },
    { "nativeGetToken", "(J)Landroid/os/IBinder;",
            (void*)android_view_InputChannel_nativeGetToken },
};

int register_android_view_InputChannel(JNIEnv* env) {
    int res = RegisterMethodsOrDie(env, "android/view/InputChannel", gInputChannelMethods,
                                   NELEM(gInputChannelMethods));

    jclass clazz = FindClassOrDie(env, "android/view/InputChannel");
    gInputChannelClassInfo.clazz = MakeGlobalRefOrDie(env, clazz);

    gInputChannelClassInfo.mPtr = GetFieldIDOrDie(env, gInputChannelClassInfo.clazz, "mPtr", "J");

    return res;
}

} // namespace android
