
#include "shared_grpc_server.h"

namespace nextapp::grpc {

namespace {

struct ToNode {
    enum Cols {
        ID, USER, NAME, KIND, DESCR, ACTIVE, PARENT, VERSION
    };

    static constexpr string_view selectCols = "id, user, name, kind, descr, active, parent, version";

    static void assign(const boost::mysql::row_view& row, pb::Node& node) {
        node.set_uuid(to_view(row.at(ID).as_string()));
        node.set_user(to_view(row.at(USER).as_string()));
        node.set_name(to_view(row.at(NAME).as_string()));
        node.set_version(row.at(VERSION).as_int64());
        const auto kind = row.at(KIND).as_int64();
        if (pb::Node::Kind_IsValid(kind)) {
            node.set_kind(static_cast<pb::Node::Kind>(kind));
        }
        if (!row.at(DESCR).is_null()) {
            node.set_descr(to_view(row.at(DESCR).as_string()));
        }
        node.set_active(row.at(ACTIVE).as_int64() != 0);
        if (!row.at(PARENT).is_null()) {
            node.set_parent(to_view(row.at(PARENT).as_string()));
        }
    }
};

} // anon ns

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateNode(::grpc::CallbackServerContext *ctx, const pb::CreateNodeReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            auto dbopts = uctx->dbOptions();

            optional<string> parent = req->node().parent();
            if (parent->empty()) {
                parent.reset();
            } else {
                co_await owner_.validateNode(*parent, cuser);
            }

            auto id = req->node().uuid();
            if (id.empty()) {
                id = newUuidStr();
            }

            bool active = true;
            if (!req->node().has_active()) {
                active = req->node().active();
            }

            enum Cols {
                ID, USER, NAME, KIND, DESCR, ACTIVE, PARENT, VERSION
            };

            dbopts.reconnect_and_retry_query = false;
            const auto res = co_await owner_.server().db().exec(format(
                    "INSERT INTO node (id, user, name, kind, descr, active, parent) VALUES (?, ?, ?, ?, ?, ?, ?) "
                    "RETURNING {}", ToNode::selectCols), dbopts,
                id,
                cuser,
                req->node().name(),
                static_cast<int>(req->node().kind()),
                req->node().descr(),
                active,
                parent);

            if (!res.empty()) {
                auto node = reply->mutable_node();
                ToNode::assign(res.rows().front(), *node);
                reply->set_error(pb::Error::OK);
            } else {
                assert(false); // Should get exception on error
            }

            // Notify clients
            auto update = newUpdate(pb::Update::Operation::Update_Operation_ADDED);
            auto node = update->mutable_node();
            *node = reply->node();
            rctx.publishLater(update);

            co_return;
        });
}  // CreateNode

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::UpdateNode(::grpc::CallbackServerContext *ctx, const pb::Node *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            // Get the existing node

            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            const auto& dbopts = uctx->dbOptions();

            bool moved = false;
            bool data_changed = false;

            for(auto retry = 0;; ++retry) {

                const pb::Node existing = co_await owner_.fetcNode(req->uuid(), cuser);

                // Check if any data has changed
                data_changed = req->name() != existing.name()
                               || req->active() != existing.active()
                               || req->kind() != existing.kind()
                               || req->descr() != existing.descr();

                // Check if the parent has changed.
                if (req->parent() != existing.parent()) {
                    throw server_err{pb::Error::DIFFEREENT_PARENT, "UpdateNode cannot move nodes in the tree"};
                }

                // Update the data, if version is unchanged
                auto res = co_await owner_.server().db().exec(
                    "UPDATE node SET name=?, active=?, kind=?, descr=?, version=version+1 WHERE id=? AND user=? AND version=?",
                    dbopts,
                    req->name(),
                    req->active(),
                    static_cast<int>(req->kind()),
                    req->descr(),
                    req->uuid(),
                    cuser,
                    existing.version()
                    );

                if (res.affected_rows() > 0) {
                    break; // Only succes-path out of the loop
                }

                LOG_DEBUG << "updateNode: Failed to update. Looping for retry.";
                if (retry >= 5) {
                    throw server_err(pb::Error::DATABASE_UPDATE_FAILED, "I failed to update, despite retrying");
                }

                boost::asio::steady_timer timer{owner_.server().ctx()};
                timer.expires_from_now(100ms);
                co_await timer.async_wait(boost::asio::use_awaitable);
            }

            // Get the current record
            const pb::Node current = co_await owner_.fetcNode(req->uuid(), cuser);

            // Notify clients about changes

            reply->set_error(pb::Error::OK);
            *reply->mutable_node() = current;

            // Notify clients
            auto update = newUpdate(pb::Update::Operation::Update_Operation_UPDATED);
            *update->mutable_node() = current;
            rctx.publishLater(update);

            co_return;
        });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::MoveNode(::grpc::CallbackServerContext *ctx, const pb::MoveNodeReq *req, pb::Status *reply)
{
       return unaryHandler(ctx, req, reply,
                        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            // Get the existing node

            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();

            for(auto retry = 0;; ++retry) {

                const pb::Node existing = co_await owner_.fetcNode(req->uuid(), cuser);

                if (existing.parent() == req->parentuuid()) {
                    reply->set_error(pb::Error::NO_CHANGES);
                    reply->set_message("The parent has not changed. Ignoring the reqest!");
                    co_return;
                }

                if (req->parentuuid() == req->uuid()) {
                    reply->set_error(pb::Error::CONSTRAINT_FAILED);
                    reply->set_message("A node cannot be its own parent. Ignoring the request!");
                    LOG_DEBUG << "A node cannot be its own parent. Ignoring the request for node-id " << req->uuid();
                    co_return;
                }

                optional<string> parent;
                if (!req->parentuuid().empty()) {
                    co_await owner_.validateNode(req->parentuuid(), cuser);
                    parent = req->parentuuid();
                }

                // Update the data, if version is unchanged
                auto res = co_await owner_.server().db().exec(
                    "UPDATE node SET parent=?, version=version+1 WHERE id=? AND user=? AND version=?",
                    parent,
                    req->uuid(),
                    cuser,
                    existing.version()
                    );

                if (res.affected_rows() > 0) {
                    break; // Only succes-path out of the loop
                }

                LOG_DEBUG << "updateNode: Failed to update. Looping for retry.";
                if (retry >= 5) {
                    throw server_err(pb::Error::DATABASE_UPDATE_FAILED, "I failed to update, despite retrying");
                }

                boost::asio::steady_timer timer{owner_.server().ctx()};
                timer.expires_from_now(100ms);
                co_await timer.async_wait(boost::asio::use_awaitable);
            }

            // Get the current record
            const pb::Node current = co_await owner_.fetcNode(req->uuid(), cuser);
            // Notify clients about changes

            reply->set_error(pb::Error::OK);
            *reply->mutable_node() = current;

            // Notify clients
            auto update = newUpdate(pb::Update::Operation::Update_Operation_MOVED);
            *update->mutable_node() = current;
            rctx.publishLater(update);

            co_return;
        });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteNode(::grpc::CallbackServerContext *ctx, const pb::DeleteNodeReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            // Get the existing node

            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();

            const auto node = co_await owner_.fetcNode(req->uuid(), cuser);

            auto res = co_await owner_.server().db().exec(format("DELETE from node where id=? and user=?", ToNode::selectCols),
                                                          req->uuid(), cuser);

            if (!res.has_value() || res.affected_rows() == 0) {
                throw server_err{pb::Error::NOT_FOUND, format("Node {} not found", req->uuid())};
            }

            reply->set_error(pb::Error::OK);
            *reply->mutable_node() = node;

            // Notify clients
            auto update = newUpdate(pb::Update::Operation::Update_Operation_DELETED);
            *update->mutable_node() = node;
            rctx.publishLater(update);

            co_return;
        });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetNodes(::grpc::CallbackServerContext *ctx,
                                                              const pb::GetNodesReq *req,
                                                              pb::NodeTree *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::NodeTree *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            const auto& dbopts = uctx->dbOptions();

            const auto res = co_await owner_.server().db().exec(format(R"(
                WITH RECURSIVE tree AS (
                SELECT * FROM node WHERE user=?
                UNION
                SELECT n.* FROM node AS n, tree AS p
                WHERE (n.parent = p.id or n.parent IS NULL) and n.user = ?
                )
                SELECT {} from tree ORDER BY parent, name)", ToNode::selectCols), dbopts, cuser, cuser);

            std::deque<pb::NodeTreeItem> pending;
            map<string, pb::NodeTreeItem *> known;

            // Root level
            known[""] = reply->mutable_root();

            assert(res.has_value());
            for(const auto& row : res.rows()) {
                pb::Node n;
                ToNode::assign(row, n);
                const auto parent = n.parent();

                if (auto it = known.find(parent); it != known.end()) {
                    auto child = it->second->add_children();
                    child->mutable_node()->Swap(&n);
                    known[child->node().uuid()] = child;
                } else {
                    // Track it for later
                    const auto id = n.uuid();
                    pending.push_back({});
                    auto child = &pending.back();
                    child->mutable_node()->Swap(&n);
                    known[child->node().uuid()] = child;
                }
            }


            // By now, all the parents are in the known list.
            // We can safely move all the pending items to the child lists of the parents
            for(auto& v : pending) {
                if (auto it = known.find(v.node().parent()); it != known.end()) {
                    auto id = v.node().uuid();
                    auto& parent = *it->second;
                    parent.add_children()->Swap(&v);
                    // known lookup must point to the node's new memory location
                    assert(parent.children().size() > 0);
                    known[id] = &parent.mutable_children()->at(parent.children().size()-1);
                } else {
                    assert(false);
                }
            }

            co_return;
        });
}

boost::asio::awaitable<pb::Node> GrpcServer::fetcNode(const std::string &uuid, const std::string &userUuid)
{
    auto res = co_await server().db().exec(format("SELECT {} from node where id=? and user=?", ToNode::selectCols),
                                           uuid, userUuid);
    if (!res.has_value()) {
        throw server_err{pb::Error::NOT_FOUND, format("Node {} not found", uuid)};
    }

    pb::Node rval;
    ToNode::assign(res.rows().front(), rval);
    co_return rval;
}

boost::asio::awaitable<void> GrpcServer::validateNode(const std::string &parentUuid, const std::string &userUuid)
{
    auto handle = co_await server().db().getConnection();
    co_await validateNode(handle, parentUuid, userUuid);
}

boost::asio::awaitable<void> GrpcServer::validateNode(jgaa::mysqlpool::Mysqlpool::Handle& handle, const std::string &parentUuid, const std::string &userUuid)
{
    auto res = co_await handle.exec("SELECT id FROM node where id=? and user=?", parentUuid, userUuid);
    if (!res.has_value()) {
        throw server_err{pb::Error::INVALID_PARENT, "Node id must exist and be owned by the user"};
    }
}

} // ns
