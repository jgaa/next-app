#pragma once

#include <queue>
#include <qqmlregistration.h>

#include <memory>

#include "nextapp_client.grpc.qpb.h"

#include <QObject>
#include "MonthModel.h"

class ServerComm : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString version
                   READ version
                   NOTIFY versionChanged)
    Q_PROPERTY(nextapp::pb::DayColorRepeated dayColorDefinitions
                   READ getDayColorsDefinitions
                   NOTIFY dayColorDefinitionsChanged)

    Q_PROPERTY(QString defaultServerAddress READ getDefaultServerAddress CONSTANT)
public:
    using colors_in_months_t = std::shared_ptr<QList<QUuid>>;

    explicit ServerComm();
    ~ServerComm();

    void start();
    void stop();

    [[nodiscard]] QString version();
    [[nodiscard]] nextapp::pb::DayColorRepeated getDayColorsDefinitions();
    Q_INVOKABLE MonthModel *getMonthModel(int year, int month);

    // Called when the servers app settings may have changed
    Q_INVOKABLE void reloadSettings();

    static ServerComm& instance() noexcept {
        assert(instance_);
        return *instance_;
    }

    static const nextapp::pb::DayColorDefinitions& getDayColorDefs() noexcept {
        return instance().day_color_definitions_;
    }

    static const nextapp::pb::ServerInfo& getServerInfo() noexcept {
        return instance().server_info_;
    }

    colors_in_months_t getColorsInMonth(unsigned year, unsigned month, bool force = false);

    QString toDayColorName(const QUuid& uuid) const;

    void setDayColor(int year, int month, int day, QUuid colorUuid);
    void addNode(const nextapp::pb::Node& node);

    // node.parent cannoty be changed. Only data-values can be updated.
    void updateNode(const nextapp::pb::Node& node);

    void deleteNode(const QUuid& uuid);

    void getNodeTree();

    static QString getDefaultServerAddress() {
        return SERVER_ADDRESS;
    }

signals:
    void versionChanged();
    void dayColorDefinitionsChanged();
    void errorRecieved(const QString &value);

    // When the server has replied to our request fo the colors for this month
    void monthColorsChanged(unsigned year, unsigned month, colors_in_months_t colors);

    // When an update-message is received from the server regarding a change for a color on a day
    void dayColorChanged(unsigned year, unsigned month, unsigned day, QUuid uuid);

    // When we get the full node-list
    void receivedNodeTree(const nextapp::pb::NodeTree& tree);

    // Triggered on all updates from the server
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);

private:
    void errorOccurred(const QGrpcStatus &status);
    void onServerInfo(nextapp::pb::ServerInfo info);
    void onGrpcReady();
    void onUpdateMessage();

    std::unique_ptr<nextapp::pb::Nextapp::Client> client_;
    nextapp::pb::ServerInfo server_info_;
    nextapp::pb::DayColorDefinitions day_color_definitions_;
    QString server_version_{"Unknown"};
    std::map<std::pair<unsigned, unsigned>, colors_in_months_t> colors_in_months_;
    std::map<QUuid, QString> colors_;
    std::queue<std::function<void()>> grpc_queue_;
    bool grpc_is_ready_ = false;
    static ServerComm *instance_;
    std::shared_ptr<QGrpcStream> updates_;
    QString current_server_address_;
};
