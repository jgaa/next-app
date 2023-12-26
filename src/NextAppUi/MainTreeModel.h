#pragma once

#include <QObject>
#include <QMap>
#include <QUuid>
#include <QAbstractItemModel>

#include "nextapp.qpb.h"

class MainTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    class TreeNode {
    public:
        // In QT 6.6, QList<> does not work with std::unique_ptr
        using node_list_t = QList<std::shared_ptr<TreeNode>>;

        TreeNode(::nextapp::pb::Node node, TreeNode *parent = {})
            : uuid_{node.uuid()}, node_{std::move(node)}, parent_{parent} {}

        auto& children() noexcept {
            return children_;
        }

        auto& node() noexcept {
            return node_;
        }

        bool hasParent() const noexcept {
            return parent_ != nullptr;
        }

        auto parent() noexcept {
            return parent_;
        }

        QVariant data(int role);

        [[nodiscard]] const QUuid& uuid() const noexcept {
            return uuid_;
        }

        static constexpr int columnCount() noexcept {
            return 1;
        }

    private:
        QUuid uuid_;
        ::nextapp::pb::Node node_;
        node_list_t children_;
        TreeNode *parent_{};
    };

    explicit MainTreeModel(QObject *parent = nullptr);

signals:

public:
    QModelIndex index(int row, int column, const QModelIndex &parent) const;
    QModelIndex parent(const QModelIndex &child) const;
    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    [[nodiscard]] bool hasChildren(const QModelIndex &parent) const;

public slots:    
    // Replaces the node if it exists, adds it if it don't exist
    // If sibling is set, the node is added/moved before the sibling.
    // If parent is set, the node is added/moved as last child.
    // If both parent ans sibling is set, the node is added/moved before the sibling. In other words, a sibling value of {} is similar to an end() iterator
    // If neither sibling or parent is set, the node is added/moved as the last node at the root level.
    void addNode(const ::nextapp::pb::Node& node, const std::optional<QUuid>& parent, const std::optional<QUuid>& beforeSibling);

    // As addNode, but only for move.
    void moveNode(const QUuid& node, const std::optional<QUuid>& parent, const std::optional<QUuid>& beforeSibling);

    void deleteNode(const QUuid& uuid);

    // Deletes any existing nodes and copys the tree from 'tree'
    void setAllNodes(const nextapp::pb::NodeTree& tree);

    void clear();

private:
    TreeNode::node_list_t& getListFromChild(MainTreeModel::TreeNode& child) const;


    TreeNode::node_list_t root_;
    QMap<QUuid, TreeNode*> uuid_index_;
};
