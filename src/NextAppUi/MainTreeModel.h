#pragma once

#include <QObject>
#include <QMap>
#include <QHash>
#include <QUuid>
#include <QAbstractItemModel>

#include "nextapp.qpb.h"

class MainTreeModel : public QAbstractItemModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    //Q_PROPERTY(string name READ name NOTIFY nameChanged)

public:
    struct ResetScope {
    public:
        ResetScope(MainTreeModel& model);

        ~ResetScope();

    private:
        MainTreeModel& model_;
    };

    class TreeNode {
    public:
        enum Roles {
            NameRole = Qt::UserRole + 1,
            UuidRole
        };

        // In QT 6.6, QList<> does not work with std::unique_ptr
        using node_list_t = QList<std::shared_ptr<TreeNode>>;

        TreeNode() = default;
        TreeNode(::nextapp::pb::Node node, TreeNode *parent = {});

        auto& children() noexcept {
            return children_;
        }

        const auto& children() const noexcept {
            return children_;
        }

        auto& node() noexcept {
            return node_;
        }

        const auto& node() const noexcept {
            return node_;
        }

        bool hasParent() const noexcept {
            return parent_ != nullptr;
        }

        auto parent() noexcept {
            return parent_;
        }

        const auto parent() const noexcept {
            return parent_;
        }

        QVariant data(int role);

        [[nodiscard]] const QUuid& uuid() const noexcept {
            return uuid_;
        }

        static constexpr int columnCount() noexcept {
            return 1;
        }

        static QHash<int, QByteArray> roleNames();

    private:
        QUuid uuid_;
        ::nextapp::pb::Node node_;
        node_list_t children_;
        TreeNode *parent_{};
    };

    explicit MainTreeModel(QObject *parent = nullptr);

signals:

public:
    QString nodeName(const QModelIndex& index) const;
    Q_INVOKABLE nextapp::pb::Node *node(const QModelIndex& ix);
    Q_INVOKABLE nextapp::pb::Node *emptyNode() {
        return node({});
    }

    Q_INVOKABLE QModelIndex *emptyQModelIndex() {
        return new QModelIndex{};
    }

    // Don't work. Node * is always empty
    //Q_INVOKABLE void addNode(const nextapp::pb::Node *, QString parentUuid, QString currentUuid);

    // The UI's interface to add a new node in the tree
    Q_INVOKABLE void addNode(const QVariantMap args);

    QModelIndex index(int row, int column, const QModelIndex &parent) const;
    QModelIndex parent(const QModelIndex &child) const;
    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    [[nodiscard]] bool hasChildren(const QModelIndex &parent) const;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const {
        return TreeNode::roleNames();
    }

    // Print the tree to the log
    void dump();

public slots:    
    // Replaces the node if it exists, adds it if it don't exist
    // If sibling is set, the node is added/moved before the sibling.
    // If parent is set, the node is added/moved as last child.
    // If both parent ans sibling is set, the node is added/moved before the sibling. In other words, a sibling value of {} is similar to an end() iterator
    // If neither sibling or parent is set, the node is added/moved as the last child of the root-node.
    void addNode(const ::nextapp::pb::Node& node, const std::optional<QUuid>& parent, const std::optional<QUuid>& beforeSibling);

    // As addNode, but only for move.
    void moveNode(const QUuid& node, const std::optional<QUuid>& parent, const std::optional<QUuid>& beforeSibling);

    void deleteNode(const QUuid& uuid);

    // Deletes any existing nodes and copys the tree from 'tree'
    void setAllNodes(const nextapp::pb::NodeTree& tree);

    void clear();

    std::unique_ptr<ResetScope> resetScope() {
        return std::make_unique<ResetScope>(*this);
    }

private:
    TreeNode::node_list_t& getListFromChild(MainTreeModel::TreeNode& child);


    TreeNode root_;
    QMap<QUuid, TreeNode*> uuid_index_;
};
