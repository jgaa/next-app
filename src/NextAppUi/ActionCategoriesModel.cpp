#include <algorithm>

#include <QProtobufSerializer>

#include "ActionCategoriesModel.h"
#include <ReviewModel.h>
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

    // if (ServerComm::instance().connected()) {
    //     setOnline(true);
    // }
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
    if (index < 0 || index >= action_categories_.size()) {
        nextapp::pb::ActionCategory cat;
        cat.setColor("deepskyblue");
        return cat;
    }

    return action_categories_.at(index);
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
    auto it = ranges::find_if(action_categories_, [&id](const nextapp::pb::ActionCategory& c) {
        return c.id_proto() == id;
    });

    if (it == action_categories_.end()) {
        return -1;
    }

    const int ix = std::distance(action_categories_.begin(), it);
    return ix;
}

QString ActionCategoriesModel::getColorFromUuid(const QString &id)
{
    auto it = ranges::find_if(action_categories_, [&id](const nextapp::pb::ActionCategory& c) {
        return c.id_proto() == id;
    });

    if (it == action_categories_.end()) [[unlikely]] {
        return "transparent";
    }

    return it->color();
}

const nextapp::pb::ActionCategory &ActionCategoriesModel::getFromUuid(const QString &uuid)
{
    static const nextapp::pb::ActionCategory empty;
    auto it = ranges::find_if(action_categories_, [&uuid](const nextapp::pb::ActionCategory& c) {
        return c.id_proto() == uuid;
    });

    if (it != action_categories_.end()) [[unlikely]] {
        return *it;
    }

    return empty;
}

int ActionCategoriesModel::rowCount(const QModelIndex &parent) const
{
    return action_categories_.size();
}

QVariant ActionCategoriesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const auto &category = action_categories_.at(index.row());
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

QCoro::Task<bool> ActionCategoriesModel::synch()
{
    // See if we need to fetch from server
    const auto server_ver = ServerComm::instance().getServerDataVersions().actionCategoryVersion();
    const auto local_ver = ServerComm::instance().getLocalDataVersions().actionCategoryVersion();
    if (server_ver == 0 || server_ver > local_ver) {
        if (!co_await synchFromServer()) {
            co_return false;
        }
    }

    co_return co_await loadFromDb();
}

QCoro::Task<void> ActionCategoriesModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    if (update->hasActionCategory()) {

        beginResetModel();

        ScopedExit do_later([this] {
            endResetModel();
        });

        // We need a copy since update lilkely goes out of scope before we finish.
        const auto cat = update->actionCategory();

        auto it = std::find_if(action_categories_.begin(), action_categories_.end(),
                               [&cat](const nextapp::pb::ActionCategory& c) {
            return c.id_proto() == cat.id_proto();
        });

        if (update->op() == nextapp::pb::Update::Operation::DELETED) {
            action_categories_.erase(it);
            co_await remove(cat.id_proto());
        } else {
            co_await save(cat);
            if (it != action_categories_.end()) {
                *it = cat;
            } else {
                action_categories_.push_back(cat);
            }

            ranges::sort(action_categories_, compare);
        }
    }

}

void ActionCategoriesModel::setOnline(bool value) {
    LOG_TRACE_N << "Setting online to " << value;
    if (online_ != value) {
        online_ = value;
        emit onlineChanged();
        if (!online_) {
            setValid(false);
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

QCoro::Task<bool> ActionCategoriesModel::synchFromServer()
{
    auto res = co_await ServerComm::instance().getActionCategories({});
    if (res.error() == nextapp::pb::ErrorGadget::Error::OK) {
        if (res.hasActionCategories()) {

            auto& db = NextAppCore::instance()->db();
            co_await db.query("DELETE FROM action_category");

            const auto& cats = res.actionCategories();
            for(const auto cat : cats.categories()) {
                if (!co_await save(cat)) {
                    co_return false;
                }
            }
        }

        co_return true;
    }

    co_return false;
}

QCoro::Task<bool> ActionCategoriesModel::loadFromDb()
{
    auto& db = NextAppCore::instance()->db();
    beginResetModel();

    ScopedExit do_later([this] {
        endResetModel();
    });

    auto res = co_await db.query("SELECT data FROM action_category");
    if (res.has_value()) {
        action_categories_.clear();
        for(const auto& row : res.value()) {
            const auto& data = row.at(0).toByteArray();
            nextapp::pb::ActionCategory cat;
            QProtobufSerializer serializer;
            cat.deserialize(&serializer, data);
            action_categories_.push_back(cat);
        }

        ranges::sort(action_categories_, compare);
        setValid(true);
        co_return true;
    }

    co_return false;
}

QCoro::Task<bool> ActionCategoriesModel::save(const nextapp::pb::ActionCategory &category)
{
    auto& db = NextAppCore::instance()->db();
    QProtobufSerializer serializer;
    QList<QVariant> params;

    params.append(category.id_proto());
    params.append(static_cast<uint>(category.version()));
    params.append(category.serialize(&serializer));

    const auto res = co_await db.query("INSERT INTO action_category (id, version, data) VALUES (?, ?, ?)", &params);

    if (res.has_value()) {
        co_return true;
    }

    co_return false;
}

QCoro::Task<bool> ActionCategoriesModel::remove(const QString &id)
{
    auto& db = NextAppCore::instance()->db();
    DbStore::param_t params;
    params.append(id);
    const auto res = co_await db.query("DELETE FROM action_category WHERE id = ?", &params);
    if (res.has_value()) {
        co_return true;
    }
    co_return false;
}

nextapp::pb::ActionCategory *ActionCategoriesModel::lookup(const QString &id)
{
    auto it = ranges::find_if(action_categories_, [&id](const nextapp::pb::ActionCategory& c) {
        return c.id_proto() == id;
    });

    if (it == action_categories_.end()) {
        return nullptr;
    }

    return &*it;
}
