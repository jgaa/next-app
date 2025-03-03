
#include <algorithm>
#include <ranges>

#include <QProtobufSerializer>

#include "MainTreeModel.h"
#include "ServerComm.h"
#include "NextAppCore.h"
#include "DbStore.h"
#include "logging.h"
#include "util.h"


using namespace std;
using namespace nextapp;

MainTreeModel *MainTreeModel::instance_;

ostream& operator << (ostream& o, const QModelIndex& v) {

    if (v.isValid()) {
        if (const auto *ptr = static_cast<MainTreeModel::TreeNode *>(v.internalPointer())) {

            o << "QModelIndex{" << v.row() << ", " << v.column() << ", ";
            std::vector<const MainTreeModel::TreeNode *> path;
            for (const auto * p = ptr; p; p = p->parent()) {
                path.push_back(p);
            }

            for(const auto *p : std::ranges::reverse_view(path)) {
                o << '/' << p->node().name();
            }

            o << '}';
        }
    } else {
        o << "QModelIndex{}";
    }

    return o;
}

namespace {

static const QString insert_query = R"(INSERT INTO node
        (uuid, parent, name, active, updated, exclude_from_wr, data) VALUES (?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(uuid) DO UPDATE SET
        parent=excluded.parent,
        name=excluded.name,
        active=excluded.active,
        updated=excluded.updated,
        exclude_from_wr=excluded.exclude_from_wr,
        data=excluded.data)";

void dumpLevel(unsigned level, MainTreeModel::TreeNode::node_list_t list) {
    for(auto &node : list) {
        QString name;
        for(auto i = 0; i < level; ++i) {
            name += "   ";
        }
        name += " --> ";
        name += node->node().name();

        LOG_DEBUG << name;
        dumpLevel(level + 1, node->children());
    }
}

optional<unsigned> getRow(MainTreeModel::TreeNode::node_list_t& list, const QUuid& uuid) {
    if (auto it = std::find_if(list.cbegin(), list.cend(), [&](const auto& v) {
            const auto node_uuid = v->uuid();
            return node_uuid == uuid;
        }); it != list.cend()) {

        return distance(list.cbegin(), it);
    }

    return {};
}

MainTreeModel::TreeNode * getTreeNode(const QModelIndex& node) noexcept {
    if (node.isValid()) {
        auto ptr = static_cast<MainTreeModel::TreeNode *>(node.internalPointer());
        assert(ptr);
        return ptr;
    }
    return {};
}

template <typename T, typename ixT>
void copyTreeBranch(MainTreeModel::TreeNode::node_list_t& list, const T& from, ixT& index, MainTreeModel::TreeNode *parent) {
    assert(parent);
    for(auto& node: from) {
        const auto name = node.node().name();
        auto new_node = make_shared<MainTreeModel::TreeNode>(node.node(), parent);
        index[new_node->uuid()] = new_node.get();
        list.emplace_back(std::move(new_node));

        // Add children recursively
        auto& current = list.back();
        copyTreeBranch(current->children(), node.children(), index, current.get());
    }

    std::ranges::sort(list, [](const auto& left, const auto& right) {
        return left->node().name().compare(right->node().name(), Qt::CaseInsensitive) < 0;
    });
}

} // anon ns


MainTreeModel::MainTreeModel(QObject *parent)
    : QAbstractItemModel{parent}
{
    instance_ = this;

    connect(std::addressof(ServerComm::instance()), &ServerComm::onUpdate,
            this, [this] (const std::shared_ptr<nextapp::pb::Update>& update) {
                onUpdate(update);
            });

    connect(std::addressof(ServerComm::instance()), &ServerComm::connectedChanged,
            this, &MainTreeModel::onOnline);

    // if (ServerComm::instance().connected()) {
    //     onOnline();
    // }
}

void MainTreeModel::setSelected(const QString& newSel)
{
    selected_ = toValidQuid(newSel);
    emit selectedChanged();
}

QString MainTreeModel::selected() const
{
    return selected_;
}

