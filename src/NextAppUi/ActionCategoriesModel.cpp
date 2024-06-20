#include <algorithm>

#include "ActionCategoriesModel.h"
#include "ServerComm.h"
#include "util.h"

using namespace std;

namespace {

bool compare(const nextapp::pb::ActionCategory& a, const nextapp::pb::ActionCategory& b) {
    return a.name() < b.name();
}

} // anon ns

ActionCategoriesModel* ActionCategoriesModel::instance_;

ActionCategoriesModel::ActionCategoriesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    assert(!instance_);
    instance_ = this;
    LOG_TRACE_N << "ActionCategoriesModel created";

    connect(&ServerComm::instance(), &ServerComm::onUpdate, this, &ActionCategoriesModel::onUpdate);

    connect(NextAppCore::instance(), &NextAppCore::onlineChanged, this, [this] (bool online) {
        onOnlineChanged(online);
    });

    if (ServerComm::instance().connected()) {
        setOnline(true);
    }
}

void ActionCategoriesModel::deleteCategory(const QString &id)
{
    if (online_) {
        ServerComm::instance().deleteActionCategory(id, [this] (auto val) {
            if (std::holds_alternative<ServerComm::CbError>(val)) {
                LOG_WARN_N << "Failed to delete category: " << std::get<ServerComm::CbError>(val).message;
            }
        });
    }
}

void ActionCategoriesModel::deleteSelection(const QModelIndexList &list)
{
    for(const auto& index : list) {
        auto id = data(index, IdRole).toString();
        deleteCategory(id);
    }
}

void ActionCategoriesModel::createCategory(const nextapp::pb::ActionCategory &category)
{
    if (online_) {
        ServerComm::instance().createActionCategory(category, [this] (auto val) {
            if (std::holds_alternative<ServerComm::CbError>(val)) {
                LOG_WARN_N << "Failed to create category: " << std::get<ServerComm::CbError>(val).message;
            }
        });
    }
}

void ActionCategoriesModel::updateCategory(const nextapp::pb::ActionCategory &category)
{
    if (online_) {
        ServerComm::instance().updateActionCategory(category, [this] (auto val) {
            if (std::holds_alternative<ServerComm::CbError>(val)) {
                LOG_WARN_N << "Failed to update category: " << std::get<ServerComm::CbError>(val).message;
            }
        });
    }
}

nextapp::pb::ActionCategory ActionCategoriesModel::get(int index)
{
    if (index < 0 || index >= action_categories_.categories().size()) {
        nextapp::pb::ActionCategory cat;
        cat.setColor("deepskyblue");
        return cat;
    }

    return action_categories_.categories().at(index);
}

QString ActionCategoriesModel::getName(const QString &id)
{
    if (const auto* cat = lookup(id)) {
        return cat->name();
    }

    return {};
}

int ActionCategoriesModel::getIndexByUuid(const QString &id)
{
    auto it = ranges::find_if(action_categories_.categories(), [&id](const nextapp::pb::ActionCategory& c) {
        return c.id_proto() == id;
    });

    if (it == action_categories_.categories().end()) {
        return -1;
    }

    const int ix = std::distance(action_categories_.categories().begin(), it);
    return ix;
}

QString ActionCategoriesModel::getColorFromUuid(const QString &id)
{
    auto it = ranges::find_if(action_categories_.categories(), [&id](const nextapp::pb::ActionCategory& c) {
        return c.id_proto() == id;
    });

    if (it == action_categories_.categories().end()) [[unlikely]] {
        return "transparent";
    }

    return it->color();
}

int ActionCategoriesModel::rowCount(const QModelIndex &parent) const
{
    return action_categories_.categories().size();
}

QVariant ActionCategoriesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const auto &category = action_categories_.categories().at(index.row());
    switch (role) {
    case NameRole:
        return category.name();
    case IconRole:
        return category.icon();
    case ColorRole:
        return category.color();
    case DescrRole:
        return category.descr();
    case IdRole:
        return category.id_proto();
    default:
        return {};
    }
}

