
#include <vector>

#include "gtest/gtest.h"

#include "MainTreeModel.h"

using namespace std;
using namespace std::string_literals;

namespace {

auto createNode(QString name) {
    nextapp::pb::Node n;
    n.setName(name);
    n.setUuid(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces));

    return n;
}

nextapp::pb::NodeTreeItem createTreeItem(QString name, unsigned children) {
    nextapp::pb::NodeTreeItem ti;

    ti.setNode(createNode(name));

    for(auto i = 0; i < children; ++i ) {
        auto cname = name.toStdString() + "-child"s + to_string(i);
        ti.children().append(createTreeItem(cname.c_str(), 0));
    }

    return ti;
}

} // anon ns

TEST(MainTree, addRoot) {

    MainTreeModel mt;

    ::nextapp::pb::Node node;
    node.setUuid("bce18f00-a32a-11ee-bd5a-fb8b4ebbc327");
    node.setName("Root-1");

    EXPECT_FALSE(mt.hasChildren({}));

    mt.addNode(node, {}, {});

    EXPECT_TRUE(mt.hasChildren({}));
    auto ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node.name());
}

TEST(MainTree, addFront) {
    MainTreeModel mt;

    ::nextapp::pb::Node node;
    node.setUuid("bce18f00-a32a-11ee-bd5a-fb8b4ebbc327");
    node.setName("Root-2");

    EXPECT_FALSE(mt.hasChildren({}));

    mt.addNode(node, {}, {});

    EXPECT_TRUE(mt.hasChildren({}));
    auto ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node.name());

    ::nextapp::pb::Node node2;
    node2.setUuid("c7bf3f02-a3c6-11ee-bfa7-1300303663f1");
    node2.setName("Root-1");

    // Add before existing first node
    mt.addNode(node2, {}, QUuid{node.uuid()});

    ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node2.name());

    ix = mt.index(1, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node.name());
}

TEST(MainTree, addEnd) {
    MainTreeModel mt;

    ::nextapp::pb::Node node;
    node.setUuid("bce18f00-a32a-11ee-bd5a-fb8b4ebbc327");
    node.setName("Root-1");

    EXPECT_FALSE(mt.hasChildren({}));

    mt.addNode(node, {}, {});

    EXPECT_TRUE(mt.hasChildren({}));
    auto ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node.name());

    ::nextapp::pb::Node node2;
    node2.setUuid("c7bf3f02-a3c6-11ee-bfa7-1300303663f1");
    node2.setName("Root-2");

    // Add last under root
    mt.addNode(node2, {}, {});

    ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node.name());

    ix = mt.index(1, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node2.name());
}

TEST(MainTree, addChild) {

    MainTreeModel mt;

    ::nextapp::pb::Node node;
    node.setUuid("bce18f00-a32a-11ee-bd5a-fb8b4ebbc327");
    node.setName("Root-1");

    EXPECT_FALSE(mt.hasChildren({}));

    mt.addNode(node, {}, {});

    EXPECT_TRUE(mt.hasChildren({}));
    auto ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node.name());

    ::nextapp::pb::Node node2;
    node2.setUuid("c7bf3f02-a3c6-11ee-bfa7-1300303663f1");
    node2.setName("Child-1");

    // Add child
    mt.addNode(node2, QUuid{node.uuid()}, {});

    auto root_ix = mt.index(0, 0, {});
    EXPECT_TRUE(root_ix.isValid());
    EXPECT_EQ(mt.data(root_ix, Qt::DisplayRole), node.name());

    auto child_ix = mt.index(0, 0, root_ix);
    EXPECT_TRUE(child_ix.isValid());
    EXPECT_EQ(mt.data(child_ix, Qt::DisplayRole).toString().toStdString(), node2.name().toStdString());
}

TEST(MainTree, parent) {

    MainTreeModel mt;

    ::nextapp::pb::Node node;
    node.setUuid("bce18f00-a32a-11ee-bd5a-fb8b4ebbc327");
    node.setName("Root-1");

    EXPECT_FALSE(mt.hasChildren({}));

    mt.addNode(node, {}, {});

    EXPECT_TRUE(mt.hasChildren({}));
    auto ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node.name());

    ::nextapp::pb::Node node2;
    node2.setUuid("c7bf3f02-a3c6-11ee-bfa7-1300303663f1");
    node2.setName("Child-1");

    // Add child
    mt.addNode(node2, QUuid{node.uuid()}, {});

    auto root_ix = mt.index(0, 0, {});
    EXPECT_TRUE(root_ix.isValid());
    EXPECT_EQ(mt.data(root_ix, Qt::DisplayRole), node.name());

    auto child_ix = mt.index(0, 0, root_ix);
    EXPECT_TRUE(child_ix.isValid());
    EXPECT_EQ(mt.data(child_ix, Qt::DisplayRole).toString().toStdString(), node2.name().toStdString());

    auto parent = mt.parent(child_ix);
    EXPECT_TRUE(parent.isValid());
    EXPECT_EQ(mt.data(parent, Qt::DisplayRole).toString().toStdString(), node.name().toStdString());

    parent = mt.parent(root_ix);
    EXPECT_FALSE(parent.isValid());
}