QModelIndex MainTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (state() != State::VALID) {
        return {};
    }
    if (column) {
        // LOG_TRACE_N << "Queried for row=" << row << ", column=" << column
        //             << ". We don't use culumns in this tree.";
        return {};
    }

    //LOG_TRACE << "index: row=" << row << ", column=" << column << ", parent=" << parent;

    if (parent.isValid()) {
        if (auto *parent_ptr = static_cast<TreeNode *>(parent.internalPointer())) {
            if (parent_ptr->children().size() > row) {
                auto *current = parent_ptr->children().at(row).get();
                return createIndex(row, column, current);
            }
        }
    } else {
        if (row == 0 && column == 0) {
            return createIndex(row, column, &root_);
        }
    }

    //LOG_TRACE << "index: ** returning empty...";
    return {};
}

QModelIndex MainTreeModel::parent(const QModelIndex &child) const
{
    if (state() != State::VALID) {
        return {};
    }
    //LOG_TRACE << "parent for : " << child;

    if (auto *current = getTreeNode(child)) {
        if (auto *parent = current->parent()) {
            if (parent == &root_) {
                return createIndex(0, 0, &root_);
            }

            auto& list = const_cast<MainTreeModel *>(this)->getListFromChild(*parent);
            if (auto row = getRow(list, parent->uuid())) {
                return createIndex(*row, 0, parent);
            }

            auto name = current->node().name();
            if (!name.isEmpty()) {
                LOG_WARN << "Could not find child " << child << " in childlist from parent '" << parent->node().name() << '\'';
            }
        }
    }

    //LOG_TRACE << "parent *** Returning empty...";
    return {};
}

int MainTreeModel::rowCount(const QModelIndex &parent) const
{
    if (state() != State::VALID) {
        return 0;
    }
    auto count = 1;
    if (parent.isValid()) {
        count = getTreeNode(parent)->children().size();
    }

    //LOG_TRACE_N << parent << " count=" << count;

    return count;
}

int MainTreeModel::columnCount(const QModelIndex &parent) const
{
    return TreeNode::columnCount();
}

QVariant MainTreeModel::data(const QModelIndex &index, int role) const
{
    //LOG_TRACE_N << index << " role=" << role;
    if (index.isValid()) {
        if (auto current = getTreeNode(index)) {
            return current->data(role);
        }
    }

    return {};
}

bool MainTreeModel::hasChildren(const QModelIndex &parent) const
{
    bool children = true; // The empty QModelIndex always have one child, the root node.
    if (parent.isValid()) {
        children = !getTreeNode(parent)->children().empty();
    }

    //LOG_TRACE_N << parent << " children=" << (children ? "true" : "false");
    return children;
}

void MainTreeModel::dump()
{
    LOG_DEBUG << "Dumping the tree.";
    LOG_DEBUG << "Root";
    dumpLevel(1, root_.children());
}

void MainTreeModel::clear()
{
    root_.children().clear();
    uuid_index_.clear();
}

QModelIndex MainTreeModel::useRoot()
{
    if (root_.children().empty()) {
        return index(0, 0, {});
    }

    return getIndex(root_.children().front().get());
}

pb::Node MainTreeModel::toNode(const QVariantMap &map)
{
    pb::Node n;

    n.setUuid(toValidQuid(map.value("uuid").toString()));
    n.setName(map.value("name").toString());
    n.setParent(toValidQuid(map.value("parent").toString()));
    n.setUser(toValidQuid(map.value("user").toString()));
    n.setActive(map.value("active").toBool());
    n.setKind(toNodeKind(map.value("kind").toString()));
    n.setDescr(map.value("descr").toString());
    n.setVersion(map.value("version").toLongLong());
    n.setExcludeFromWeeklyReview(map.value("excludeFromWeeklyReview").toBool());
    n.setCategory(map.value("category").toString());

    return n;
}

QVariantMap MainTreeModel::toMap(const nextapp::pb::Node &node)
{
    QVariantMap vm;

    vm["uuid"] = node.uuid();
    vm["name"] = node.name();
    vm["parent"] = node.parent();
    vm["active"] = node.active();
    vm["kind"] = toString(node.kind());
    vm["descr"] = node.descr();
    vm["version"] = static_cast<qint64>(node.version());
    vm["excludeFromWeeklyReview"] = node.excludeFromWeeklyReview();
    vm["category"] = node.category();

    return vm;
}

