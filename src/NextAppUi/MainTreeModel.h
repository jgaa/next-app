#pragma once

#include <QObject>
#include <QMap>
#include <QHash>
#include <QUuid>
#include <QAbstractItemModel>

#include "qcorotask.h"
#include "nextapp.qpb.h"

#include "ServerSynchedCahce.h"

// Singleton, handled in main.cpp
class MainTreeModel : public QAbstractItemModel
    , public ServerSynchedCahce<nextapp::pb::Node, MainTreeModel>
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QModelIndex useRoot READ useRoot NOTIFY useRootChanged)

    // Used by multiple qml components
    Q_PROPERTY(QString selected READ selected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY stateChanged)

public:

    Q_ENUM(State)


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

    static MainTreeModel* instance() noexcept {
        return instance_;
    }

    void setSelected(const QString& selected);
    QString selected() const;

    State state() const noexcept {
        return state_;
    }

    // bool valid() const noexcept {
    //     return state_ == State::VALID;
    // }

signals:
    void useRootChanged();
    void selectedChanged();
    void stateChanged();

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
    Q_INVOKABLE QString nodeNameFromUuid(const QString& uuid, bool fullPath = false) {
        return nodeNameFromQuuid(QUuid{uuid}, fullPath);
    }
    QString nodeNameFromQuuid(const QUuid& uuid, bool fullPath = false);

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

    QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex &parent) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const  override{
        return TreeNode::roleNames();
    }

    // Print the tree to the log
    void dump();

public slots:    

    //void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);

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

    // ServerSynchedCahce overrides
    QCoro::Task<void> pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update) override;
    QCoro::Task<bool> save(const nextapp::pb::Node& node) override;
    QCoro::Task<bool> loadFromCache() override;
    bool hasItems(const nextapp::pb::Status& status) const noexcept override {
        return status.hasNodes();
    }
    bool isRelevant(const nextapp::pb::Update& update) const noexcept override {
        return update.hasNode();
    }
    nextapp::pb::NodeRepeated getItems(const nextapp::pb::Status& status) override{
        return status.nodes().nodes();
    }
    std::string_view itemName() const noexcept override {
        return "node";
    }
    std::shared_ptr<GrpcIncomingStream> openServerStream(nextapp::pb::GetNewReq req) override;


private:
    void addNode(TreeNode *parent, const nextapp::pb::Node& node);
    void moveNode(TreeNode *parent, TreeNode *current, const nextapp::pb::Node& node);
    void insertNode(TreeNode::node_list_t& list, std::shared_ptr<TreeNode>& tn, int row);
    QModelIndex getIndex(TreeNode *node);
    int getInsertRow(const TreeNode *parent, const nextapp::pb::Node& node);
    TreeNode *lookupTreeNode(const QUuid& uuid, bool emptyIsRoot = true);
    bool isDescent(const QUuid &uuid, const QUuid &toParentUuid);
    QCoro::Task<void> onOnline();
    //QCoro::Task<bool> synchFromServer();
    //void setState(State state) noexcept;

    TreeNode::node_list_t& getListFromChild(MainTreeModel::TreeNode& child);
    TreeNode root_;
    QMap<QUuid, TreeNode*> uuid_index_;
    std::vector<std::shared_ptr<nextapp::pb::Update>> pending_updates_;
    bool has_initial_tree_ = false;
    QString selected_;
    static MainTreeModel *instance_;
    State state_{State::LOCAL};
};
