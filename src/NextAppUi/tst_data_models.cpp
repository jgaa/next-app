
#include "gtest/gtest.h"

#include "MainTreeModel.h"

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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
