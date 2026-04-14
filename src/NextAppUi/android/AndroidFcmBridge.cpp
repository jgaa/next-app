#include "AndroidFcmBridge.h"
#include "logging.h"
#include "QCoreApplication"
#include <jni.h>

namespace {
constexpr const char* kMainActivityClass = "eu/lastviking/nextapp/app/MainActivity";
constexpr const char* kFcmServiceClass = "eu/lastviking/nextapp/app/NextappFirebaseMessagingService";

QString fetchFcmTokenFromJava() {
// Make sure the JVM is up and the activity is created:
    QJniEnvironment env;
    // Call the static Java method you added:
    QJniObject tokenObj = QJniObject::callStaticObjectMethod(
        kMainActivityClass,
        "getFcmToken",                              // method name
        "()Ljava/lang/String;"                      // JNI signature
    );
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return {};
    }
    if (tokenObj.isValid()){
        return tokenObj.toString();
    }
    LOG_WARN_N << "Failed to fetch FCM token from Java";
    return {};
}

QString fetchFcmPackageIdFromJava() {
// Make sure the JVM is up and the activity is created:
    QJniEnvironment env;
    // Call the static Java method you added:
    QJniObject tokenObj = QJniObject::callStaticObjectMethod(
        kMainActivityClass,
        "getFcmProjectId",                              // method name
        "()Ljava/lang/String;"                      // JNI signature
    );
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return {};
    }
    if (tokenObj.isValid()){
        return tokenObj.toString();
    }
    LOG_WARN_N << "Failed to fetch FCM ProjectId from Java";
    return {};
}

} // ns

AndroidFcmBridge &AndroidFcmBridge::instance() {
    static AndroidFcmBridge inst;
    return inst;
}

QString AndroidFcmBridge::getToken() {
    if (token_.isEmpty()) {
        LOG_TRACE_N << "Fetching FCM token from Java";
        token_ = fetchFcmTokenFromJava();
    }

    return token_;
}

QString AndroidFcmBridge::getPackageId() {
    if (packageId_.isEmpty()) {
        packageId_ = fetchFcmPackageIdFromJava();
    }
    return packageId_;
}

namespace {
void nativeOnNewToken(JNIEnv *env, jobject /*thiz*/, jstring jtoken)
{
    nextapp::logging::initAndroidLogging();
    LOG_DEBUG_N << "Fcm Called";
    const char *token = env->GetStringUTFChars(jtoken, nullptr);
    QMetaObject::invokeMethod(&AndroidFcmBridge::instance(),
                              [token]() {
                                  const auto str_token = QString::fromUtf8(token);
                                  LOG_DEBUG_N << "Token: " <<  str_token.left(8) << "...";
                                  AndroidFcmBridge::instance().setToken(QString::fromUtf8(token));
                              },
                              Qt::QueuedConnection);
    env->ReleaseStringUTFChars(jtoken, token);
}

void nativeOnMessage(JNIEnv *env, jobject /*thiz*/,
    jstring jmsgId, jstring jnotif, jstring jdata)
{
    nextapp::logging::initAndroidLogging();
    LOG_DEBUG_N << "Fcm Called. "
        << (QCoreApplication::instance() ? "Have QCoreApplication" : "No QCoreApplication");

    auto getString = [&](jstring js) {
        const char *s = env->GetStringUTFChars(js, nullptr);
        QString qs = QString::fromUtf8(s);
        env->ReleaseStringUTFChars(js, s);
        return qs;
    };
    QString msgId = getString(jmsgId);
    QString notif = getString(jnotif);
    QString data  = getString(jdata);

    QMetaObject::invokeMethod(&AndroidFcmBridge::instance(),
                              [msgId,notif,data]() {
                              LOG_DEBUG_N << "Message received: "
                                          << "msgId: " << msgId
                                          << ", notif: " << notif
                                          << ", data: " << data;
                                  emit AndroidFcmBridge::instance().messageReceived(msgId, notif, data);
                              },
                              Qt::QueuedConnection);
}
} // namespace

bool registerAndroidFcmBridgeNatives(JNIEnv* env) {
    if (!env) {
        LOG_ERROR_N << "registerAndroidFcmBridgeNatives: JNIEnv is null";
        return false;
    }

    jclass cls = env->FindClass(kFcmServiceClass);
    if (!cls) {
        LOG_ERROR_N << "registerAndroidFcmBridgeNatives: class not found: " << kFcmServiceClass;
        return false;
    }

    static const JNINativeMethod methods[] = {
        {"nativeOnNewToken", "(Ljava/lang/String;)V",
         reinterpret_cast<void*>(nativeOnNewToken)},
        {"nativeOnMessage", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
         reinterpret_cast<void*>(nativeOnMessage)},
    };

    if (env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != 0) {
        LOG_ERROR_N << "registerAndroidFcmBridgeNatives: RegisterNatives failed for "
                    << kFcmServiceClass;
        env->DeleteLocalRef(cls);
        return false;
    }

    env->DeleteLocalRef(cls);
    return true;
}
