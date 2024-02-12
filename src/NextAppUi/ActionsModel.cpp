
#include <QDate>

#include "ActionsModel.h"
#include "ServerComm.h"

#include "logging.h"

using namespace std;

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

nextapp::pb::Action ActionsModel::newAction()
{
    nextapp::pb::Action action;
    action.setPriority(nextapp::pb::ActionPriorityGadget::PRI_NORMAL);
    return action;
}

void ActionsModel::start()
{
    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedActions,
            this,
            &ActionsModel::receivedActions);
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
    endResetModel();
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
    case DescrRole:
        return action.descr();
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
        case DescrRole:
            return "Description";
        case NodeRole:
            return "Node";
        case CreatedDateRole:
            return "CreatedDate";
        case DueTypeRole:
            return "DueType";
        case DueByTimeRole:
            return "DueBy";
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
    roles[DescrRole] = "descr";
    roles[NodeRole] = "node";
    roles[CreatedDateRole] = "createdDate";
    roles[DueTypeRole] = "dueType";
    roles[DueByTimeRole] = "dueBy";
    roles[CompletedRole] = "done";
    return roles;
}

