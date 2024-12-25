#pragma once

#include <set>
#include <QUuid>

#include <QObject>

// Create a concept ot any range of QUuid
template <typename T>
concept QUuidRange = requires(T t) {
    { *std::begin(t) } -> std::convertible_to<QUuid>;
    { *std::end(t) } -> std::convertible_to<QUuid>;
};

class ActionsOnCurrentCalendar : public QObject
{
    Q_OBJECT
public:
    using actions_t = std::set<QUuid>;

    explicit ActionsOnCurrentCalendar() = default;

    static ActionsOnCurrentCalendar* instance();

    template <QUuidRange T>
    void setActions(const T& actions) {
        actions_.clear();
        for (const auto& action : actions) {
            actions_.insert(action);
        }
        emit modelReset();
    }

    void addAction(const QUuid& action);
    void removeAction(const QUuid& action);

    [[nodiscard]] const actions_t& actions() const noexcept {
        return actions_;
    }

    bool contains(const QUuid& action) const noexcept {
        return actions_.contains(action);
    }

signals:
    void modelReset();
    void actionAdded(const QUuid& action);
    void actionRemoved(const QUuid& action);

private:
    actions_t actions_;
};
