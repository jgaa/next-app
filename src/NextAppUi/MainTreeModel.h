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
    Q_PROPERTY(QModelIndex useRoot READ useRoot NOTIFY useRootChanged)
    Q_PROPERTY(QString selected READ selected WRITE setSelected NOTIFY selectedChanged)

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
            UuidRole,
            KindRole,
            DescrRole,
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

        void setParent(TreeNode *parent) {
            parent_ = parent;
            assert(node_.parent() == parent->node().uuid());
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

    void start();

    void setSelected(const QString& selected);
    QString selected() const;

signals:
    void useRootChanged();
    void selectedChanged();

public:
    QString nodeName(const QModelIndex& index) const;
    Q_INVOKABLE nextapp::pb::Node *node(const QModelIndex& ix);
    Q_INVOKABLE QVariantMap nodeMap(const QModelIndex& ix);
    Q_INVOKABLE nextapp::pb::Node *nodeFromUuid(const QString& uuid);
    Q_INVOKABLE QVariantMap nodeMapFromUuid(const QString& uuid);
    Q_INVOKABLE QModelIndex indexFromUuid(const QString& uuid);
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
    Q_INVOKABLE void updateNode(const QVariantMap args);
    Q_INVOKABLE QString uuidFromModelIndex(const QModelIndex ix);
    Q_INVOKABLE void deleteNode(const QString& uuid);
    Q_INVOKABLE void moveNode(const QString& uuid, const QString& toParentUuid);
    Q_INVOKABLE bool canMove(const QString& uuid, const QString& toParentUuid);

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
    // Deletes any existing nodes and copys the tree from 'tree'
    void setAllNodes(const nextapp::pb::NodeTree& tree);

    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);

    void clear();

    std::unique_ptr<ResetScope> resetScope() {
        return std::make_unique<ResetScope>(*this);
    }

    QModelIndex useRoot();

    // I have not been able to use nextapp::pb objects directly in Qml,
    // so let's serialzie via QVariantMap for now.
    static nextapp::pb::Node toNode(const QVariantMap& map);
    static QVariantMap toMap(const nextapp::pb::Node& map);
    static nextapp::pb::Node::Kind toNodeKind(const QString& name);
    static QString toString(const nextapp::pb::Node::Kind& kind);

private:
    void addNode(TreeNode *parent, const nextapp::pb::Node& node);
    void moveNode(TreeNode *parent, TreeNode *current, const nextapp::pb::Node& node);
    void insertNode(TreeNode::node_list_t& list, std::shared_ptr<TreeNode>& tn, int row);
    QModelIndex getIndex(TreeNode *node);
    int getInsertRow(const TreeNode *parent, const nextapp::pb::Node& node);
    void pocessUpdate(const nextapp::pb::Update& update);
    TreeNode *lookupTreeNode(const QUuid& uuid, bool emptyIsRoot = true);
    bool isDescent(const QUuid &uuid, const QUuid &toParentUuid);

    TreeNode::node_list_t& getListFromChild(MainTreeModel::TreeNode& child);
    TreeNode root_;
    QMap<QUuid, TreeNode*> uuid_index_;
    std::vector<std::shared_ptr<nextapp::pb::Update>> pending_updates_;
    bool has_initial_tree_ = false;
    QString selected_;
};
