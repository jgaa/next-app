
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
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            const auto uctx = rctx.uctx;
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

            auto trx = co_await rctx.dbh->transaction();

            co_await owner_.server().db().exec(
                "INSERT INTO tenant (id, name, kind, descr, state, properties) VALUES (?, ?, ?, ?, ?, ?)",
                tenant.uuid(),
                tenant.name(),
                toLower(pb::Tenant::Kind_Name(tenant.kind())),
                tenant.descr(),
                toLower(pb::Tenant::State_Name(tenant.state())),
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

                tenant.add_users()->CopyFrom(user);            }

            // TODO: Publish the creation of tenant and users to ant logged in admins
            // TODO: Add the creations to an event-log available for admins

            *reply->mutable_tenant() = tenant;

            co_await trx.commit();

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateDevice(::grpc::CallbackServerContext *ctx, const pb::CreateDeviceReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            auto device = req->device();

            if (device.csr().empty()) {
                throw db_err{nextapp::pb::Error::MISSING_CSR, "Missing CSR"};
            }

            if (device.uuid().empty()) {
                device.set_uuid(newUuidStr());
            } else {
                validatedUuid(device.uuid());
            }

            // Check if the device already exists
            auto res = co_await rctx.dbh->exec("SELECT user FROM device WHERE id=?", device.uuid());
            if (!res.rows().empty()) {
                const auto uid = res.rows().front().at(0).as_string();

                if (uid != cuser) {
                    LOG_WARN << "Rejecting re-create of device " << device.uuid()
                             << "for user " << uid << " (not the owner)";
                    throw db_err{nextapp::pb::Error::ALREADY_EXIST, "Device already exists"};
                }

                res = co_await rctx.dbh->exec(
                    "SELECT t.id, u.active, t.state FROM tenant t JOIN user u on u.tenant = t.id WHERE u.id=?", device.uuid());
                if (!res.rows().empty()) {
                    enum Cols { TENANT, ACTIVE, STATE };

                    const auto tid = res.rows().front().at(TENANT).as_string();
                    const auto active = res.rows().front().at(ACTIVE).as_int64();
                    const auto state = res.rows().front().at(STATE).as_string();

                    if (state == "pending_activation") {
                        // This is OK. The device is re-created during pre-activation
                        LOG_DEBUG << "Device " << device.uuid() << " is re-created in pending_activation state";
                    } else {
                        LOG_INFO << "Rejecting re-create of device " << device.uuid()
                                 << "for user " << uid << ", tenant" << tid << " in state " << state;
                        throw db_err{nextapp::pb::Error::ALREADY_EXIST, "Device already exists"};
                    }
                }
            }

            pb::CreateDeviceResp resp;
            string cert_hash;
            resp.set_deviceid(device.uuid());
            try {
                const auto cert = owner_.server().ca().signCert(device.csr(), device.uuid(), &cert_hash);
                assert(!cert.cert.empty());
                resp.set_cert(cert.cert);
            } catch (const std::exception& ex) {
                LOG_WARN << "Failed to sign certificate for device " << device.uuid() << ": " << ex.what();
                throw db_err{nextapp::pb::Error::INVALID_CSR, ex.what()};
            }

            // Save the device
            res = co_await rctx.dbh->exec(
                R"(INSERT INTO device
                    (id, user, hostName, os, osVersion, appVersion, productType, productVersion, arch, prettyName, certHash)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                rctx.uctx->dbOptions(),
                device.uuid(), cuser, device.hostname(), device.os(), device.osversion(),
                device.appversion(), device.producttype(), device.productversion(),
                device.arch(), device.prettyname(), toBlob(cert_hash));

            if (auto *response = reply->mutable_createdeviceresp()) {
                response->CopyFrom(resp);
            } else {
                throw runtime_error{"Failed to set response"};
            }
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetUserGlobalSettings(::grpc::CallbackServerContext *ctx,
                                                                           const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            auto res = co_await rctx.dbh->exec(
                "SELECT settings, version FROM user_settings WHERE user = ?",
                cuser);

            enum Cols { SETTINGS, VERSION };
            if (!res.rows().empty()) {
                const auto& row = res.rows().front();
                auto blob = row.at(SETTINGS).as_blob();
                pb::UserGlobalSettings settings;
                if (settings.ParseFromArray(blob.data(), blob.size())) {
                    auto version = row.at(VERSION).as_int64();
                    settings.set_version(version);
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
