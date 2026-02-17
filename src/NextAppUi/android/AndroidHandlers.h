#pragma once

#include <QString>
#include <QJniObject>

struct JNIEnv;

namespace nextapp {
namespace android {

QString getMediaDir();
QString getLogsDir();
QString getNamedDir(const QString& name);
bool registerAndroidHandlersNatives(JNIEnv* env);

} }