pb::Node::Kind MainTreeModel::toNodeKind(const QString &name)
{
    if (name == "folder") return nextapp::pb::Node::Kind::FOLDER;
    if (name == "organization") return nextapp::pb::Node::Kind::ORGANIZATION;
    if (name == "person") return nextapp::pb::Node::Kind::PERSON;
    if (name == "project") return nextapp::pb::Node::Kind::PROJECT;
    if (name == "task") return nextapp::pb::Node::Kind::TASK;
    assert(false);
    return nextapp::pb::Node::Kind::FOLDER;
}

QString MainTreeModel::toString(const nextapp::pb::Node::Kind &kind)
{
    using kind_t = nextapp::pb::Node::Kind;
    switch(kind) {
    case kind_t::FOLDER:
        return "folder";
    case kind_t::ORGANIZATION:
        return "organization";
    case kind_t::PERSON:
        return "person";
    case kind_t::PROJECT:
        return "project";
    case kind_t::TASK:
        return "task";
    }

    return {};
}

void MainTreeModel::addNode(TreeNode *parent, const nextapp::pb::Node &node)
{
    const int row = getInsertRow(parent, node);
    if (!suspend_model_notifications_) {
        beginInsertRows(getIndex(parent), row, row);
    }
    auto new_node = make_shared<TreeNode>(node, parent);
    uuid_index_[new_node->uuid()] = new_node.get();
    insertNode(parent->children(), new_node, row);
    if (!suspend_model_notifications_) {
        endInsertRows();
    }

    if (parent == &root_ && row == 0 && !suspend_model_notifications_) {
        emit useRootChanged();
    }
}

void MainTreeModel::moveNode(TreeNode *parent, TreeNode *current, const nextapp::pb::Node &node)
{
    // `parent` is the new parent we are moving to
    // We still have the actual/old parent in `current->parent`

    auto old_parent = lookupTreeNode(current->parent()->uuid());
    assert(old_parent);

    const auto sourceParentIx = getIndex(old_parent);
    const auto destinationParentIx = getIndex(parent);
    const auto src_row = getIndex(current).row();
    const auto dst_row = getInsertRow(parent, node);
    beginMoveRows(sourceParentIx, src_row, src_row, destinationParentIx, dst_row);

    auto tn = old_parent->children().takeAt(src_row);
    tn->node() = node; // We want version and parent to be up to date
    tn->setParent(parent);
    insertNode(parent->children(), tn, dst_row);
    endMoveRows();
}

void MainTreeModel::insertNode(TreeNode::node_list_t &list, std::shared_ptr<TreeNode> &tn, int row)
{
    if (row >= list.size()) {
        list.append(tn);
    } else {
        list.insert(row, tn);
    }
}

QModelIndex MainTreeModel::getIndex(TreeNode *node)
{
    assert(node);

    if (node == &root_) {
        return createIndex(0, 0, &root_);
    }

    auto& list = const_cast<MainTreeModel *>(this)->getListFromChild(*node);
    const auto row = getRow(list, node->uuid());
    if (row) {
        return createIndex(*row, 0, node);
    }

    assert(false);
    return {};
}

// We insert in sorted order.
int MainTreeModel::getInsertRow(const TreeNode *parent, const nextapp::pb::Node &node)
{
    assert(parent);
    int row = 0;
    for(const auto& n : parent->children()) {
        if (n->node().name().compare(node.name(), Qt::CaseInsensitive) > 0) {
            return row;
        }
        ++row;
    }

    return row;
}

