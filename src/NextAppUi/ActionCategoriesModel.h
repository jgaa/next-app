#pragma once

#include <set>

#include <QObject>
#include <QQmlEngine>
#include <QAbstractListModel>
#include "qcorotask.h"

#include "NextAppCore.h"

class ActionCategoriesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    enum Roles {
        NameRole = Qt::UserRole + 1,
        IconRole,
        ColorRole,
        DescrRole,
        IdRole,
    };

    Q_PROPERTY(bool valid READ valid NOTIFY validChanged FINAL)

public:
    ActionCategoriesModel(QObject *parent = {});

    Q_INVOKABLE void deleteCategory(const QString& id);
    Q_INVOKABLE void deleteSelection(const QModelIndexList& list);
    Q_INVOKABLE void createCategory(const nextapp::pb::ActionCategory& category);
    Q_INVOKABLE void updateCategory(const nextapp::pb::ActionCategory& category);
    Q_INVOKABLE nextapp::pb::ActionCategory get(int index);
    Q_INVOKABLE QString getName(const QString& id);
    Q_INVOKABLE int getIndexByUuid(const QString& id);
    Q_INVOKABLE QString getColorFromUuid(const QString& id);

    const nextapp::pb::ActionCategory& getFromUuid(const QString& uuid);

    void onOnlineChanged(bool online) {
        setOnline(online);
    }

    bool valid() const noexcept {
        return valid_ && online_;
    }

    bool online() const noexcept {
        return online_;
    }

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;


    static ActionCategoriesModel& instance();

    QCoro::Task<bool> synch();

signals:
    void validChanged();
    void onlineChanged();

private:
    QCoro::Task<void> onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);
    void setOnline(bool value);
    void setValid(bool value);
    // void fetchIf();
    // void onReceivedActionCategories(nextapp::pb::ActionCategories& action_categories);
    QCoro::Task<bool> synchFromServer();
    QCoro::Task<bool> loadFromDb();
    QCoro::Task<bool> save(const nextapp::pb::ActionCategory& category);
    QCoro::Task<bool> remove(const QString& id);

    nextapp::pb::ActionCategory *lookup(const QString& id);

    bool online_{};
    bool valid_{};
    QList<nextapp::pb::ActionCategory> action_categories_;
    std::set<QString> deleted_entries_;
    static ActionCategoriesModel *instance_;
};
