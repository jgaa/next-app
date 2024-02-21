
#include <memory>
#include <QDate>
#include <QUuid>

#include "ActionsModel.h"
#include "ServerComm.h"
#include "MainTreeModel.h"

#include "logging.h"

using namespace std;
using namespace nextapp;

namespace {

template <typename T>
concept ActionType = std::is_same_v<T, pb::ActionInfo> || std::is_same_v<T, pb::Action>;

template <ActionType T, ActionType U>
int comparePriName(const T& left, const U& right) {
    if (left.priority() != right.priority()) {
        return left.priority() - right.priority();
    }

    return left.name().compare(right.name(), Qt::CaseInsensitive);
}

template <ActionType T, ActionType U>
int64_t compare(const T& left, const U& right) {
    if (left.kind() != right.kind()) {
        // Lowest is most significalt
        return left.kind() - right.kind();
    }

    assert(left.kind() == right.kind()) ;
    switch(left.kind()) {
    case pb::ActionKindGadget::ActionKind::AC_OVERDUE:
    case pb::ActionKindGadget::ActionKind::AC_UNSCHEDULED:
        return comparePriName(left, right);
    case pb::ActionKindGadget::ActionKind::AC_TODAY:
        if (left.dueByTime() || right.dueByTime()) {
            return left.dueByTime() - right.dueByTime();
        }
        return left.name().compare(right.name(), Qt::CaseInsensitive);
    case pb::ActionKindGadget::ActionKind::AC_UPCOMING:
        if (left.dueByTime() || right.dueByTime()) {
            return left.dueByTime() - right.dueByTime();
        }
        return comparePriName(left, right);
    case pb::ActionKindGadget::ActionKind::AC_DONE:
        if (left.completedTime() != right.completedTime()) {
            return left.completedTime() - right.completedTime();
        }
    case pb::ActionKindGadget::ActionKind::AC_UNSET:
        return comparePriName(left, right);
    }
}

template <ActionType T, ActionType U>
bool comparePred(const T& left, const U& right) {
    return compare(left, right) < 0LL;
}

template <ActionType T, ActionType U>
int findInsertRow(const T& action, const QList<U>& list) {
    int row = 0;
    // assume that list is already sorted

    bool prev_was_target = false;
    const U *prev = {};
    for(const auto& a: list) {
        if (comparePred(action, a)) {
            break;
        }
        prev_was_target = a.id_proto() == action.id_proto();
        ++row;
    }

    if (prev_was_target) {
        --row; // Assume exactely the same sorting order
        assert(row >= 0);
    }
    return row;
}

pb::ActionInfo toActionInfo(const pb::Action& action) {
    pb::ActionInfo ai;
    ai.setId_proto(action.id_proto());
    ai.setNode(action.node());
    ai.setPriority(action.priority());
    ai.setStatus(action.status());
    ai.setName(action.name());
    ai.setCreatedDate(ai.createdDate());
    ai.setDueType(action.dueType());
    ai.setCompleted(action.completed());
    ai.setCompletedTime(ai.completedTime());
    ai.setKind(action.kind());
    return ai;
}

void insertAction(QList<pb::ActionInfo>& list, const pb::Action& action, int row) {

    if (row >= list.size()) {
        list.append(toActionInfo(action));
    } else {
        list.insert(row, toActionInfo(action));
    }
}

} // anon ns

ActionsModel::ActionsModel(QObject *parent)
    : actions_{make_shared<nextapp::pb::Actions>()}
{
}

void ActionsModel::populate(QString node)
{
    // We re-allocate rather than reset. Then we don't have to check if the
    // object is valid before every use in other places.
    actions_ = make_shared<nextapp::pb::Actions>();

    nextapp::pb::GetActionsReq filter;
    filter.setActive(true);
    filter.setNode(node);

    fetch(filter);
}

void ActionsModel::addAction(const nextapp::pb::Action &action)
{
    ServerComm::instance().addAction(action);
}

void ActionsModel::updateAction(const nextapp::pb::Action &action)
{
    ServerComm::instance().updateAction(action);
}

void ActionsModel::deleteAction(const QString &uuid)
{
    ServerComm::instance().deleteAction(uuid);
}

nextapp::pb::Action ActionsModel::newAction()
{
    nextapp::pb::Action action;
    action.setPriority(nextapp::pb::ActionPriorityGadget::PRI_NORMAL);
    return action;
}

ActionPrx *ActionsModel::getAction(QString uuid)
{
    if (uuid.isEmpty()) {
        return new ActionPrx{};
    }

    auto prx = make_unique<ActionPrx>(uuid);
    return prx.release();
}

void ActionsModel::markActionAsDone(const QString &actionUuid, bool done)
{
    ServerComm::instance().markActionAsDone(actionUuid, done);
}

void ActionsModel::start()
{
    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedActions,
            this,
            &ActionsModel::receivedActions);

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::onUpdate,
            this,
            &ActionsModel::onUpdate);
}

void ActionsModel::fetch(nextapp::pb::GetActionsReq &filter)
{
    ServerComm::instance().getActions(filter);
}

void ActionsModel::receivedActions(const std::shared_ptr<nextapp::pb::Actions> &actions)
{
    LOG_DEBUG << "Action model reset with " << actions->actions().size() << " items";
    beginResetModel();
    actions_ = actions;
    std::ranges::sort(actions_->actions(), [](const auto& left, const auto& right) {
        return comparePred(left, right);
    });
    endResetModel();
}

template <ActionType T>
int findCurrentRow(const QList<T>& list, QString id) {

    auto it = std::ranges::find_if(list, [&id](const T& a) {
        return a.id_proto() == id;
    });

    if (it != list.end()) {
        auto row = ranges::distance(list.begin(), it);
        return row;
    }

    return -1;
}

void ActionsModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    if (!update->hasAction()) {
        return;
    }

    const auto& action = update->action();
    const auto op = update->op();

    if (op == pb::Update::Operation::ADDED || op == pb::Update::Operation::UPDATED) {
        if (action.node() != MainTreeModel::instance()->selected()) {
            return; // Irrelevant
        }
    }

    // TODO: For moved, we need to handle both cases, old node and new node

    switch(op) {
    case pb::Update::Operation::ADDED: {
insert_as_new:
        auto row = findInsertRow(action, actions_->actions());
        beginInsertRows({}, row, row);
        insertAction(actions_->actions(), action, row);
        endInsertRows();
    }
    break;
    case pb::Update::Operation::UPDATED: {
        auto row = findInsertRow(action, actions_->actions());
        if (auto currentRow = findCurrentRow(actions_->actions(), action.id_proto()) ; currentRow >=0 ) {
            auto& list = actions_->actions();
            if (list.at(currentRow).version() > action.version()) {
                // We have a newer version already. Ignore
                LOG_DEBUG << "Ignoring update of Action " << action.id_proto() << " \"" << action.name()
                          << "\", version " << action.version() << ": we already have a newer version.";
                return;
            }

            if (row != currentRow) {
                beginMoveRows({}, currentRow, currentRow, {}, min<int>(row, list.size()));
                if (row > currentRow) {
                    --row; // Compensate for the deleted row
                }
                list.removeAt(currentRow);
                insertAction(list, action, row);
                endMoveRows();
                const auto cix = index(row);
                emit dataChanged(cix, cix);
            } else {
                // Update in place
                auto &crow = list[currentRow];
                assert(crow.id_proto() == action.id_proto());
                crow = toActionInfo(action);
                const auto cix = index(currentRow);
                emit dataChanged(cix, cix);
            }
        } else {
            // Not found
            LOG_DEBUG << "Did not find updated action  " << action.id_proto() << " \"" << action.name()
                      << "\" in the current list af actions. Inserting it as new.";
            goto insert_as_new;
        }
    }
    break;
    case pb::Update::Operation::DELETED: {
        // The deleted event gives us an empty Action with just the id field containing information.
        if (auto currentRow = findCurrentRow(actions_->actions(), action.id_proto()) ; currentRow >=0 ) {
            beginRemoveRows({}, currentRow, currentRow);
            auto& list = actions_->actions();
            list.removeAt(currentRow);
            endRemoveRows();
        }
    }
    break;
    }
}

int ActionsModel::rowCount(const QModelIndex &parent) const
{
    return actions_->actions().size();
}

QVariant ActionsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const auto row = index.row();
    if (row < 0 && row >= actions_->actions().size()) {
        return {};
    }

    const auto& action = actions_->actions().at(row);

    switch(role) {
    case NameRole:
        return action.name();
    case UuidRole:
        return action.id_proto();
    case PriorityRole:
        return static_cast<unsigned>(action.priority());
    case StatusRole:
        return action.status();
    case NodeRole:
        return action.node();
    case CreatedDateRole:
        return QDate{action.createdDate().year(), action.createdDate().month(), action.createdDate().mday()};
    case DueTypeRole:
        return action.dueType();
    case DueByTimeRole:
        return static_cast<quint64>(action.dueByTime());
    case CompletedRole:
        return action.completed();
    case CompletedTimeRole:
        if (action.completedTime()) {
            return QDateTime::fromSecsSinceEpoch(action.completedTime());
        }
        return {};
    }

    return {};
}

QVariant ActionsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (section == 0 && orientation == Qt::Horizontal) {
        switch(role) {
        case NameRole:
            return "Name";
        case UuidRole:
            return "Id";
        case PriorityRole:
            return "Priority";
        case StatusRole:
            return "Status";
        case NodeRole:
            return "Node";
        case CreatedDateRole:
            return "CreatedDate";
        case DueTypeRole:
            return "DueType";
        case DueByTimeRole:
            return "DueBy";
        case CompletedTimeRole:
            return "CompletedTime";
        case CompletedRole:
            return "Done";
        }
    }

    return {};
}

QHash<int, QByteArray> ActionsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[UuidRole] = "uuid";
    roles[PriorityRole] = "priority";
    roles[StatusRole] = "status";
    roles[NodeRole] = "node";
    roles[CreatedDateRole] = "createdDate";
    roles[DueTypeRole] = "dueType";
    roles[DueByTimeRole] = "dueBy";
    roles[CompletedRole] = "done";
    roles[CompletedTimeRole] = "completedTime";
    return roles;
}

ActionPrx::ActionPrx(QString actionUuid)
    : valid_{false}, uuid_{actionUuid}
{
    if (QUuid{actionUuid}.isNull()) {
        throw runtime_error{"Invalid uuid for action"};
    }

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedAction,
            this,
            &ActionPrx::receivedAction);

    pb::GetActionReq req;
    req.setUuid(actionUuid);
    ServerComm::instance().getAction(req);
}

ActionPrx::ActionPrx()
    : valid_{true}
{
    action_.setPriority(nextapp::pb::ActionPriorityGadget::PRI_NORMAL);
}

void ActionPrx::receivedAction(const nextapp::pb::Status &status)
{
    if (!valid_) {
        if (status.hasAction()) {
            const auto& action = status.action();
            if (action.id_proto() == uuid_) {
                valid_ = true;
                action_ = action;
                emit actionChanged();
                emit validChanged();
                return;
            }
        }

        if (status.error() != pb::ErrorGadget::Error::OK) {
            // TODO: Add uuid to status so we can validate it's the relevant failure
            // TODO: Make sure that the UI handles the failure to get a action
            emit actionChanged();
        }
    }
}