QHash<int, QByteArray> ActionCategoriesModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {IconRole, "icon"},
        {ColorRole, "color"},
        {DescrRole, "descr"},
        {IdRole, "id"},
    };
}

ActionCategoriesModel &ActionCategoriesModel::instance()
{
    assert(instance_);
    return *instance_;
}

void ActionCategoriesModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    if (update->hasActionCategory()) {

        beginResetModel();

        ScopedExit do_later([this] {
            endResetModel();
        });

        const auto& cat = update->actionCategory();
        auto it = std::find_if(action_categories_.categories().begin(), action_categories_.categories().end(),
                               [&cat](const nextapp::pb::ActionCategory& c) {
            return c.id_proto() == cat.id_proto();
        });

        if (update->op() == nextapp::pb::Update::Operation::DELETED) {
            action_categories_.categories().erase(it);
        } else {
            if (it != action_categories_.categories().end()) {
                *it = cat;
            } else {
                action_categories_.categories().push_back(cat);
            }

            ranges::sort(action_categories_.categories(), compare);
        }
    }

}

void ActionCategoriesModel::setOnline(bool value) {
    LOG_TRACE_N << "Setting online to " << value;
    if (online_ != value) {
        online_ = value;
        emit onlineChanged();
        if (online_) {
            fetchIf();
        } else {
            setValid(false);
            beginResetModel();
            action_categories_.categories().clear();
            endResetModel();
        }
    }
}

void ActionCategoriesModel::setValid(bool value) {
    if (valid_ != value) {
        LOG_TRACE_N << "Setting valid to " << value;
        valid_ = value;
        emit validChanged();
    }
}

void ActionCategoriesModel::fetchIf()
{
    if (online_) {
        deleted_entries_.clear();
        ServerComm::instance().fetchActionCategories([this](auto val) {
            if (std::holds_alternative<ServerComm::CbError>(val)) {
                LOG_WARN_N << "Failed to get categories: " << std::get<ServerComm::CbError>(val).message;
                setValid(false);
                // TODO: What do we do now? Retry after a while?
                return;
            }

            auto& data = std::get<nextapp::pb::ActionCategories>(val);
            onReceivedActionCategories(data);
        });
    }
}

void ActionCategoriesModel::onReceivedActionCategories(nextapp::pb::ActionCategories &action_categories)
{
    beginResetModel();

    ScopedExit do_later([this] {
        endResetModel();
    });

    auto old = std::move(action_categories_);
    action_categories_ = std::move(action_categories);

    // Handle the case where the server already sent an update for a newer entry than what we received from our request.
    for(auto &cat : old.categories()) {
        if (auto it = ranges::find_if(action_categories_.categories(),
                               [&cat](const nextapp::pb::ActionCategory& c) {
            return c.id_proto() == cat.id_proto();
        }); it == action_categories_.categories().end()) {
            if (cat.version() > it->version()) {
                // Keep the old value
                *it = std::move(cat);
            }
        }
    }

    // Handle the case where a category was deleted after the server prepared the response, and we already got the notification.
    for(auto& del : deleted_entries_) {
        if (auto it = ranges::find_if(action_categories_.categories(),
                               [&del](const nextapp::pb::ActionCategory& c) {
            return c.id_proto() == del;
        }); it != action_categories_.categories().end()) {
            action_categories_.categories().erase(it);
        }
    }
    deleted_entries_.clear();

    ranges::sort(action_categories_.categories(), compare);

    setValid(true);
}

nextapp::pb::ActionCategory *ActionCategoriesModel::lookup(const QString &id)
{
    auto it = ranges::find_if(action_categories_.categories(), [&id](const nextapp::pb::ActionCategory& c) {
        return c.id_proto() == id;
    });

    if (it == action_categories_.categories().end()) {
        return nullptr;
    }

    return &*it;
}
