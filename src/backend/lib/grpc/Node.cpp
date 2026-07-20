
#include "shared_grpc_server.h"

#include <algorithm>

namespace nextapp::grpc {

namespace {

uint32_t countTemplateNodes(const pb::NodeTemplate& root)
{
    uint32_t total = root.name().empty() ? 0u : 1u;
    for (const auto& child : root.children()) {
        total += countTemplateNodes(child);
    }
    return total;
}

bool templateHasInbox(const pb::NodeTemplate& root)
{
    return root.inbox() || std::ranges::any_of(root.children(), templateHasInbox);
}

struct ToNode {
    enum Cols {
        ID, USER, NAME, KIND, DESCR, ACTIVE, INBOX, PARENT, VERSION, UPDATED, UPDATED_ID, DELETED, EXCLUDE_FROM_WR, CATEGORY
    };

    static constexpr string_view selectCols = "id, user, name, kind, descr, active, inbox, parent, version, updated, updated_id, deleted, exclude_from_wr, category ";

    static void assign(const boost::mysql::row_view& row, pb::Node& node, const RequestCtx& rctx,
                       bool include_updated_id = true) {
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
        node.set_inbox(row.at(INBOX).as_int64() != 0);
        if (!row.at(PARENT).is_null()) {
            node.set_parent(pb_adapt(row.at(PARENT).as_string()));
        }
        node.set_deleted(row.at(ToNode::DELETED).as_int64() == 1);
        node.set_updated(toMsTimestamp(row.at(ToNode::UPDATED).as_datetime(), rctx.uctx->tz()));
        if (include_updated_id) {
            node.set_updatedid(row.at(ToNode::UPDATED_ID).as_uint64());
        }
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
    return mutatingUnaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            auto dbopts = uctx->dbOptions();
            auto trx = co_await rctx.dbh->transaction();

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
            rctx.session().requireWritableForAdd("node");

            if (req->node().inbox()) {
                co_await rctx.dbh->exec("UPDATE node SET inbox=FALSE WHERE user=?", cuser);
            }

            bool active = true;
            if (!req->node().has_active()) {
                active = req->node().active();
            }

            dbopts.reconnect_and_retry_query = false;
            auto reservation = rctx.uctx->reserveAddition(1, UserContext::PlanResource::NODE);
            const auto res = co_await rctx.dbh->exec(format(
                    "INSERT INTO node (id, user, name, kind, descr, active, inbox, parent, exclude_from_wr, category) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                    "RETURNING {}", ToNode::selectCols), dbopts,
                id,
                cuser,
                req->node().name(),
                static_cast<int>(req->node().kind()),
                req->node().descr(),
                active,
                req->node().inbox(),
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

            co_await trx.commit();

            // Notify clients
            auto update = newUpdate(pb::Update::Operation::Update_Operation_ADDED);
            auto node = update->mutable_node();
            *node = reply->node();
            rctx.publishLater(update);
            reservation.commit();

            co_return;
        });
}  // CreateNode

boost::asio::awaitable<void> GrpcServer::saveNodes(jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::Nodes& nodes, RequestCtx& rctx) {
    const auto& cuser = rctx.uctx->userUuid();
    const auto &items = nodes.nodes();
    const size_t num_items = items.size();
    if (num_items > 0) {
        rctx.session().requireWritableForAdd("nodes");
    }

    const auto inbox_count = std::ranges::count_if(items, [](const auto& node) { return node.inbox(); });
    if (inbox_count > 1) {
        throw server_err{pb::Error::CONSTRAINT_FAILED, "Only one node can be marked as inbox"};
    }
    if (inbox_count == 1) {
        co_await dbh.exec("UPDATE node SET inbox=FALSE WHERE user=?", cuser);
    }

    const auto sql = "INSERT INTO node (id, user, name, kind, descr, active, inbox, parent, exclude_from_wr, category) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) ";

    enum Cols {
        ID, USER, NAME, KIND, DESCR, ACTIVE, INBOX, PARENT, EXCLUDE_FROM_WR, CATEGORY, COLS_
    };

    jgaa::mysqlpool::FieldViewMatrix values{num_items, COLS_};

    size_t index = 0;
    for(const auto& node : items) {
        LOG_TRACE_N << "Saving node " << node.uuid() << " for user " << cuser;
        assert(index < values.rows());
        values.set(index, ID, node.uuid());
        values.set(index, USER, cuser);
        values.set(index, NAME, node.name());
        values.set(index, KIND, static_cast<int>(node.kind()));
        values.set(index, DESCR, toStringViewOrNull(node.descr()));
        values.set(index, ACTIVE, node.active() ? 1 : 0);
        values.set(index, INBOX, node.inbox() ? 1 : 0);
        values.set(index, PARENT, toStringViewOrNull(node.parent()));
        values.set(index, EXCLUDE_FROM_WR, node.excludefromweeklyreview() ? 1 : 0);
        values.set(index, CATEGORY, toStringViewOrNull(node.category()));
        ++index;
    }

    auto reservation = rctx.uctx->reserveAddition(static_cast<uint32_t>(num_items), UserContext::PlanResource::NODE);
    co_await dbh.exec(sql, values);
    reservation.commit();
}


boost::asio::awaitable<void> GrpcServer::addNodes(const std::string &parent_id, const pb::NodeTemplate &t, RequestCtx& rctx)
{
    const auto& cuser = rctx.uctx->userUuid();

    string id;
    if (!t.name().empty()) {
        id = newUuidStr();
        const auto kind = static_cast<int>(t.kind());
        if (t.inbox()) {
            co_await rctx.dbh->exec("UPDATE node SET inbox=FALSE WHERE user=?", cuser);
        }
        auto res = co_await rctx.dbh->exec(R"(INSERT INTO node (id, user, name, kind, descr, inbox, parent)
                        VALUES(?, ?, ?, ?, ?, ?, ?))", rctx.uctx->dbOptions(),
                                           id,
                                           cuser,
                                           t.name(),
                                           kind,
                                           t.descr(),
                                           t.inbox(),
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
    return mutatingUnaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            auto dbopts = uctx->dbOptions();
            auto trx = co_await rctx.dbh->transaction();
            rctx.session().requireWritableForAdd("nodes");
            if (templateHasInbox(*req)) {
                co_await rctx.dbh->exec("UPDATE node SET inbox=FALSE WHERE user=?", cuser);
            }
            auto reservation = rctx.uctx->reserveAddition(countTemplateNodes(*req), UserContext::PlanResource::NODE);

            co_await owner_.addNodes({}, *req, rctx);
            co_await trx.commit();
            reservation.commit();
            auto& publish = rctx.publishLater(pb::Update::Operation::Update_Operation_ADDED);
            publish.set_reload(pb::Update::Reload::Update_Reload_NODES);

            co_return;
        });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::ResetNodes(::grpc::CallbackServerContext *ctx,
                                                                const pb::ResetNodesReq *req,
                                                                pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();
            auto trx = co_await rctx.dbh->transaction();
            rctx.session().requireWritableForAdd("nodes");

            co_await rctx.dbh->exec("DELETE FROM time_block WHERE user = ?", cuser);
            co_await rctx.dbh->exec("DELETE FROM node WHERE user = ?", cuser);
            rctx.uctx->onMassDelete({UserContext::PlanResource::TIME_BLOCK,
                                     UserContext::PlanResource::NODE,
                                     UserContext::PlanResource::ACTION,
                                     UserContext::PlanResource::WORK_SESSION});

            optional<UserContext::ResourceReservation> reservation;
            if (req->has_template_root()) {
                reservation.emplace(rctx.uctx->reserveAddition(countTemplateNodes(req->template_root()),
                                                               UserContext::PlanResource::NODE));
                co_await owner_.addNodes({}, req->template_root(), rctx);
            }

            co_await rctx.dbh->exec("UPDATE `user` SET data_sync_epoch = data_sync_epoch + 1 WHERE id = ?", cuser);
            const auto epoch_res = co_await rctx.dbh->exec("SELECT data_sync_epoch FROM `user` WHERE id = ?", cuser);
            if (epoch_res.rows().empty()) {
                throw server_err{pb::Error::GENERIC_ERROR, "Failed to load updated data sync epoch"};
            }
            const auto data_sync_epoch = epoch_res.rows().front().at(0).as_uint64();

            co_await trx.commit();
            if (reservation) {
                reservation->commit();
            }

            rctx.updates.clear();
            co_await rctx.uctx->publishFullResync(data_sync_epoch);

            co_return;
        });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::UpdateNode(::grpc::CallbackServerContext *ctx, const pb::Node *req, pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
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
                auto trx = co_await rctx.dbh->transaction();
                if (req->inbox()) {
                    co_await rctx.dbh->exec("UPDATE node SET inbox=FALSE WHERE user=? AND id<>?", cuser, req->uuid());
                }
                auto res = co_await rctx.dbh->exec(
                    "UPDATE node SET name=?, active=?, kind=?, descr=?, inbox=?, exclude_from_wr=?, category=? "
                    "WHERE id=? AND user=? AND version=?",
                    dbopts,
                    // Update arguments
                    req->name(),
                    req->active(),
                    static_cast<int>(req->kind()),
                    req->descr(),
                    req->inbox(),
                    req->excludefromweeklyreview(),
                    toStringOrNull(req->category()),
                    // query arguments
                    req->uuid(),
                    cuser,
                    existing.version()
                    );

                if (res.affected_rows() > 0) {
                    co_await trx.commit();
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
       return mutatingUnaryHandler(ctx, req, reply,
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
                    auto cycle_res = co_await rctx.dbh->exec(R"(
                        WITH RECURSIVE descendants AS (
                            SELECT id FROM node WHERE id=? AND user=?
                            UNION ALL
                            SELECT n.id
                            FROM node n
                            JOIN descendants d ON n.parent = d.id
                            WHERE n.user=?
                        )
                        SELECT 1 FROM descendants WHERE id=? LIMIT 1)",
                        req->uuid(), cuser, cuser, req->parentuuid());
                    if (!cycle_res.rows().empty()) {
                        reply->set_error(pb::Error::CONSTRAINT_FAILED);
                        reply->set_message("A node cannot be moved under its own descendant.");
                        LOG_DEBUG_N << "Rejecting move of node " << req->uuid()
                                    << " under descendant " << req->parentuuid();
                        co_return;
                    }
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
    return mutatingUnaryHandler(ctx, req, reply,
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
                *req, *rctx.dbh, flush, rctx);

            LOG_DEBUG_N << "Sent " << total_rows << " nodes to client.";
            co_return;
    }, __func__);
}


boost::asio::awaitable<uint64_t> GrpcServer::exportNodes(
    const pb::GetNewReq& req,
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    const export_flush_fn_t& flush_fn,
    RequestCtx& rctx,
    bool removeDeleted) {
    switch (rctx.session().syncClientMode()) {
    case UserContext::SyncClientMode::Current:
        co_return co_await exportNodesCurrent(req, dbh, flush_fn, rctx, removeDeleted);
    case UserContext::SyncClientMode::Legacy:
    case UserContext::SyncClientMode::Unset:
        co_return co_await exportNodesLegacy(req, dbh, flush_fn, rctx, removeDeleted);
    }

    co_return co_await exportNodesLegacy(req, dbh, flush_fn, rctx, removeDeleted);
}

boost::asio::awaitable<uint64_t> GrpcServer::exportNodesLegacy(
    const pb::GetNewReq& req,
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    const export_flush_fn_t& flush_fn,
    RequestCtx& rctx,
    bool removeDeleted) {

    const auto uctx = rctx.uctx;
    const auto& cuser = uctx->userUuid();
    const auto batch_size = server().config().options.stream_batch_size;
    static const auto prefixed_cols = prefixNames(ToNode::selectCols, "n.");

    // Use batched reading from the database, so that we can get all the data, but
    // without running out of memory.
    // TODO: Set a timeout or constraints on how many db-connections we can keep open for batches.
    assert(rctx.dbh);
    const auto cursor = getLegacySyncCursor(req);
    const auto where_clause = "updated > ?";
    const auto order_clause = "updated, sort_path, id";
    const auto sql = format(R"(
        WITH RECURSIVE node_tree AS (
            SELECT
                {0},
                CAST(id AS CHAR(1024)) AS sort_path
            FROM node
            WHERE user=? AND parent IS NULL
            UNION ALL
            SELECT
                {1},
                CONCAT(node_tree.sort_path, '/', n.id) AS sort_path
            FROM node AS n
            INNER JOIN node_tree ON n.parent = node_tree.id
            WHERE n.user=?
        )
        SELECT {0}
        FROM node_tree
        WHERE {2} {3}
        ORDER BY {4})",
        ToNode::selectCols,
        prefixed_cols,
        where_clause,
        (removeDeleted || cursor.full_sync) ? "AND deleted=0" : "",
        order_clause);
    co_await rctx.dbh->start_exec(sql,
        uctx->dbOptions(), cuser, cuser, toMsDateTime(cursor.since, uctx->tz()));

