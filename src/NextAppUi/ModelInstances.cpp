#include "ModelInstances.h"

ModelInstances::ModelInstances() {}

NotificationsModel *ModelInstances::getNotificationsModel() noexcept
{
    auto *nm = NotificationsModel::instance();
    // Set CPP as ownership, because we manage the lifetime of the model.
    QQmlEngine::setObjectOwnership(nm, QQmlEngine::CppOwnership);
    return nm;
}
