#pragma once

#include <QString>
#include <QJniObject>

#if defined(__ANDROID__)
#include <jni.h>
#else
struct JNIEnv;
#endif

namespace nextapp {
namespace android {

QString getMediaDir();
QString getLogsDir();
QString getNamedDir(const QString& name);
bool registerAndroidHandlersNatives(JNIEnv* env);

} }
