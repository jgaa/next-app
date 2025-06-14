
#include "shared_grpc_server.h"

namespace nextapp::grpc {

namespace {

struct ToNode {
    enum Cols {
        ID, USER, NAME, KIND, DESCR, ACTIVE, PARENT, VERSION, UPDATED, DELETED, EXCLUDE_FROM_WR, CATEGORY
    };

    static constexpr string_view selectCols = "id, user, name, kind, descr, active, parent, version, updated, deleted, exclude_from_wr, category ";

    static void assign(const boost::mysql::row_view& row, pb::Node& node, const RequestCtx& rctx) {
        node.set_uuid(pb_adapt(row.at(ID).as_string()));
        node.set_user(pb_adapt(row.at(USER).as_string()));
        if (row.at(NAME).is_string()) {
            node.set_name(pb_adapt(row.at(NAME).as_string()));
        }
        node.set_version(row.at(VERSION).as_int64());
        const auto kind = row.at(KIND).as_int64();
        if (pb::Node::Kind_IsValid(kind)) {
            node.set_kind(static_cast<pb::Node::Kind>(kind));
        }
        if (!row.at(DESCR).is_null()) {
            node.set_descr(pb_adapt(row.at(DESCR).as_string()));
        }
        node.set_active(row.at(ACTIVE).as_int64() != 0);
        if (!row.at(PARENT).is_null()) {
            node.set_parent(pb_adapt(row.at(PARENT).as_string()));
        }
        node.set_deleted(row.at(ToNode::DELETED).as_int64() == 1);
        node.set_updated(toMsTimestamp(row.at(ToNode::UPDATED).as_datetime(), rctx.uctx->tz()));
        if (row.at(EXCLUDE_FROM_WR).is_int64() && row.at(EXCLUDE_FROM_WR).as_int64() != 0) {
            node.set_excludefromweeklyreview(true);
        }
        if (row.at(CATEGORY).is_string()) {
            node.set_category(pb_adapt(row.at(CATEGORY).as_string()));
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

            dbopts.reconnect_and_retry_query = false;
            const auto res = co_await owner_.server().db().exec(format(
                    "INSERT INTO node (id, user, name, kind, descr, active, parent, exclude_from_wr, category) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
                    "RETURNING {}", ToNode::selectCols), dbopts,
                id,
                cuser,
                req->node().name(),
                static_cast<int>(req->node().kind()),
                req->node().descr(),
                active,
                parent,
                req->node().excludefromweeklyreview(),
                toStringOrNull(req->node().category()));

            if (!res.empty()) {
                auto node = reply->mutable_node();
                ToNode::assign(res.rows().front(), *node, rctx);
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

boost::asio::awaitable<void> GrpcServer::saveNodes(jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::Nodes& nodes, RequestCtx& rctx) {
    const auto& cuser = rctx.uctx->userUuid();
    const auto &items = nodes.nodes();
    const size_t num_items = items.size();

    const auto sql = "INSERT INTO node (id, user, name, kind, descr, active, parent, exclude_from_wr, category) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) ";

    enum Cols {
        ID, USER, NAME, KIND, DESCR, ACTIVE, PARENT, EXCLUDE_FROM_WR, CATEGORY, COLS_
    };

    jgaa::mysqlpool::FieldViewMatrix values{num_items, COLS_};

    size_t index = 0;
    for(const auto& node : items) {
        assert(index < values.rows());
        values.set(index, ID, node.uuid());
        values.set(index, USER, cuser);
        values.set(index, NAME, node.name());
        values.set(index, KIND, static_cast<int>(node.kind()));
        values.set(index, DESCR, toStringViewOrNull(node.descr()));
        values.set(index, ACTIVE, node.active() ? 1 : 0);
        values.set(index, PARENT, toStringViewOrNull(node.parent()));
        values.set(index, EXCLUDE_FROM_WR, node.excludefromweeklyreview() ? 1 : 0);
        values.set(index, CATEGORY, toStringViewOrNull(node.category()));
        ++index;
    }

    co_await dbh.exec(sql, values);
}


boost::asio::awaitable<void> GrpcServer::addNodes(const std::string &parent_id, const pb::NodeTemplate &t, RequestCtx& rctx)
{
    const auto& cuser = rctx.uctx->userUuid();

    string id;
    if (!t.name().empty()) {
        id = newUuidStr();
        const auto kind = static_cast<int>(t.kind());
        auto res = co_await rctx.dbh->exec(R"(INSERT INTO node (id, user, name, kind, descr, parent)
                        VALUES(?, ?, ?, ?, ?, ?))", rctx.uctx->dbOptions(),
                                           id,
                                           cuser,
                                           t.name(),
                                           kind,
                                           t.descr(),
                                           toStringOrNull(parent_id));

        if (!res.affected_rows()) {
            throw server_err{pb::Error::DATABASE_UPDATE_FAILED, "Failed to insert node from template"};
        }
    } else {
        // Only the root-node is without name.
        assert(parent_id.empty());
    }

    for (const auto& child : t.children()) {
        co_await addNodes(id, child, rctx);
    }
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateNodesFromTemplate(::grpc::CallbackServerContext *ctx,
                                                                             const pb::NodeTemplate *req,
                                                                             pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            auto dbopts = uctx->dbOptions();
            auto trx = co_await rctx.dbh->transaction();

            co_await owner_.addNodes({}, *req, rctx);
            co_await trx.commit();
            auto& publish = rctx.publishLater(pb::Update::Operation::Update_Operation_ADDED);
            publish.set_reload(pb::Update::Reload::Update_Reload_NODES);

            co_return;
        });
}

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

                const pb::Node existing = co_await owner_.fetcNode(req->uuid(), cuser, rctx);

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
                    "UPDATE node SET name=?, active=?, kind=?, descr=?, exclude_from_wr=?, category=? "
                    "WHERE id=? AND user=? AND version=?",
                    dbopts,
                    // Update arguments
                    req->name(),
                    req->active(),
                    static_cast<int>(req->kind()),
                    req->descr(),
                    req->excludefromweeklyreview(),
                    toStringOrNull(req->category()),
                    // query arguments
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
                timer.expires_after(100ms);
                co_await timer.async_wait(boost::asio::use_awaitable);
            }

