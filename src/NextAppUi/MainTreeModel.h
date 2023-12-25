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
            : node_{std::move(node)}, parent_{parent} {}

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

        [[nodiscard]] QUuid uuid() const noexcept {
            return QUuid{node_.uuid()};
        }

        static constexpr int columnCount() noexcept {
            return 1;
        }

    private:
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
    // If parent is set, the node is added/moved as last child.
    // If sibling is set, the node is added/moved before the sibling.
    // If neither sibling or parent is set, the node is added/moved as the firstnode at the root level.
    void addNode(::nextapp::pb::Node node, const std::optional<QUuid>& parent, const std::optional<QUuid>& beforeSibling);

    // As addNode, but only for move.
    void moveNode(const QUuid& node, const std::optional<QUuid>& parent, const std::optional<QUuid>& beforeSibling);

    void deleteNode(const QUuid& uuid);

    // Deletes any existing nodes and copys the tree from 'tree'
    void setAllNodes(nextapp::pb::NodeTree tree);

    void clear();

private:
    TreeNode::node_list_t& getListFromChild(MainTreeModel::TreeNode& child) const;

    void addNode_(::nextapp::pb::Node node, const std::optional<QUuid>& parent, const std::optional<QUuid>& beforeSibling);

    // As addNode, but only for move.
    void moveNode_(const QUuid& node, const std::optional<QUuid>& parent, const std::optional<QUuid>& beforeSibling);

    void deleteNode_(const QUuid& uuid);

    // Deletes any existing nodes and copys the tree from 'tree'
    void setAllNodes_(nextapp::pb::NodeTree tree);

    TreeNode::node_list_t root_;
    QMap<QUuid, TreeNode*> uuid_index_;
};
