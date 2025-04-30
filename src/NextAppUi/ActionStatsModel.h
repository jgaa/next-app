#pragma once

#include <memory>

#include <QObject>
#include <QQmlEngine>
#include <QDate>
#include <QUuid>

#include "ActionInfoCache.h"
#include "qcorotask.h"

class ActionStatsModel : public QObject
    , public std::enable_shared_from_this<ActionStatsModel>
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool valid MEMBER valid_ NOTIFY validChanged)
    Q_PROPERTY(int totalMinutes MEMBER totalMinutes_ NOTIFY validChanged)
    Q_PROPERTY(int totalSessions MEMBER totalSessions_ NOTIFY validChanged)
    Q_PROPERTY(int daysCount READ getDaysCount NOTIFY validChanged)
    Q_PROPERTY(QVariantList workInDays READ getWorkInDays NOTIFY validChanged)
    Q_PROPERTY(QString firstSessionDate READ getFirstSessionDate NOTIFY validChanged)
    Q_PROPERTY(QString lastSessionDate READ getLastSessionDate NOTIFY validChanged)
    Q_PROPERTY(const nextapp::pb::ActionInfo& actionInfo READ actionInfo NOTIFY validChanged)
    Q_PROPERTY(bool withOrigin MEMBER with_origin_ WRITE setWithOrigin NOTIFY validChanged)

    struct WorkInDay{
        QDateTime date;
        int minutes{};
        int sessions{};
    };

public:
    explicit ActionStatsModel(QUuid actionId);
    ~ActionStatsModel() override;

    // Starts the fetch process
    QCoro::Task<void> fetch();

    bool valid() const noexcept {
        return valid_;
    }

    void setWithOrigin(bool withOrigin) {
        with_origin_ = withOrigin;
        setValid(false);
        fetch();
    }

signals:
    void validChanged();

private:
    QVariantList getWorkInDays() const;
    QString getFirstSessionDate() const;
    QString getLastSessionDate() const;
    int getDaysCount() const {
        return workInDays_.size();
    }

    const nextapp::pb::ActionInfo& actionInfo() const;

    void setValid(bool valid);

    std::shared_ptr<nextapp::pb::ActionInfo> actionInfo_;
    std::vector<WorkInDay> workInDays_;
    QUuid action_id_;
    int totalMinutes_{};
    int totalSessions_{};
    bool valid_{false};
    bool with_origin_{false};
};

/*!
 * This is a wrapper class to expose the ActionStatsModel to QML.
 * The model is created in C++ and passed to QML as a pointer.
 * This allows us to manage the lifetime of the model in C++.
 */
class ActionStatsModelPtr : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(ActionStatsModel* model READ getModel NOTIFY modelChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)

signals:
    void modelChanged();
    void validChanged();

public:
    explicit ActionStatsModelPtr(std::shared_ptr<ActionStatsModel> model);
    ~ActionStatsModelPtr() override;

    ActionStatsModel* getModel() const {
        assert(model_);
        QQmlEngine::setObjectOwnership(model_.get(), QQmlEngine::CppOwnership);
        return model_.get();
    }

    bool valid() const noexcept {
        if (auto model = model_) {
            return model->valid();
        }

        return false;
    }

private:
    std::shared_ptr<ActionStatsModel> model_;
};