    nextapp::pb::Status reply;

    auto *nodes = reply.mutable_nodes();
    const bool include_updated_id = true;
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
            ToNode::assign(row, *node, rctx, include_updated_id);
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

boost::asio::awaitable<uint64_t> GrpcServer::exportNodesCurrent(
    const pb::GetNewReq& req,
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    const export_flush_fn_t& flush_fn,
    RequestCtx& rctx,
    bool removeDeleted) {

    const auto uctx = rctx.uctx;
    const auto& cuser = uctx->userUuid();
    const auto batch_size = server().config().options.stream_batch_size;
    static const auto prefixed_cols = prefixNames(ToNode::selectCols, "n.");

    assert(rctx.dbh);
    const auto cursor = getCurrentSyncCursor(req);
    const auto fetch_all = cursor.since == 0;
    const auto where_clause = fetch_all ? "TRUE" : "updated_id > ?";
    const auto order_clause = "updated_id, sort_path, id";
    const auto sql = format(R"(
        WITH RECURSIVE node_tree AS (
            SELECT
                {0},
                CAST(id AS CHAR(1024)) AS sort_path
            FROM node
            WHERE user=? AND parent IS NULL
            UNION ALL
            SELECT
                {1},
                CONCAT(node_tree.sort_path, '/', n.id) AS sort_path
            FROM node AS n
            INNER JOIN node_tree ON n.parent = node_tree.id
            WHERE n.user=?
        )
        SELECT {0}
        FROM node_tree
        WHERE {2} {3}
        ORDER BY {4})",
        ToNode::selectCols,
        prefixed_cols,
        where_clause,
        (removeDeleted || cursor.full_sync) ? "AND deleted=0" : "",
        order_clause);
    if (fetch_all) {
        co_await rctx.dbh->start_exec(sql, uctx->dbOptions(), cuser, cuser);
    } else {
        co_await rctx.dbh->start_exec(sql, uctx->dbOptions(), cuser, cuser, cursor.since);
    }

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
    for (auto rows = co_await rctx.dbh->readSome(); read_more; rows = co_await rctx.dbh->readSome()) {
        read_more = rctx.dbh->shouldReadMore();
        if (rows.empty()) {
            LOG_TRACE_N << "Out of rows to iterate... num_rows_in_batch=" << num_rows_in_batch;
            break;
        }

        for (const auto& row : rows) {
            auto *node = nodes->add_nodes();
            ToNode::assign(row, *node, rctx, true);
            ++total_rows;
            if (++num_rows_in_batch >= batch_size) {
                co_await flush();
            }
        }
    }

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
    if (!res.has_value() || res.rows().empty()) {
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
    rctx.uctx->onMassDelete({UserContext::PlanResource::NODE,
                             UserContext::PlanResource::ACTION,
                             UserContext::PlanResource::WORK_SESSION,
                             UserContext::PlanResource::TIME_BLOCK});
    // This delete can fan out across many dependent rows via hard-delete cascades.
    // Force a full sync instead of publishing an incremental tombstone stream that
    // may leave other clients with inconsistent local constraints.
    co_await rctx.dbh->exec("UPDATE `user` SET data_sync_epoch = data_sync_epoch + 1 WHERE id = ?", cuser);
    const auto epoch_res = co_await rctx.dbh->exec("SELECT data_sync_epoch FROM `user` WHERE id = ?", cuser);
    if (epoch_res.rows().empty()) {
        throw server_err{pb::Error::GENERIC_ERROR, "Failed to load updated data sync epoch"};
    }
    const auto data_sync_epoch = epoch_res.rows().front().at(0).as_uint64();

    co_await trx.commit();

    rctx.updates.clear();
    co_await rctx.uctx->publishFullResync(data_sync_epoch);
}

} // ns