// We are a coroutine and may outlive the caller. So we use a smart pointer for the update,
// not just a reference to the data.
QCoro::Task<void> MainTreeModel::pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update)
{
    using nextapp::pb::Update;
    using nextapp::pb::Node;


    assert(update->hasNode());
    const Node& node = update->node();

    TreeNode* parent = &root_;
    if (!node.parent().isEmpty()) {
        parent = lookupTreeNode(QUuid{node.parent()});
    }
    assert(!node.uuid().isEmpty());

    TreeNode* current = lookupTreeNode(QUuid{node.uuid()});

    const auto op = update->op();

    if (op != Update::Operation::DELETED) {
        if (current && current->node().version() > node.version()) {
            LOG_DEBUG << "Received updated/added/moved node " << node.uuid() <<" with version less than the existing node. Ignoring.";
            co_return;
        }
    }

    switch(op) {
    case Update::Operation::ADDED:
added:
        assert(current == nullptr);
        assert(parent);
        co_await save(node);
        addNode(parent, node);
        break;
    case Update::Operation::UPDATED:
        assert(current);
        {
            auto cix = getIndex(current);
            current->node() = node;
            co_await save(node);
            emit dataChanged(cix, cix);
        }
        break;
    case Update::Operation::MOVED:
        if (current) {
            assert(parent);
            assert(parent != current->parent());
            moveNode(parent, current, node);
            co_await save(parent->node());
            co_await save(node);
        } else {
            // Add it!
            LOG_WARN << "Failed to locate moved node " << current->node().uuid() << ". Will add it.";
            goto added;
        }
        break;
    case Update::Operation::DELETED:
        if (current) {
            if (current == &root_) {
                LOG_WARN << "Cannot delete root node!";
                co_return;
            }
            assert(parent);
            auto cix = getIndex(current);
            auto parent_ix = getIndex(parent);

            if (auto *sel = lookupTreeNode(QUuid{selected()})) {
                if (sel == current || isDescent(sel->uuid(), current->uuid())) {
                    LOG_TRACE << "Clearing selection in tree due to deleted node.";
                    setSelected({});
                }
            }

            co_await save(node);
            co_await doLoadLocally();
            emit nodeDeleted();
        }
        break;
    }
}

MainTreeModel::TreeNode *MainTreeModel::lookupTreeNode(const QUuid &uuid, bool emptyIsRoot)
{
    if (uuid.isNull()) {
        if (emptyIsRoot) {
            return &root_;
        }
        return {};
    }

    if (auto it = uuid_index_.find(uuid); it != uuid_index_.end()) {
        return it.value();
    }

    return {};
}

bool MainTreeModel::isDescent(const QUuid &nodeUuid, const QUuid &descentOf)
{
    if (auto node = lookupTreeNode(nodeUuid)) {
        for(auto *parent = node->parent(); parent; parent = parent->parent()) {
            if (parent->uuid() == descentOf) {
                return true;
            }
        }
    }

    return false;
}

QCoro::Task<void> MainTreeModel::onOnline()
{
    if (ServerComm::instance().status() != ServerComm::Status::ONLINE) {
        setState(State::LOCAL);
    }
    co_return;
}

QCoro::Task<bool> MainTreeModel::loadFromCache()
{
    auto& db = NextAppCore::instance()->db();

    QString query = R"(WITH RECURSIVE node_tree AS (
        -- Select the root nodes (those without a parent)
        SELECT uuid, parent, active, updated, data
        FROM node
        WHERE parent IS NULL

        UNION ALL

        -- Recursively select the child nodes
        SELECT n.uuid, n.parent, n.active, n.updated, n.data
        FROM node n
        INNER JOIN node_tree nt ON n.parent = nt.uuid
    )
    SELECT uuid, data FROM node_tree)";

    enum Cols {
        UUID = 0,
        DATA
    };

    auto rval = co_await db.legacyQuery(query);
    if (rval) {
        for (const auto& row : rval.value()) {
            QProtobufSerializer serializer;
            nextapp::pb::Node node;
            const auto& data = row.at(DATA).toByteArray();
            node.deserialize(&serializer, data);
            assert(node.uuid() == row.at(UUID).toString());

            // The query is supposed to return parents first.
            auto parent = lookupTreeNode(QUuid{node.parent()});
            assert(parent);
            if (parent) {
                addNode(parent, node);
            } else {
                LOG_WARN_N << "Missing parent for tree node: " << node.uuid() << " " << node.name();
            }
        }
    }

    co_return true;
}

std::shared_ptr<GrpcIncomingStream> MainTreeModel::openServerStream(nextapp::pb::GetNewReq req) {
    return ServerComm::instance().synchNodes(req);
}

