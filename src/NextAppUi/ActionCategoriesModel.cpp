#include <algorithm>

#include <QProtobufSerializer>

#include "ActionCategoriesModel.h"
#include <ReviewModel.h>
#include "ServerCommAccess.h"
#include "util.h"

using namespace std;

namespace {

bool compare(const nextapp::pb::ActionCategory& a, const nextapp::pb::ActionCategory& b) {
    return a.name() < b.name();
}

} // anon ns

ActionCategoriesModel* ActionCategoriesModel::instance_;

ActionCategoriesModel::ActionCategoriesModel(QObject *parent)
    : ActionCategoriesModel(*NextAppCore::instance(), parent)
{
}

ActionCategoriesModel::ActionCategoriesModel(RuntimeServices& runtime, QObject *parent)
    : QAbstractListModel(parent)
    , runtime_{runtime}
{
    assert(!instance_);
    instance_ = this;
    LOG_TRACE_N << "ActionCategoriesModel created";

    none_ = tr("-- None --");

    connect(&runtime_.serverComm(), &ServerCommAccess::onUpdate, this,
            [this](const std::shared_ptr<nextapp::pb::Update>& update) {
                onUpdate(update).then(
                    [] {},
                    [](const std::exception &e) {
                        LOG_ERROR_N << "Failed to apply action category update: " << e.what();
                    });
            });

    if (auto *core = dynamic_cast<NextAppCore*>(&runtime_)) {
        connect(core, &NextAppCore::onlineChanged, this, [this] (bool online) {
            onOnlineChanged(online);
        });
    }

    // if (ServerComm::instance().connected()) {
    //     setOnline(true);
    // }
}

void ActionCategoriesModel::deleteCategory(const QString &id)
{
    if (online_) {
        runtime_.serverComm().deleteActionCategory(id, [this] (auto val) {
            if (std::holds_alternative<ServerCommAccess::CbError>(val)) {
                LOG_WARN_N << "Failed to delete category: " << std::get<ServerCommAccess::CbError>(val).message;
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
        runtime_.serverComm().createActionCategory(category, [this] (auto val) {
            if (std::holds_alternative<ServerCommAccess::CbError>(val)) {
                LOG_WARN_N << "Failed to create category: " << std::get<ServerCommAccess::CbError>(val).message;
            }
        });
    }
}

void ActionCategoriesModel::updateCategory(const nextapp::pb::ActionCategory &category)
{
    if (online_) {
        runtime_.serverComm().updateActionCategory(category, [this] (auto val) {
            if (std::holds_alternative<ServerCommAccess::CbError>(val)) {
                LOG_WARN_N << "Failed to update category: " << std::get<ServerCommAccess::CbError>(val).message;
            }
        });
    }
}

nextapp::pb::ActionCategory ActionCategoriesModel::get(int index)
{
    if (index == action_categories_.size()) {
        nextapp::pb::ActionCategory cat;
        cat.setName(none_);
        cat.setColor("transparent");
        return cat;
    }

    if (index < 0 || index >= action_categories_.size()) {
        nextapp::pb::ActionCategory cat;
        cat.setColor("deepskyblue");
        return cat;
    }

    return action_categories_.at(index);
}

QString ActionCategoriesModel::getName(const QString &id)
{
    if (id.isEmpty()) {
        return none_; // -- None --
    }

    if (const auto* cat = lookup(id)) {
        return cat->name();
    }

    return {};
}

int ActionCategoriesModel::getIndexByUuid(const QString &id)
{
    if (id.isEmpty()) {
        return action_categories_.size(); // -- None --
    }

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
    if (id.isEmpty()) {
        return "transparent"; // -- None --
    }

    auto it = ranges::find_if(action_categories_, [&id](const nextapp::pb::ActionCategory& c) {
        return c.id_proto() == id;
    });

    if (it == action_categories_.end()) [[unlikely]] {
        return "transparent";
    }

    return it->color();
}

bool ActionCategoriesModel::isValid(int row)
{
    return row >= 0 && row < action_categories_.size();
}

const nextapp::pb::ActionCategory &ActionCategoriesModel::getFromUuid(const QString &uuid)
{
    static const nextapp::pb::ActionCategory empty;
    if (uuid.isEmpty()) {
        static nextapp::pb::ActionCategory none;
        none.setName(none_);
        return none;
    }

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
    return action_categories_.size() + 1;
}

QVariant ActionCategoriesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    if (index.row() == action_categories_.size()) {
        switch (role) {
        case NameRole:
            return none_;
        case ValidRole:
            return false;
        default:
            return {};
        }
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
    case ValidRole:
        return true;
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
        {ValidRole, "valid"} // -- None -- is selectable but not valid
    };
}

ActionCategoriesModel &ActionCategoriesModel::instance()
{
    assert(instance_);
    return *instance_;
}

QCoro::Task<bool> ActionCategoriesModel::synch(bool fullSync)
{
    setValid(false);

    // See if we need to fetch from server
    const auto server_ver = runtime_.serverComm().getServerDataVersions().actionCategoryVersion();
    const auto local_ver = co_await loadLocalVersionFromDb();
    runtime_.serverComm().setLocalActionCategoryVersion(local_ver);

    const bool need_sync = fullSync || server_ver == 0 || server_ver > local_ver;
    if (need_sync) {
        if (!co_await synchFromServer()) {
            co_return false;
        }
    }

    if (load_after_sync_) {
        if (!co_await loadFromDb()) {
            co_return false;
        }
    }

    if (need_sync) {
        if (!co_await storeLocalVersionInDb(server_ver)) {
            co_return false;
        }
        runtime_.serverComm().setLocalActionCategoryVersion(server_ver);
    }

    co_return true;
}

QCoro::Task<void> ActionCategoriesModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    if (!update->hasActionCategory()) {
        co_return;
    }

    if (!valid_) {
        pending_updates_.push_back(update);
        co_return;
    }

    co_await applyUpdate(update);
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
    auto res = co_await runtime_.serverComm().getActionCategories({});
    if (res.error() == nextapp::pb::ErrorGadget::Error::OK) {
        if (res.hasActionCategories()) {
            co_return co_await replaceAllFromServer(res.actionCategories());
        }

        co_return true;
    }

    co_return false;
}

QCoro::Task<bool> ActionCategoriesModel::loadFromDb()
{
    auto& db = dbStore();
    auto res = co_await db.legacyQuery("SELECT data FROM action_category");
    if (res.has_value()) {
        QList<nextapp::pb::ActionCategory> action_categories;
        for(const auto& row : res.value()) {
            const auto& data = row.at(0).toByteArray();
            nextapp::pb::ActionCategory cat;
            QProtobufSerializer serializer;
            cat.deserialize(&serializer, data);
            action_categories.push_back(cat);
        }

        beginResetModel();
        action_categories_ = std::move(action_categories);
        ranges::sort(action_categories_, compare);
        endResetModel();

        co_await applyPendingUpdates();
        setValid(true);
        co_return true;
    }

    co_return false;
}

QCoro::Task<void> ActionCategoriesModel::applyUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    // We need a copy since update likely goes out of scope before we finish.
    const auto cat = update->actionCategory();

    auto it = std::find_if(action_categories_.begin(), action_categories_.end(),
                           [&cat](const nextapp::pb::ActionCategory& c) {
        return c.id_proto() == cat.id_proto();
    });

    if (update->op() == nextapp::pb::Update::Operation::DELETED) {
        if (!co_await remove(cat.id_proto())) {
            LOG_WARN_N << "Failed to remove category from local cache: " << cat.id_proto();
            co_return;
        }

        if (it != action_categories_.end()) {
            action_categories_.erase(it);
            beginResetModel();
            endResetModel();
        }
        co_return;
    }

    if (!co_await save(cat)) {
        LOG_WARN_N << "Failed to persist category in local cache: " << cat.id_proto();
        co_return;
    }

    if (it != action_categories_.end()) {
        *it = cat;
    } else {
        action_categories_.push_back(cat);
    }

    beginResetModel();
    ranges::sort(action_categories_, compare);
    endResetModel();
}

