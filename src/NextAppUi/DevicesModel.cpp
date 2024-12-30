#include "DevicesModel.h"
#include "ServerComm.h"


DevicesModel::DevicesModel()
{
    fetchIf();
}

int DevicesModel::rowCount(const QModelIndex &parent) const
{
    return devices_.size();
}

QVariant DevicesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= devices_.size()) {
        return {};
    }

    const auto& device = devices_[index.row()];

    switch (role) {
    case IdRole:
        return device.id_proto();
    case UserRole:
        return device.user();
    case NameRole:
        return device.name();
    case CreatedRole:
        return QDateTime::fromSecsSinceEpoch(device.created().seconds())
            .toString("yyyy-MM-dd hh:mm");
    case HostNameRole:
        return device.hostName();
    case OsRole:
        return device.os();
    case OsVersionRole:
        return device.osVersion();
    case AppVersionRole:
        return device.appVersion();
    case ProductTypeRole:
        return device.productType();
    case ProductVersionRole:
        return device.productVersion();
    case ArchRole:
        return device.arch();
    case PrettyNameRole:
        return device.prettyName();
    case LastSeenRole:
        if (!device.lastSeen().seconds()) {
            return tr("Never");
        }
        return QDateTime::fromSecsSinceEpoch(device.lastSeen().seconds())
            .toString("yyyy-MM-dd hh:mm");
    case EnabledRole:
        return device.enabled();
    case NumSessionsRole:
        return static_cast<qlonglong>(device.numSessions());
    default:
        assert(false && "Unhandled role");
    }

    return {};
}

QHash<int, QByteArray> DevicesModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {UserRole, "user"},
        {NameRole, "name"},
        {CreatedRole, "created"},
        {HostNameRole, "hostName"},
        {OsRole, "os"},
        {OsVersionRole, "osVersion"},
        {AppVersionRole, "appVersion"},
        {ProductTypeRole, "productType"},
        {ProductVersionRole, "productVersion"},
        {ArchRole, "arch"},
        {PrettyNameRole, "prettyName"},
        {LastSeenRole, "lastSeen"},
        {EnabledRole, "deviceEnabled"},
        {NumSessionsRole, "numSessions"},
    };
}

DevicesModel *DevicesModel::instance()
{
    static DevicesModel model;
    return &model;
}

void DevicesModel::setValid(bool valid) {
    if (valid_ == valid) {
        return;
    }

    valid_ = valid;
    emit validChanged();
}

QCoro::Task<void> DevicesModel::fetchIf()
{
    valid_ = false;
    beginResetModel();
    devices_.clear();
    endResetModel();

    if (!ServerComm::instance().connected()) {
        setValid(false);
        co_return;
    };

    auto res = co_await ServerComm::instance().fetchDevices();
    if (res.error() == nextapp::pb::ErrorGadget::Error::OK && res.hasDevices()) {
        beginResetModel();
        devices_.clear();
        for(const auto& device : res.devices().devices()) {
            devices_.push_back(device);
        }
        endResetModel();
        setValid(true);
    } else {
        setValid(false);
    }
}

void DevicesModel::enableDevice(QString deviceId, bool active)
{
    doEnableDevice(deviceId, active);
}

void DevicesModel::deleteDevice(QString deviceId)
{
    doDeleteDevice(deviceId);
}

void DevicesModel::refresh()
{
    fetchIf();
}

QCoro::Task<void> DevicesModel::doEnableDevice(QString deviceId, bool active) {
    const auto status = co_await ServerComm::instance().enableDevice(deviceId, active);
    if (status.error() == nextapp::pb::ErrorGadget::Error::OK) {
        if (status.hasDevice()) {
            const auto& device = status.device();
            auto row = 0;
            for (auto& d : devices_) {
                if (d.id_proto() == device.id_proto()) {
                    d = device;
                    const auto ix = createIndex(row, 0);
                    emit dataChanged(ix, ix);
                    break;
                }
                ++row;
            }
        };
    }
}

QCoro::Task<void> DevicesModel::doDeleteDevice(QString deviceId)
{
    const auto status = co_await ServerComm::instance().deleteDevice(deviceId);
    if (status.error() == nextapp::pb::ErrorGadget::Error::OK) {
        fetchIf();
    }
}