QCoro::Task<bool> MainTreeModel::doSynch(bool fullSync)
{
    beginResetModel();
    endResetModel();
    suspend_model_notifications_ = true;
    ScopedExit guard{[this] {
        suspend_model_notifications_ = false;
        if (state() == State::VALID) {
            beginResetModel();
            endResetModel();
        }
    }};

    co_return co_await synch(fullSync);
}

QCoro::Task<bool> MainTreeModel::doLoadLocally()
{
    beginResetModel();
    endResetModel();
    suspend_model_notifications_ = true;
    ScopedExit guard{[this] {
        suspend_model_notifications_ = false;
        if (state() == State::VALID) {
            beginResetModel();
            endResetModel();
        }
    }};

    co_return co_await loadLocally();
}

QCoro::Task<bool> MainTreeModel::save(const QProtobufMessage& item)
{
    const auto& node = static_cast<const nextapp::pb::Node&>(item);

    auto& db = NextAppCore::instance()->db();
    QList<QVariant> params;

    if (node.deleted()) {
        QString sql = R"(DELETE FROM node WHERE uuid = ?)";
        params.append(node.uuid());
        LOG_TRACE_N << "Deleting node " << node.uuid() << " " << node.name();

        const auto rval = co_await db.legacyQuery(sql, &params);
        if (!rval) {
            LOG_WARN_N << "Failed to delete node " << node.uuid() << " " << node.name()
                        << " err=" << rval.error();
        }
        co_return true; // TODO: Add proper error handling. Probably a full resynch if the node is in the db.
    }

    QProtobufSerializer serializer;

    params << node.uuid();
    params << node.parent();
    params << node.name();
    params << node.active();
    params << qlonglong{node.updated()};
    params << node.excludeFromWeeklyReview();
    params << node.serialize(&serializer);

    const auto rval = co_await db.legacyQuery(insert_query, &params);
    if (!rval) {
        LOG_ERROR_N << "Failed to update node: " << node.uuid() << " " << node.name()
                    << " err=" << rval.error();
        co_return false; // TODO: Add proper error handling. Probably a full resynch.
    }

    co_return true;
}

QCoro::Task<bool> MainTreeModel::saveBatch(const QList<nextapp::pb::Node> &items)
{
    auto& db = NextAppCore::instance()->db();
    static const QString delete_query = R"(DELETE FROM node WHERE uuid = ?)";
    auto getParams = [](const nextapp::pb::Node& node) {
        QList<QVariant> params;
        QProtobufSerializer serializer;
        params << node.uuid();
        params << node.parent();
        params << node.name();
        params << node.active();
        params << qlonglong{node.updated()};
        params << node.excludeFromWeeklyReview();
        params << node.serialize(&serializer);
        return params;
    };
    auto isDeleted = [](const nextapp::pb::Node& node) { return node.deleted(); };
    auto getId = [](const nextapp::pb::Node& node) { return node.uuid(); };

    co_return co_await db.queryBatch(insert_query, delete_query, items, getParams, isDeleted, getId);
}

MainTreeModel::TreeNode::node_list_t &MainTreeModel::getListFromChild(TreeNode &child)
{

    if (child.hasParent()) {
        return child.parent()->children();
    }

    return root_.children();
}

MainTreeModel::TreeNode::TreeNode(nextapp::pb::Node node, TreeNode *parent)
    : uuid_{node.uuid()}, node_{std::move(node)}, parent_{parent} {

    auto name = node_.name();
    if (!name.isEmpty() && !parent) {
        LOG_WARN << "Impossible!";
        assert(false);
    }
}

QVariant MainTreeModel::TreeNode::data(int role)
{
    switch(role) {
    case Qt::DisplayRole:
    case NameRole:
        return node().name();
    case UuidRole:
        return node().uuid();
    case KindRole:
        return MainTreeModel::toString(node().kind());
    case DescrRole:
        return node().descr();
    case ExcludedFromWeeklyReviewRole:
        return node().excludeFromWeeklyReview();
    }

    return {};
}

QHash<int, QByteArray> MainTreeModel::TreeNode::roleNames() {
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[UuidRole] = "uuid";
    roles[KindRole] = "kind";
    roles[ExcludedFromWeeklyReviewRole] = "excludedFromWeeklyReview";
    return roles;
}

