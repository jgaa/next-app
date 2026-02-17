#include <jni.h>

#include "AndroidHandlers.h"
#include "logging.h"

#ifdef WITH_FCM
#include "AndroidFcmBridge.h"
#endif

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    if (!vm) {
        LOG_ERROR_N << "JNI_OnLoad: JavaVM is null";
        return JNI_ERR;
    }

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOG_ERROR_N << "JNI_OnLoad: failed to get JNIEnv";
        return JNI_ERR;
    }

    bool ok = nextapp::android::registerAndroidHandlersNatives(env);
#ifdef WITH_FCM
    ok = ok && registerAndroidFcmBridgeNatives(env);
#endif

    return ok ? JNI_VERSION_1_6 : JNI_ERR;
}