QCoro::Task<void> ActionCategoriesModel::applyPendingUpdates()
{
    while (!pending_updates_.empty()) {
        auto pending = std::move(pending_updates_);
        pending_updates_.clear();

        for (const auto& update : pending) {
            co_await applyUpdate(update);
        }
    }
}

QCoro::Task<uint64_t> ActionCategoriesModel::loadLocalVersionFromDb()
{
    auto& db = dbStore();
    const auto version = co_await db.queryOne<qulonglong>(
        "SELECT value FROM sync_state WHERE key = 'action_category_version'");
    co_return version.value_or(0);
}

QCoro::Task<bool> ActionCategoriesModel::storeLocalVersionInDb(uint64_t version)
{
    auto& db = dbStore();
    QList<QVariant> params;
    params << QStringLiteral("action_category_version");
    params << static_cast<qulonglong>(version);

    const auto res = co_await db.legacyQuery(R"(INSERT INTO sync_state (key, value)
        VALUES (?, ?)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value)", &params);
    co_return res.has_value();
}

QCoro::Task<bool> ActionCategoriesModel::replaceAllFromServer(const nextapp::pb::ActionCategories& categories)
{
    auto& db = dbStore();
    const bool using_outer_transaction = sync_db_override_ && !load_after_sync_;

    if (!using_outer_transaction && !co_await db.legacyQuery("BEGIN IMMEDIATE")) {
        LOG_WARN_N << "Failed to start action-category refresh transaction.";
        co_return false;
    }

    const auto rollback = [&]() -> QCoro::Task<bool> {
        if (!using_outer_transaction) {
            (void) co_await db.legacyQuery("ROLLBACK");
        }
        co_return false;
    };

    if (!co_await db.legacyQuery("DELETE FROM action_category")) {
        LOG_WARN_N << "Failed to clear local action categories before refresh.";
        co_return co_await rollback();
    }

    for (const auto& cat : categories.categories()) {
        if (!co_await save(cat)) {
            LOG_WARN_N << "Failed to persist refreshed action category " << cat.id_proto();
            co_return co_await rollback();
        }
    }

    if (!using_outer_transaction && !co_await db.legacyQuery("COMMIT")) {
        LOG_WARN_N << "Failed to commit action-category refresh transaction.";
        co_return co_await rollback();
    }

    co_return true;
}

QCoro::Task<bool> ActionCategoriesModel::save(const nextapp::pb::ActionCategory &category)
{
    auto& db = dbStore();
    QProtobufSerializer serializer;
    QList<QVariant> params;

    params << category.id_proto();
    params << static_cast<uint>(category.version());
    params << category.name();
    params << category.serialize(&serializer);

    const auto res = co_await db.legacyQuery(R"(INSERT INTO action_category (id, version, name, data)
        VALUES (?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            version = excluded.version,
            name = excluded.name,
            data = excluded.data)", &params);

    if (res.has_value()) {
        co_return true;
    }

    co_return false;
}

QCoro::Task<bool> ActionCategoriesModel::remove(const QString &id)
{
    auto& db = dbStore();
    DbStore::param_t params;
    params.append(id);
    const auto res = co_await db.legacyQuery("DELETE FROM action_category WHERE id = ?", &params);
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