TEST(MainTree, addChildren) {

    MainTreeModel mt;

    ::nextapp::pb::Node node;
    node.setUuid("bce18f00-a32a-11ee-bd5a-fb8b4ebbc327");
    node.setName("Root-1");

    EXPECT_FALSE(mt.hasChildren({}));

    mt.addNode(node, {}, {});

    EXPECT_TRUE(mt.hasChildren({}));
    auto ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node.name());

    QUuid current_uuid;
    vector<QUuid> uuids;

    auto root_ix = mt.index(0, 0, {});

    for(auto i = 0; i < 100; ++i) {
        ::nextapp::pb::Node node2;
        current_uuid = QUuid::createUuid();
        uuids.push_back(current_uuid);
        node2.setUuid(current_uuid.toString(QUuid::StringFormat::WithoutBraces));
        node2.setName(QString{("Child-"s + to_string(i)).c_str()});

        // Add child
        mt.addNode(node2, QUuid{node.uuid()}, {});

        EXPECT_TRUE(root_ix.isValid());
        EXPECT_EQ(mt.data(root_ix, Qt::DisplayRole), node.name());

        auto child_ix = mt.index(i, 0, root_ix);
        EXPECT_TRUE(child_ix.isValid());
        EXPECT_EQ(mt.data(child_ix, Qt::DisplayRole).toString().toStdString(), node2.name().toStdString());
    }

    {
        // Insert child at third position
        auto third_uuid = uuids.at(2);
        auto third_ix = mt.index(2, 0, root_ix);
        EXPECT_TRUE(third_ix.isValid());
        auto third_name = mt.data(third_ix, Qt::DisplayRole).toString().toStdString();

        ::nextapp::pb::Node node2;
        current_uuid = QUuid::createUuid();
        node2.setUuid(current_uuid.toString(QUuid::StringFormat::WithoutBraces));
        node2.setName(QString{"Child-inserted-as-3rd"});

        // Add child
        mt.addNode(node2, QUuid{node.uuid()}, third_uuid);
    }

    for(auto i = 0; i < 101; ++i) {
        auto name_num = (i < 3) ? i : i - 1;
        auto name = "Child-"s + to_string(name_num);
        if (i == 2) {
            name = "Child-inserted-as-3rd";
        }

        auto child_ix = mt.index(i, 0, root_ix);
        EXPECT_TRUE(child_ix.isValid());
        EXPECT_EQ(mt.data(child_ix, Qt::DisplayRole).toString().toStdString(), name);
    }
}

TEST(MainTree, deleteSingleRoot) {

    MainTreeModel mt;

    ::nextapp::pb::Node node;
    node.setUuid("bce18f00-a32a-11ee-bd5a-fb8b4ebbc327");
    node.setName("Root-1");

    EXPECT_FALSE(mt.hasChildren({}));

    mt.addNode(node, {}, {});

    EXPECT_TRUE(mt.hasChildren({}));
    auto ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node.name());

    EXPECT_TRUE(mt.hasChildren({}));

    mt.deleteNode(QUuid(node.uuid()));
    ix = mt.index(0, 0, {});
    EXPECT_FALSE(ix.isValid());
    EXPECT_FALSE(mt.hasChildren({}));
}

TEST(MainTree, deleteFirst) {

    MainTreeModel mt;

    // Create three entries at the root level
    std::vector<QUuid> uuids;
    for(auto i = 0; i < 3; ++i) {
        ::nextapp::pb::Node node2;
        const auto current_uuid = QUuid::createUuid();
        uuids.push_back(current_uuid);
        node2.setUuid(current_uuid.toString(QUuid::StringFormat::WithoutBraces));
        node2.setName(QString{("Root-"s + to_string(i)).c_str()});
        mt.addNode(node2, {}, {}); // Append
    }

    EXPECT_EQ(mt.rowCount({}), 3);

    for(auto i = 0; i < 3; ++i) {
        auto ix = mt.index(i, 0, {});
        EXPECT_TRUE(ix.isValid());
        const auto name = "Root-"s + to_string(i);
        EXPECT_EQ(mt.data(ix, Qt::DisplayRole).toString().toStdString(), name);
    }

    mt.deleteNode(uuids.at(0));
    EXPECT_EQ(mt.rowCount({}), 2);
    EXPECT_EQ(mt.data(mt.index(0, 0, {}), Qt::DisplayRole), "Root-1");
    EXPECT_EQ(mt.data(mt.index(1, 0, {}), Qt::DisplayRole), "Root-2");
}

