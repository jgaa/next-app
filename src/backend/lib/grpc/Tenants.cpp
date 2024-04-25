
#include "shared_grpc_server.h"

namespace nextapp::grpc {

namespace {

} // anon ns

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateTenant(::grpc::CallbackServerContext *ctx, const pb::CreateTenantReq *req, pb::Status *reply)
{
    // Do some basic checks before we attempt to create anything...
    if (!req->has_tenant() || req->tenant().name().empty()) {
        setError(*reply, pb::Error::MISSING_TENANT_NAME);
    } else {

        for(const auto& user : req->users()) {
            if (user.email().empty()) {
                setError(*reply, pb::Error::MISSING_USER_EMAIL);
            } else if (user.name().empty()) {
                setError(*reply, pb::Error::MISSING_USER_NAME);
            }
        }
    }

    if (reply->error() != pb::Error::OK) {
        auto* reactor = ctx->DefaultReactor();
        reactor->Finish(::grpc::Status::OK);
        return reactor;
    }

    LOG_DEBUG_N << "Request to create tenant " << req->tenant().name();

    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {

            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();

            pb::Tenant tenant{req->tenant()};

            if (tenant.uuid().empty()) {
                tenant.set_uuid(newUuidStr());
            }
            if (tenant.properties().empty()) {
                tenant.mutable_properties();
            }

            const auto properties = toJson(*tenant.mutable_properties());
            if (!tenant.has_kind()) {
                tenant.set_kind(pb::Tenant::Tenant::Kind::Tenant_Kind_Guest);
            }

            co_await owner_.server().db().exec(
                "INSERT INTO tenant (id, name, kind, descr, active, properties) VALUES (?, ?, ?, ?, ?, ?)",
                tenant.uuid(),
                tenant.name(),
                pb::Tenant::Kind_Name(tenant.kind()),
                tenant.descr(),
                tenant.active(),
                properties);

            LOG_INFO << "User " << cuser
                     << " has created tenant name=" << tenant.name() << ", id=" << tenant.uuid()
                     << ", kind=" << pb::Tenant::Kind_Name(tenant.kind());

            // create users
            for(const auto& user_template : req->users()) {
                pb::User user{user_template};

                if (user.uuid().empty()) {
                    user.set_uuid(newUuidStr());
                }

                user.set_tenant(tenant.uuid());
                auto kind = user.kind();
                if (!user.has_kind()) {
                    user.set_kind(pb::User::Kind::User_Kind_Regular);
                }

                if (!user.has_active()) {
                    user.set_active(true);
                }

                auto user_props = toJson(*user.mutable_properties());
                co_await owner_.server().db().exec(
                    "INSERT INTO user (id, tenant, name, email, kind, active, descr, properties) VALUES (?,?,?,?,?,?,?,?)",
                    user.uuid(),
                    user.tenant(),
                    user.name(),
                    user.email(),
                    pb::User::Kind_Name(user.kind()),
                    user.active(),
                    user.descr(),
                    user_props);

                LOG_INFO << "User " << cuser
                         << " has created user name=" << user.name() << ", id=" << user.uuid()
                         << ", kind=" << pb::User::Kind_Name(user.kind())
                         << ", tenant=" << user.tenant();
            }

            // TODO: Publish the new tenant and users

            *reply->mutable_tenant() = tenant;

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetUserGlobalSettings(::grpc::CallbackServerContext *ctx,
                                                                           const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            auto res = co_await rctx.dbh->exec(
                "SELECT settings FROM user_settings WHERE user = ?",
                cuser);

            enum Cols { SETTINGS };
            if (!res.rows().empty()) {
                const auto& row = res.rows().front();
                auto blob = row.at(SETTINGS).as_blob();
                pb::UserGlobalSettings settings;
                if (settings.ParseFromArray(blob.data(), blob.size())) {
                    reply->mutable_userglobalsettings()->CopyFrom(settings);
                } else {
                    LOG_WARN_N << "Failed to parse UserGlobalSettings for user " << cuser;
                    throw runtime_error{"Failed to parse UserGlobalSettings"};
                }
            } else {
                reply->set_error(pb::NOT_FOUND);
                reply->set_message("User settings not found");
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SetUserGlobalSettings(::grpc::CallbackServerContext *ctx,
                                                                           const pb::UserGlobalSettings *req,
                                                                           pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            // TODO: Validate the settings

            const auto blob = toBlob(*req);

            auto res = co_await rctx.dbh->exec(
                "INSERT INTO user_settings (user, settings) VALUES (?, ?) "
                "ON DUPLICATE KEY UPDATE settings = ?",
                rctx.uctx->dbOptions(), cuser, blob, blob);

            auto update = newUpdate(res.affected_rows() == 1 /* inserted, 2 == updated */
                                        ? pb::Update::Operation::Update_Operation_ADDED
                                        : pb::Update::Operation::Update_Operation_UPDATED);

            *update->mutable_userglobalsettings() = *req;
            owner_.publish(update);

            *reply->mutable_userglobalsettings() = *req;

            // TODO: Signal other servers that the settings has changed
            owner_.updateSessionSettingsForUser(*req, ctx);

            co_return;
        }, __func__);
}

} // ns
