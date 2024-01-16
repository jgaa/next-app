
#include <algorithm>
#include <ranges>
#include "MainTreeModel.h"
#include "logging.h"
#include <stack>

using namespace std;
using namespace nextapp;

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
}

} // anon ns


MainTreeModel::MainTreeModel(QObject *parent)
    : QAbstractItemModel{parent}
{}

QModelIndex MainTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column) {
        LOG_TRACE_N << "Queried for row=" << row << ", column=" << column
                    << ". We don't use culumns in this tree.";
        return {};
    }

    LOG_TRACE << "index: row=" << row << ", column=" << column << ", parent=" << parent;

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

    LOG_TRACE << "index: ** returning empty...";
    return {};
}

QModelIndex MainTreeModel::parent(const QModelIndex &child) const
{
    LOG_TRACE << "parent for : " << child;

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

    LOG_TRACE << "parent *** Returning empty...";
    return {};
}

int MainTreeModel::rowCount(const QModelIndex &parent) const
{
    auto count = 1;
    if (parent.isValid()) {
        count = getTreeNode(parent)->children().size();
    }

    LOG_TRACE_N << parent << " count=" << count;

    return count;
}

int MainTreeModel::columnCount(const QModelIndex &parent) const
{
    return TreeNode::columnCount();
}

QVariant MainTreeModel::data(const QModelIndex &index, int role) const
{
    LOG_TRACE_N << index << " role=" << role;
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

    LOG_TRACE_N << parent << " children=" << (children ? "true" : "false");
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

MainTreeModel::TreeNode::node_list_t &MainTreeModel::getListFromChild(TreeNode &child)
{

    if (child.hasParent()) {
        return child.parent()->children();
    }

    return root_.children();
}

void MainTreeModel::addNode(const nextapp::pb::Node& node, const std::optional<QUuid> &parentUuid, const std::optional<QUuid> &beforeSibling)
{
    LOG_TRACE_N << "name=" << node.name();

    auto uuid_str = node.uuid();
    assert(!uuid_str.isEmpty());
    const QUuid node_uuid{uuid_str};

    deleteNode(node_uuid);

    // The node cannot exist in the tree at this point
    assert(uuid_index_.find(node_uuid) == uuid_index_.end());

    if (beforeSibling) {
        auto sibling = uuid_index_.value(*beforeSibling);
        assert(sibling);
        if (sibling) {
            assert(sibling->hasParent());
            auto& list = sibling->parent()->children();
            auto new_node = make_shared<MainTreeModel::TreeNode>(node, sibling->parent());
            uuid_index_[new_node->uuid()] = new_node.get();
            auto siblings_row = getRow(list, *beforeSibling);
            assert(siblings_row);
            list.insert(siblings_row ? *siblings_row : 0, std::move(new_node));
        } else {
            throw runtime_error{"Failed to lookup sibling"};
        }
        return;
    }

    if (parentUuid) {
        auto parent = uuid_index_.value(*parentUuid);
        assert(parent);
        auto new_node = make_shared<MainTreeModel::TreeNode>(std::move(node), parent);

        uuid_index_[new_node->uuid()] = new_node.get();
        auto& list = parent->children();
        list.emplace_back(std::move(new_node));
        return;
    }

    auto new_node = make_shared<MainTreeModel::TreeNode>(std::move(node), &root_);
    uuid_index_[new_node->uuid()] = new_node.get();
    root_.children().emplaceBack(std::move(new_node));
}

void MainTreeModel::moveNode(const QUuid &node,
                             const std::optional<QUuid> &parent,
                             const std::optional<QUuid> &beforeSibling)
{
    assert(false && "Not implemented");
}

void MainTreeModel::deleteNode(const QUuid &uuid)
{
    if (auto current = uuid_index_.value(uuid)) {
        assert(current->uuid() == uuid);
        auto& parent_list = getListFromChild(*current);
        if (auto row = getRow(parent_list, uuid)) {
            parent_list.removeAt(*row);
            uuid_index_.remove(uuid);
        }
    }
}

void MainTreeModel::setAllNodes(const nextapp::pb::NodeTree& tree)
{
    clear();
    copyTreeBranch(root_.children(), tree.nodes(), uuid_index_, &root_);
}

MainTreeModel::TreeNode::TreeNode(nextapp::pb::Node node, TreeNode *parent)
    : uuid_{node.uuid()}, node_{std::move(node)}, parent_{parent} {

    auto name = node_.name();
    if (!name.isEmpty() && !parent) {
        LOG_WARN << "Impossible!";
    }
}

QVariant MainTreeModel::TreeNode::data(int role)
{
    switch(role) {
    case Qt::DisplayRole:
    case NameRole:
        return node().name();
    case UuidRole:
        return uuid();
    }

    return {};
}

QHash<int, QByteArray> MainTreeModel::TreeNode::roleNames() {
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[UuidRole] = "uuid";
    return roles;
}

QString MainTreeModel::nodeName(const QModelIndex &ix) const
{
    if (auto current = getTreeNode(ix)) {
        return current->node().name();
    }

    return {};
}

pb::Node MainTreeModel::node(const QModelIndex &ix)
{
    if (auto current = getTreeNode(ix)) {
        return current->node();
    }

    return {};
}

MainTreeModel::ResetScope::ResetScope(MainTreeModel &model)
    : model_{model} {

    model_.beginResetModel();
}

MainTreeModel::ResetScope::~ResetScope() {
    model_.endResetModel();
}
