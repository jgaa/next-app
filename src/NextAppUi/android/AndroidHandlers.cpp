
#include <QDebug>
#include <jni.h>

#include "QCoreApplication"

#include "NextAppCore.h"
#include "logging.h"
#include "JniGuard.h"

namespace {
constexpr const char* kMainActivityClass = "eu/lastviking/nextapp/app/MainActivity";

void nativeOnSharedNextapp(JNIEnv *env, jobject /* activity */, jstring jPath)
{
    LOG_DEBUG_N << "Got a shared-file event.";
    qDebug() << "Got a shared-file event.";
    const char *cPath = env->GetStringUTFChars(jPath, nullptr);
    QString qPath = QString::fromUtf8(cPath);
    env->ReleaseStringUTFChars(jPath, cPath);

    LOG_DEBUG_N << "Got a shared-file event for " << qPath;

    NextAppCore::handleSharedFile(qPath);
}
} // namespace

namespace nextapp::android {

bool registerAndroidHandlersNatives(JNIEnv* env) {
    if (!env) {
        LOG_ERROR_N << "registerAndroidHandlersNatives: JNIEnv is null";
        return false;
    }

    jclass cls = env->FindClass(kMainActivityClass);
    if (!cls) {
        LOG_ERROR_N << "registerAndroidHandlersNatives: class not found: " << kMainActivityClass;
        return false;
    }

    static const JNINativeMethod methods[] = {
        {"nativeOnSharedNextapp", "(Ljava/lang/String;)V",
         reinterpret_cast<void*>(nativeOnSharedNextapp)},
    };

    if (env->RegisterNatives(cls, methods, sizeof(methods) / sizeof(methods[0])) != 0) {
        LOG_ERROR_N << "registerAndroidHandlersNatives: RegisterNatives failed for "
                    << kMainActivityClass;
        env->DeleteLocalRef(cls);
        return false;
    }

    env->DeleteLocalRef(cls);
    return true;
}

QString getMediaDir() {
#ifdef NEXTAPP_USE_JNI
    requireStaticMethod(
        kMainActivityClass,
        "getAppMediaDir",
        "()Ljava/lang/String;");

    static const QString dir = QJniObject::callStaticObjectMethod(
    kMainActivityClass,
    "getAppMediaDir",
    "()Ljava/lang/String;"
    ).toString();

    return dir;
#else
    LOG_WARN_N << "JNI not available, using temp dir for media dir.";
    return {};
#endif
}

QString getLogsDir() {
#ifdef NEXTAPP_USE_JNI
    requireStaticMethod(
        kMainActivityClass,
        "ensureMediaSubdir",
        "(Ljava/lang/String;)Ljava/lang/String;");

    static const QString dir = QJniObject::callStaticObjectMethod(
    kMainActivityClass,
    "ensureMediaSubdir",
    "(Ljava/lang/String;)Ljava/lang/String;",
    QJniObject::fromString("Logs").object<jstring>()
    ).toString();
    return dir;
#else
    LOG_WARN_N << "JNI not available, using temp dir for logs dir.";
    return QCoreApplication::applicationDirPath() + "/Logs";
#endif
}

QString getNamedDir(const QString& name) {
#ifdef NEXTAPP_USE_JNI
    requireStaticMethod(
        kMainActivityClass,
        "ensureMediaSubdir",
        "(Ljava/lang/String;)Ljava/lang/String;");

    static const QString dir = QJniObject::callStaticObjectMethod(
    kMainActivityClass,
    "ensureMediaSubdir",
    "(Ljava/lang/String;)Ljava/lang/String;",
    QJniObject::fromString(name).object<jstring>()
    ).toString();
    return dir;
#else
    LOG_WARN_N << "JNI not available, using temp dir for " << name << " dir.";
    return QCoreApplication::applicationDirPath() + "/" + name;
#endif
}

} // ns
