#include "ModelInstances.h"

using namespace std;

ModelInstances::ModelInstances() {}

NotificationsModel *ModelInstances::getNotificationsModel() noexcept
{
    auto *nm = NotificationsModel::instance();
    // Set CPP as ownership, because we manage the lifetime of the model.
    QQmlEngine::setObjectOwnership(nm, QQmlEngine::CppOwnership);
    return nm;
}

ActionStatsModelPtr *ModelInstances::getActionStatsModel(QString actionId) noexcept
{
    QUuid actionUuid = QUuid::fromString(actionId);
    if (actionUuid.isNull()) {
        LOG_WARN << "Invalid action UUID: " << actionId;
        assert(false);
        return nullptr;
    }

    try {
        auto model = make_shared<ActionStatsModel>(actionUuid);
        model->fetch();
        auto ptr = make_unique<ActionStatsModelPtr>(model);
        auto *rval = ptr.release();
        QQmlEngine::setObjectOwnership(rval, QQmlEngine::JavaScriptOwnership);
        return rval;
    } catch (const std::exception& e) {
        LOG_WARN << "Failed to create ActionStatsModel: " << e.what();
    }

    assert(false);
    return nullptr;
}