TEST(MainTree, deleteMiddle) {

    MainTreeModel mt;

    // Create three entries at the root level
    std::vector<QUuid> uuids;
    for(auto i = 0; i < 3; ++i) {
        ::nextapp::pb::Node node2;
        const auto current_uuid = QUuid::createUuid();
        uuids.push_back(current_uuid);
        node2.setUuid(current_uuid.toString(QUuid::StringFormat::WithoutBraces));
        node2.setName(QString{("Root-"s + to_string(i)).c_str()});
        mt.addNode(node2, {}, {}); // Append
    }

    EXPECT_EQ(mt.rowCount({}), 3);

    for(auto i = 0; i < 3; ++i) {
        auto ix = mt.index(i, 0, {});
        EXPECT_TRUE(ix.isValid());
        const auto name = "Root-"s + to_string(i);
        EXPECT_EQ(mt.data(ix, Qt::DisplayRole).toString().toStdString(), name);
    }

    mt.deleteNode(uuids.at(1));
    EXPECT_EQ(mt.rowCount({}), 2);
    EXPECT_EQ(mt.data(mt.index(0, 0, {}), Qt::DisplayRole), "Root-0");
    EXPECT_EQ(mt.data(mt.index(1, 0, {}), Qt::DisplayRole), "Root-2");
}

TEST(MainTree, deleteLast) {

    MainTreeModel mt;

    // Create three entries at the root level
    std::vector<QUuid> uuids;
    for(auto i = 0; i < 3; ++i) {
        ::nextapp::pb::Node node2;
        const auto current_uuid = QUuid::createUuid();
        uuids.push_back(current_uuid);
        node2.setUuid(current_uuid.toString(QUuid::StringFormat::WithoutBraces));
        node2.setName(QString{("Root-"s + to_string(i)).c_str()});
        mt.addNode(node2, {}, {}); // Append
    }

    EXPECT_EQ(mt.rowCount({}), 3);

    for(auto i = 0; i < 3; ++i) {
        auto ix = mt.index(i, 0, {});
        EXPECT_TRUE(ix.isValid());
        const auto name = "Root-"s + to_string(i);
        EXPECT_EQ(mt.data(ix, Qt::DisplayRole).toString().toStdString(), name);
    }

    mt.deleteNode(uuids.at(2));
    EXPECT_EQ(mt.rowCount({}), 2);
    EXPECT_EQ(mt.data(mt.index(0, 0, {}), Qt::DisplayRole), "Root-0");
    EXPECT_EQ(mt.data(mt.index(1, 0, {}), Qt::DisplayRole), "Root-1");
}

TEST(MainTree, deleteChild) {

    MainTreeModel mt;

    ::nextapp::pb::Node node;
    node.setUuid("bce18f00-a32a-11ee-bd5a-fb8b4ebbc327");
    node.setName("Root-1");

    EXPECT_FALSE(mt.hasChildren({}));

    mt.addNode(node, {}, {});

    EXPECT_TRUE(mt.hasChildren({}));
    auto ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_EQ(mt.data(ix, Qt::DisplayRole), node.name());

    ::nextapp::pb::Node node2;
    node2.setUuid("c7bf3f02-a3c6-11ee-bfa7-1300303663f1");
    node2.setName("Child-1");

    // Add child
    mt.addNode(node2, QUuid{node.uuid()}, {});

    auto root_ix = mt.index(0, 0, {});
    EXPECT_TRUE(root_ix.isValid());
    EXPECT_EQ(mt.data(root_ix, Qt::DisplayRole), node.name());

    auto child_ix = mt.index(0, 0, root_ix);
    EXPECT_TRUE(child_ix.isValid());
    EXPECT_EQ(mt.data(child_ix, Qt::DisplayRole).toString().toStdString(), node2.name().toStdString());

    EXPECT_TRUE(mt.hasChildren({}));

    mt.deleteNode(QUuid(node2.uuid()));
    ix = mt.index(0, 0, {});
    EXPECT_TRUE(ix.isValid());
    EXPECT_TRUE(mt.hasChildren({}));
}

TEST(MainTree, setAllNodes) {
    nextapp::pb::NodeTree tree;
    tree.nodes().append(createTreeItem("root", 3));

    MainTreeModel mt;
    mt.setAllNodes(tree);

    auto root = mt.index(0, 0, {});
    EXPECT_TRUE(root.isValid());
    EXPECT_TRUE(mt.hasChildren({}));
    EXPECT_EQ(mt.rowCount({}), 1);

    EXPECT_TRUE(root.isValid());
    EXPECT_TRUE(mt.hasChildren(root));
    EXPECT_EQ(mt.rowCount(root), 3);

    auto c1 = mt.index(0, 0, root);
    EXPECT_FALSE(mt.hasChildren(c1));
    EXPECT_EQ(mt.data(c1, Qt::DisplayRole), "root-child0");

    auto c2 = mt.index(1, 0, root);
    EXPECT_FALSE(mt.hasChildren(c2));
    EXPECT_EQ(mt.data(c2, Qt::DisplayRole), "root-child1");

    auto c3 = mt.index(2, 0, root);
    EXPECT_FALSE(mt.hasChildren(c3));
    EXPECT_EQ(mt.data(c3, Qt::DisplayRole), "root-child2");
}

int main(int argc, char **argv) {
    static_assert(__cplusplus >= 202002L);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
