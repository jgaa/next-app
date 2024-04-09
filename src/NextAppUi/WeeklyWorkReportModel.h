#pragma once

#include <QQmlEngine>
#include <QAbstractTableModel>
#include "ServerComm.h"
#include "util.h"
#include "nextapp.qpb.h"

class WeeklyWorkReportModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isVisible READ isVisible WRITE setIsVisible NOTIFY isVisibleChanged) // Set by the view
    Q_PROPERTY(bool isAvailable READ isAvailable NOTIFY isAvailableChanged)
public:
    enum Roles {
        SummaryRole = Qt::UserRole + 1,
    };

    struct NodeSummary {
        QString name;
        std::array<uint32_t , 7> duration = {};
    };

    WeeklyWorkReportModel(QObject *parent = nullptr);

    bool isVisible() const noexcept {
        return visible_;
    }

    void setIsVisible(bool visible);

    bool isAvailable() const noexcept {
        return initialized_ && online_;
    }

    void setOnline(bool online);

    void start();
    void fetch();

    void receivedDetailedWorkSummary(const nextapp::pb::DetailedWorkSummary& summary, const ServerComm::MetaData& meta);

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void isVisibleChanged();
    void isAvailableChanged();

private:
    bool initialized_ = false;
    bool online_ = false;
    bool visible_ = false;
    std::map<QUuid, NodeSummary> nodes_;
    std::vector<QUuid> sorted_nodes_;
    const QUuid uuid_ = QUuid::createUuid();
};
