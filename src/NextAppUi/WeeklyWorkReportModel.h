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

public:
    enum WeekSelection {
        THIS_WEEK,
        LAST_WEEK,
        SELECTED_WEEK
    };

    Q_PROPERTY(bool isVisible READ isVisible WRITE setIsVisible NOTIFY isVisibleChanged) // Set by the view
    Q_PROPERTY(bool isAvailable READ isAvailable NOTIFY isAvailableChanged); // Set by the model
    Q_PROPERTY(time_t startTime READ startTime WRITE setStartTime NOTIFY startTimeChanged);
    Q_PROPERTY(WeekSelection weekSelection READ weekSelection WRITE setWeekSelection NOTIFY weekSelectionChanged FINAL)
public:
    enum Roles {
        SummaryRole = Qt::UserRole + 1,
    };

    struct NodeSummary {
        QString name;
        std::array<uint32_t , 7> duration = {};
    };

    void setStartTime(time_t when);
    time_t startTime() {
        return startTime_;
    }

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

    WeekSelection weekSelection() const;
    void setWeekSelection(WeekSelection when);

signals:
    void isVisibleChanged();
    void isAvailableChanged();
    void startTimeChanged();

    void weekSelectionChanged();

private:
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);
    void onUpdatedDuration();
    void needRefresh();

    bool initialized_ = false;
    bool online_ = false;
    bool visible_ = false;
    std::map<QUuid, NodeSummary> nodes_;
    std::vector<QUuid> sorted_nodes_;
    const QUuid uuid_ = QUuid::createUuid();
    WeekSelection week_selection_ = THIS_WEEK;
    time_t startTime_ = time({});
    bool refresh_when_activated_ = false;
};
