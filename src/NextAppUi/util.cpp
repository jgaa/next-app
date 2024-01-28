#include "util.h"

QString toValidQuid(const QString &str) {
    QUuid uuid{str};
    if (uuid.isNull()) {
        return {};
    }
    auto rval = uuid.toString(QUuid::WithoutBraces);
    return rval;
}