            // Get the current record
            const pb::Node current = co_await owner_.fetcNode(req->uuid(), cuser, rctx);

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

                const pb::Node existing = co_await owner_.fetcNode(req->uuid(), cuser, rctx);

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
                    "UPDATE node SET parent=? WHERE id=? AND user=? AND version=?",
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
                timer.expires_after(100ms);
                co_await timer.async_wait(boost::asio::use_awaitable);
            }

            // Get the current record
            const pb::Node current = co_await owner_.fetcNode(req->uuid(), cuser, rctx);
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
            co_await owner_.deleteNode(req->uuid(), rctx);
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
                ToNode::assign(row, n, rctx);
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

::grpc::ServerWriteReactor<pb::Status> *
GrpcServer::NextappImpl::GetNewNodes(::grpc::CallbackServerContext *ctx, const pb::GetNewReq *req)
{
    return writeStreamHandler(ctx, req,
        [this, req, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto stream_scope = owner_.server().metrics().data_streams_nodes().scoped();

            auto flush = [&](pb::Status& status) -> boost::asio::awaitable<void> {
                co_await stream->sendMessage(std::move(status), boost::asio::use_awaitable);
            };

            const auto total_rows = co_await owner_.exportNodes(
                req->since(), *rctx.dbh, flush, rctx);

            LOG_DEBUG_N << "Sent " << total_rows << " nodes to client.";
            co_return;
    }, __func__);
}


boost::asio::awaitable<uint64_t> GrpcServer::exportNodes(
    const uint64_t since,
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    const export_flush_fn_t& flush_fn,
    RequestCtx& rctx) {

    const auto uctx = rctx.uctx;
    const auto& cuser = uctx->userUuid();
    const auto batch_size = server().config().options.stream_batch_size;

    // Use batched reading from the database, so that we can get all the data, but
    // without running out of memory.
    // TODO: Set a timeout or constraints on how many db-connections we can keep open for batches.
    assert(rctx.dbh);
    co_await  rctx.dbh->start_exec(
        format("SELECT {} from node WHERE user=? AND updated > ?", ToNode::selectCols),
        uctx->dbOptions(), cuser, toMsDateTime(since, uctx->tz()));

    nextapp::pb::Status reply;

    auto *nodes = reply.mutable_nodes();
    auto num_rows_in_batch = 0u;
    auto total_rows = 0u;
    auto batch_num = 0u;

    auto flush = [&]() -> boost::asio::awaitable<void> {
        reply.set_error(::nextapp::pb::Error::OK);
        assert(reply.has_nodes());
        ++batch_num;
        reply.set_message(format("Fetched {} nodes in batch {}", reply.nodes().nodes_size(), batch_num));
        co_await flush_fn(reply);
        reply.Clear();
        nodes = reply.mutable_nodes();
        num_rows_in_batch = {};
    };

    bool read_more = true;
    for(auto rows = co_await rctx.dbh->readSome()
         ; read_more
         ; rows = co_await rctx.dbh->readSome()) {

        read_more = rctx.dbh->shouldReadMore(); // For next iteration

        if (rows.empty()) {
            LOG_TRACE_N << "Out of rows to iterate... num_rows_in_batch=" << num_rows_in_batch;
            break;
        }

        for(const auto& row : rows) {
            auto * node = nodes->add_nodes();
            ToNode::assign(row, *node, rctx);
            ++total_rows;
            // Do we need to flush?
            if (++num_rows_in_batch >= batch_size) {
                co_await flush();
            }
        }

    } // read more from db loop

    co_await flush();

    co_return total_rows;
}

boost::asio::awaitable<pb::Node> GrpcServer::fetcNode(const std::string &uuid, const std::string &userUuid, RequestCtx& rctx)
{
    auto res = co_await rctx.dbh->exec(format("SELECT {} from node where id=? and user=?", ToNode::selectCols),
                                           uuid, userUuid);
    if (!res.has_value()) {
        throw server_err{pb::Error::NOT_FOUND, format("Node {} not found", uuid)};
    }

    pb::Node rval;
    ToNode::assign(res.rows().front(), rval, rctx);
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
    co_return;
}

boost::asio::awaitable<void> GrpcServer::deleteNode(const std::string& uuid, RequestCtx& rctx) {
    const auto dbopts = rctx.uctx->dbOptions();
    const auto& cuser = rctx.uctx->userUuid();

    // The cascading effects from deleting a node may be massive, so we will not
    // manually handle the cascading effects, but rely on the database to handle it.
    // The user-app must re-load all models who depends on nodes/this node to get the
    // correct state.

    // We will first delete the node to create the cascading effect where all the dependent
    // objects are recursively deleted by the database.
    // Then we will add a new, empty node in state deleted to support replication to clients.

    LOG_DEBUG_N << "Deleting node " << uuid << " for user " << cuser;

    auto trx = co_await rctx.dbh->transaction();
    const auto dres = co_await rctx.dbh->exec("DELETE from node where id=? and user=?", dbopts, uuid, cuser);
    if (dres.affected_rows() == 0) {
        throw server_err{pb::Error::NOT_FOUND, format("Node {} not found", uuid)};
    }

    co_await rctx.dbh->exec("INSERT INTO node (id, user, active, deleted) VALUES (?, ?, 0, 1)", dbopts, uuid, cuser);

    auto res = co_await rctx.dbh->exec(format("SELECT {} from node where id=? and user=?",
                                              ToNode::selectCols),
                                       uuid, cuser);
    if (!res.rows().empty()) {
        auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_DELETED);
        ToNode::assign(res.rows().front(), *update.mutable_node(), rctx);
    }

    co_await trx.commit();
}

} // ns
