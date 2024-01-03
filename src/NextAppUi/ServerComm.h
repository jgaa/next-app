#pragma once

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
public:
    using colors_in_months_t = std::shared_ptr<QList<QString>>;

    explicit ServerComm();
    ~ServerComm();

    void start();

    [[nodiscard]] QString version();
    [[nodiscard]] nextapp::pb::DayColorRepeated getDayColorsDefinitions();
    [[nodiscard]] MonthModel *getMonthModel(int year, int month);

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

    colors_in_months_t getColorsInMonth(unsigned year, unsigned month);

signals:
    void versionChanged();
    void dayColorDefinitionsChanged();
    void errorRecieved(const QString &value);
    void monthColorsChanged(unsigned year, unsigned month, colors_in_months_t colors);

private:
    void errorOccurred(const QGrpcStatus &status);
    void onServerInfo(nextapp::pb::ServerInfo info);

    std::unique_ptr<nextapp::pb::Nextapp::Client> client_;
    nextapp::pb::ServerInfo server_info_;
    nextapp::pb::DayColorDefinitions day_color_definitions_;
    QString server_version_{"Unknown"};
    std::map<std::pair<unsigned, unsigned>, colors_in_months_t> colors_in_months_;
    std::map<QUuid, QString> colors_;
    static ServerComm *instance_;
};