QString MainTreeModel::nodeName(const QModelIndex &ix) const
{
    if (auto current = getTreeNode(ix)) {
        return current->node().name();
    }

    return {};
}

pb::Node *MainTreeModel::node(const QModelIndex &ix)
{
    if (auto current = getTreeNode(ix)) {
        return new pb::Node{current->node()};
    }

    return new pb::Node{};
}

QVariantMap MainTreeModel::nodeMap(const QModelIndex &ix)
{
    if (auto current = getTreeNode(ix)) {
        return toMap(current->node());
    }

    return {};
}

pb::Node *MainTreeModel::nodeFromUuid(const QString &uuid)
{
    if (auto it = uuid_index_.find(QUuid{uuid}); it != uuid_index_.end()) {
        return new pb::Node{it.value()->node()};
    }

    return {};
}

QVariantMap MainTreeModel::nodeMapFromUuid(const QString &uuid)
{
    if (auto it = uuid_index_.find(QUuid{uuid}); it != uuid_index_.end()) {
        return toMap(it.value()->node());
    }

    return {};
}

QModelIndex MainTreeModel::indexFromUuid(const QString &uuid)
{
    if (auto it = uuid_index_.find(QUuid{uuid}); it != uuid_index_.end()) {
        return getIndex(it.value());
    }

    return {};
}

QString MainTreeModel::nodeNameFromQuuid(const QUuid &uuid, bool fullPath)
{
    if (auto it = uuid_index_.find(uuid); it != uuid_index_.end()) {
        if (fullPath) {
            QString name;
            for(auto *p = it.value(); p; p = p->parent()) {
                if (!name.isEmpty()) {
                    name.prepend('/');
                }
                name.prepend(p->node().name());
            }

            return name;
        }

        return it.value()->node().name();
    }

    return {};
}

bool MainTreeModel::isChildOfSelected(const QUuid &uuid)
{
    if (auto *sel = lookupTreeNode(QUuid{selected()}); sel) {
        return isDescent(uuid, sel->uuid());
    }

    return false;
}

void MainTreeModel::addNode(QVariantMap args)
{
    nextapp::pb::Node node = toNode(args);
    assert(!node.name().isEmpty());

    // We will update the UI when we get the update notification
    ServerComm::instance().addNode(node);
}

void MainTreeModel::updateNode(const QVariantMap args)
{
    nextapp::pb::Node node = toNode(args);
    assert(!node.name().isEmpty());
    assert(!node.uuid().isEmpty());

    ServerComm::instance().updateNode(node);
}

QString MainTreeModel::uuidFromModelIndex(const QModelIndex ix)
{
    if (auto current = getTreeNode(ix)) {
        return current->node().uuid();
    }

    return {};
}

void MainTreeModel::deleteNode(const QString &uuid)
{
    QUuid id{uuid};
    assert(!id.isNull());

    ServerComm::instance().deleteNode(id);
}

void MainTreeModel::moveNode(const QString &uuid, const QString &toParentUuid)
{
    QUuid id{uuid};
    QUuid parentId{toParentUuid};
    assert(!id.isNull());

    if (isDescent(parentId, id)) {
        LOG_WARN << "Node cannot be moved to one of its descents";
        return;
    }

    ServerComm::instance().moveNode(id, parentId);
}

bool MainTreeModel::canMove(const QString &uuid, const QString &toParentUuid)
{
    if (uuid == toParentUuid || isDescent(QUuid{toParentUuid}, QUuid{uuid})) {
        return false;
    }

    return true;
}

QString MainTreeModel::getCategoryForNode(const QString &uuid, bool recurse)
{
    if (auto *node = lookupTreeNode(QUuid{uuid})) {
        if (!node->node().category().isEmpty()) {
            return node->node().category();
        }

        if (recurse) {
            for(auto *p = node->parent(); p; p = p->parent()) {
                if (!p->node().category().isEmpty()) {
                    return p->node().category();
                }
            }
        }
    }

    return {};
}

MainTreeModel::ResetScope::ResetScope(MainTreeModel &model)
    : model_{model} {

    model_.beginResetModel();
}

MainTreeModel::ResetScope::~ResetScope() {
    model_.endResetModel();
    emit model_.useRootChanged();
}
