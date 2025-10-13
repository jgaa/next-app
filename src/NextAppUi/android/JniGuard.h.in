#pragma once

#include <string>
#include <QString>
#include <QDebug>

#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>

#include "logging.h"

namespace nextapp {
namespace android {

// Convert "com/example/Foo" -> "com.example.Foo" (QString)
inline QString toDotNameStr(const char* slashName) {
    QString s = QString::fromUtf8(slashName ? slashName : "");
    return s.replace(u'/', u'.');
}

// Keep a QJniObject overload if you were using a Java String somewhere
inline QJniObject toDotName(const char* slashName) {
    return QJniObject::fromString(toDotNameStr(slashName));
}

#ifdef NEXTAPP_USE_JNI

// Utility: describe & clear any pending JNI exception; returns true if there was one.
inline bool describeAndClearPendingJniException(JNIEnv* env) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();   // prints to logcat
        env->ExceptionClear();
        return true;
    }
    return false;
}

inline bool describeAndClearPendingJniException(QJniEnvironment& env) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }
    return false;
}

// --- Validation helpers ------------------------------------------------------

inline void checkClass(const char* slashClass) {
    QJniEnvironment env;
    jclass cls = env->FindClass(slashClass); // local ref
    if (!cls || describeAndClearPendingJniException(env)) {
        LOG_ERROR_N << "JNI class not found:" << (slashClass ? slashClass : "<null>");
        ::abort();
    }
    env->DeleteLocalRef(cls);
}

inline void checkStaticMethod(const char* slashClass,
                              const char* methodName,
                              const char* jniSig)
{
    QJniEnvironment env;
    jclass cls = env->FindClass(slashClass); // local ref
    if (!cls || describeAndClearPendingJniException(env)) {
        LOG_ERROR_N << "JNI class not found when checking static method:"
                    << (slashClass ? slashClass : "<null>") << methodName << jniSig;
        ::abort();
    }

    jmethodID mid = env->GetStaticMethodID(cls, methodName, jniSig);
    bool hadEx = describeAndClearPendingJniException(env);
    env->DeleteLocalRef(cls);

    if (!mid || hadEx) {
        LOG_ERROR_N << "JNI static method not found:" << methodName << jniSig
                    << " in " << (slashClass ? slashClass : "<null>");
        ::abort();
    }
}

inline void checkInstanceMethod(jobject obj,
                                const char* methodName,
                                const char* jniSig)
{
    if (!obj) {
        LOG_ERROR_N << "JNI object is null when checking instance method"
                    << methodName << jniSig;
        ::abort();
    }

    QJniEnvironment env;
    jclass cls = env->GetObjectClass(obj); // local ref
    if (!cls || describeAndClearPendingJniException(env)) {
        LOG_ERROR_N << "JNI failed to obtain object class for instance method check";
        ::abort();
    }

    jmethodID mid = env->GetMethodID(cls, methodName, jniSig);
    bool hadEx = describeAndClearPendingJniException(env);
    env->DeleteLocalRef(cls);

    if (!mid || hadEx) {
        LOG_ERROR_N << "JNI instance method not found:" << methodName << jniSig;
        ::abort();
    }
}

inline void checkStaticField(const char* slashClass,
                             const char* fieldName,
                             const char* jniSig)
{
    QJniEnvironment env;
    jclass cls = env->FindClass(slashClass);
    if (!cls || describeAndClearPendingJniException(env)) {
        LOG_ERROR_N << "JNI class not found when checking static field:"
                    << (slashClass ? slashClass : "<null>") << fieldName << jniSig;
        ::abort();
    }

    jfieldID fid = env->GetStaticFieldID(cls, fieldName, jniSig);
    bool hadEx = describeAndClearPendingJniException(env);
    env->DeleteLocalRef(cls);

    if (!fid || hadEx) {
        LOG_ERROR_N << "JNI static field not found:" << fieldName << jniSig;
        ::abort();
    }
}

inline void checkInstanceField(jobject obj,
                               const char* fieldName,
                               const char* jniSig)
{
    if (!obj) {
        LOG_ERROR_N << "JNI object is null when checking instance field"
                    << fieldName << jniSig;
        ::abort();
    }

    QJniEnvironment env;
    jclass cls = env->GetObjectClass(obj);
    if (!cls || describeAndClearPendingJniException(env)) {
        LOG_ERROR_N << "JNI failed to obtain object class for instance field check";
        ::abort();
    }

    jfieldID fid = env->GetFieldID(cls, fieldName, jniSig);
    bool hadEx = describeAndClearPendingJniException(env);
    env->DeleteLocalRef(cls);

    if (!fid || hadEx) {
        LOG_ERROR_N << "JNI instance field not found:" << fieldName << jniSig;
        ::abort();
    }
}

/// === place this AFTER the check* functions ===
inline void requireClass(const char* slashClass) {
    checkClass(slashClass);
}
inline void requireStaticMethod(const char* slashClass,
                                const char* methodName,
                                const char* jniSig) {
    checkStaticMethod(slashClass, methodName, jniSig);
}
inline void requireInstanceMethod(jobject obj,
                                  const char* methodName,
                                  const char* jniSig) {
    checkInstanceMethod(obj, methodName, jniSig);
}
inline void requireStaticField(const char* slashClass,
                               const char* fieldName,
                               const char* jniSig) {
    checkStaticField(slashClass, fieldName, jniSig);
}
inline void requireInstanceField(jobject obj,
                                 const char* fieldName,
                                 const char* jniSig) {
    checkInstanceField(obj, fieldName, jniSig);
}

#else // NEXTAPP_USE_JNI

// No-ops that still log helpful messages in non-Android builds.
inline void checkClass(const char* slashClass) {
    LOG_WARN_N << "JNI is not enabled, cannot check class" << (slashClass ? slashClass : "<null>");
}
inline void checkStaticMethod(const char* slashClass, const char* methodName, const char* jniSig) {
    LOG_WARN_N << "JNI is not enabled, cannot check static method" << methodName << jniSig
               << " in " << (slashClass ? slashClass : "<null>");
}
inline void checkInstanceMethod(jobject, const char* methodName, const char* jniSig) {
    LOG_WARN_N << "JNI is not enabled, cannot check instance method" << methodName << jniSig;
}
inline void checkStaticField(const char* slashClass, const char* fieldName, const char* jniSig) {
    LOG_WARN_N << "JNI is not enabled, cannot check static field" << fieldName << jniSig
               << " in " << (slashClass ? slashClass : "<null>");
}
inline void checkInstanceField(jobject, const char* fieldName, const char* jniSig) {
    LOG_WARN_N << "JNI is not enabled, cannot check instance field" << fieldName << jniSig;
}

#endif // NEXTAPP_USE_JNI

} // namespace android
} // namespace nextapp
