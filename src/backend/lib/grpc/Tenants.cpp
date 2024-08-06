
#include "shared_grpc_server.h"

namespace nextapp::grpc {

namespace {

string getOtpHash(string_view user, string_view uuid, string_view otp)
{
    return sha256(format("{}/{}/{}",user, uuid, otp), true);
}

} // anon ns

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateTenant(::grpc::CallbackServerContext *ctx,
                                                                  const pb::CreateTenantReq *req,
                                                                  pb::Status *reply)
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

            if (!uctx->isAdmin()) {
                // TODO: Enable when sessions works
                //throw server_err{nextapp::pb::Error::PERMISSION_DENIED, "Permission denied"};
            }

            pb::Tenant tenant{req->tenant()};

            if (tenant.uuid().empty()) {
                tenant.set_uuid(newUuidStr());
            }
            if (tenant.properties().empty()) {
                tenant.mutable_properties();
            }

            // TODO: Serialize this so that only one operation on one Tenant can be doing at a time.
            //       We don't want a race-condition where two users are contending to create the same tenant name or email name.

            // See if the tenant name or any emails are already in use.
            auto res = co_await rctx.dbh->exec(
                "SELECT id, state FROM tenant WHERE name=?",
                rctx.uctx->dbOptions(), tenant.name());
            if (!res.rows().empty()) {
                enum Cols {ID, STATE};
                pb::Tenant::State state = pb::Tenant::State::Tenant_State_SUSPENDED;
                auto tid = res.rows().front().at(ID).as_string();
                pb::Tenant::State_Parse(res.rows().front().at(STATE).as_string(), &state);
                if (state == pb::Tenant::State::Tenant_State_PENDING_ACTIVATION) {
                    // Remove it.
                    LOG_DEBUG << "Removing tenant " << tid << " in pending state because of new tenant creation re-using name "
                              << tenant.name();
                    co_await rctx.dbh->exec("DELETE FROM tenant WHERE id = ?", tid);
                }
            }

            set<string> removed_tenants;
            for(const auto& u : req->users()) {
                res = co_await rctx.dbh->exec(
                    "SELECT t.id, t.state, u.id FROM user u JOIN tenant t on t.id = u.tenant where u.email=?",
                        rctx.uctx->dbOptions(), u.email());
                for(auto r : res.rows()) {
                    enum Cols { TENANT, STATE, USER };
                    auto tid = r.at(TENANT).as_string();
                    pb::Tenant::State state = pb::Tenant::State::Tenant_State_SUSPENDED;
                    const auto state_name = r.at(STATE).as_string();
                    pb::Tenant::State_Parse(toUpper(state_name), &state);
                    auto uid = r.at(USER).as_string();
                    if (state == pb::Tenant::State::Tenant_State_PENDING_ACTIVATION) {
                        if (removed_tenants.insert(tid).second) {
                            // Remove it.
                            LOG_DEBUG << "Removing tenant " << tid << " in pending state because of new tenant creation re-using email "
                                      << u.email();
                            co_await rctx.dbh->exec("DELETE FROM tenant WHERE id = ?", tid);
                        }
                    } else {
                        throw server_err{nextapp::pb::Error::ALREADY_EXIST, "User already exists"};
                    }
                }
            }


            const auto properties = toJson(*tenant.mutable_properties());
            if (!tenant.has_kind()) {
                tenant.set_kind(pb::Tenant::Tenant::Kind::Tenant_Kind_GUEST);
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
                    user.set_kind(pb::User::Kind::User_Kind_REGULAR);
                } else if (kind == pb::User::Kind::User_Kind_SUPER) {
                    if (tenant.kind() == pb::Tenant::Kind::Tenant_Kind_SUPER) {
                        throw server_err{nextapp::pb::Error::PERMISSION_DENIED,
                                         "Only Super Tenants can create Super users!"};
                    }
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

            // TODO: Publish the creation of tenant and users to and logged in admins
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

            // NB: This is the userid for the admin-account that is creating the device
            const auto& cuser = rctx.uctx->userUuid();

            if (!rctx.uctx->isAdmin()) {
                throw server_err{nextapp::pb::Error::PERMISSION_DENIED, "Only Admin users can create devices"};
            }

            auto device = req->device();

            if (device.csr().empty()) {
                throw server_err{nextapp::pb::Error::MISSING_CSR, "Missing CSR"};
            }

            if (device.uuid().empty()) {
                device.set_uuid(newUuidStr());
            } else {
                validatedUuid(device.uuid());
            }

            // This is the user_id for the user owning the device
            string user_id;
            {
                if (req->has_userid()) {
                    user_id = req->userid();
                } else if (req->has_otpauth()) {
                    const auto& auth = req->otpauth();
                    if (!isValidEmail(req->otpauth().email())) {
                        throw server_err{pb::Error::CONSTRAINT_FAILED, "Invalid email"};
                    }
                    auto res = co_await rctx.dbh->exec(
                        "SELECT id, user, otp_hash FROM otp WHERE email=? AND kind='new_device'",
                        auth.email());
                    if (res.rows().empty()) {
                        LOG_DEBUG << "No 'new_device' OTP found for email " << auth.email();
                        throw server_err{pb::Error::AUTH_FAILED, "Invalid OTP"};
                    }
                    enum Cols { ID, USER, OTP_HASH };
                    const auto& row = res.rows().front();
                    const auto otp_hash = row.at(OTP_HASH).as_string();
                    user_id = row.at(USER).as_string();
                    const auto id = row.at(ID).as_string();

                    const auto hash = getOtpHash(user_id, id, auth.otp());
                    if (hash != otp_hash) {
                        LOG_INFO << "Invalid OTP for email " << auth.email();
                        throw server_err{pb::Error::AUTH_FAILED, "Invalid OTP"};
                    }

                    co_await rctx.dbh->exec("DELETE FROM otp WHERE id=?", id);

                } else {
                    throw server_err{pb::Error::MISSING_AUTH, "Missing User ID or OTP Auth"};
                }
            }

            if (user_id.empty()) {
                throw server_err{pb::Error::MISSING_USER_ID, "Could not determine user ID"};
            }

            validatedUuid(user_id);

            // Check if the device already exists
            auto res = co_await rctx.dbh->exec("SELECT user FROM device WHERE id=?", device.uuid());
            if (!res.rows().empty()) {
                const auto uid = res.rows().front().at(0).as_string();

                if (uid != cuser) {
                    LOG_WARN << "Rejecting re-create of device " << device.uuid()
                             << "for user " << uid << " (not the owner)";
                    throw server_err{nextapp::pb::Error::ALREADY_EXIST, "Device already exists"};
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
                        co_await rctx.dbh->exec("DELETE FROM device WHERE id=?", device.uuid());
                    } else {
                        LOG_INFO << "Rejecting re-create of device " << device.uuid()
                                 << "for user " << uid << ", tenant" << tid << " in state " << state;
                        throw server_err{nextapp::pb::Error::ALREADY_EXIST, "Device already exists"};
                    }
                }
            }

            pb::CreateDeviceResp resp;
            string cert_hash;
            resp.set_deviceid(device.uuid());
            resp.set_cacert(owner_.server().ca().rootCert());
            try {
                const auto cert = owner_.server().ca().signCert(device.csr(), device.uuid(), &cert_hash);
                assert(!cert.cert.empty());
                resp.set_cert(cert.cert);
            } catch (const std::exception& ex) {
                LOG_WARN << "Failed to sign certificate for device " << device.uuid() << ": " << ex.what();
                throw server_err{nextapp::pb::Error::INVALID_CSR, ex.what()};
            }

            // Save the device
            res = co_await rctx.dbh->exec(
                R"(INSERT INTO device
                    (id, user, hostName, os, osVersion, appVersion, productType, productVersion, arch, prettyName, certHash, name)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?))",
                rctx.uctx->dbOptions(),
                device.uuid(), user_id, device.hostname(), device.os(), device.osversion(),
                device.appversion(), device.producttype(), device.productversion(),
                device.arch(), device.prettyname(), toBlob(cert_hash), device.name());

            if (auto *response = reply->mutable_createdeviceresp()) {
                response->CopyFrom(resp);
            } else {
                throw runtime_error{"Failed to set response"};
            }
    }, __func__);
}

boost::asio::awaitable<void> GrpcServer::getGlobalSettings(pb::UserGlobalSettings &settings, RequestCtx &rctx)
{
    const auto& cuser = rctx.uctx->userUuid();

    auto res = co_await rctx.dbh->exec(
        "SELECT settings, version FROM user_settings WHERE user = ?",
        cuser);

    enum Cols { SETTINGS, VERSION };
    if (!res.rows().empty()) {
        const auto& row = res.rows().front();
        auto blob = row.at(SETTINGS).as_blob();
        if (settings.ParseFromArray(blob.data(), blob.size())) {
            auto version = row.at(VERSION).as_int64();
            settings.set_version(version);
        } else {
            LOG_WARN_N << "Failed to parse UserGlobalSettings for user " << cuser;
            throw runtime_error{"Failed to parse UserGlobalSettings"};
        }
    } else {
        throw server_err{pb::Error::NOT_FOUND, "User settings not found"};
    }

    co_return;
}


::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetUserGlobalSettings(::grpc::CallbackServerContext *ctx,
                                                                           const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            // const auto& cuser = rctx.uctx->userUuid();

            pb::UserGlobalSettings settings;
            co_await owner_.getGlobalSettings(settings, rctx);
            reply->mutable_userglobalsettings()->CopyFrom(settings);
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
                "SELECT version FROM user_settings where user = ?", cuser);
            if (!res.rows().empty()) {
                const auto old_version = res.rows().front().front().as_int64();
                if (old_version > req->version()) {
                    throw server_err{pb::Error::CONFLICT,
                                     format("Existing version {} is higher or equal than/to the 'new' version {}.",
                                            old_version, req->version())};
                    co_return;
                }
            }

            res = co_await rctx.dbh->exec(
                "INSERT INTO user_settings (user, settings) VALUES (?, ?) "
                "ON DUPLICATE KEY UPDATE settings = ?",
                rctx.uctx->dbOptions(), cuser, blob, blob);

            auto update = newUpdate(res.affected_rows() == 1 /* inserted, 2 == updated */
                                        ? pb::Update::Operation::Update_Operation_ADDED
                                        : pb::Update::Operation::Update_Operation_UPDATED);

            // Rr-read from database so we get everything right. The version is updated by a trigger.
            pb::UserGlobalSettings settings;
            co_await owner_.getGlobalSettings(settings, rctx);

            update->mutable_userglobalsettings()->CopyFrom(settings);
            rctx.publishLater(update);

            reply->mutable_userglobalsettings()->CopyFrom(settings);

            // TODO: Cluster: Signal other servers that the settings has changed
            owner_.sessionManager().setUserSettings(toUuid(cuser), settings);

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetOtpForNewDevice(
    ::grpc::CallbackServerContext *ctx, const pb::OtpRequest *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            // Delete any existing OTP for this user
            auto res = co_await rctx.dbh->exec(
                "DELETE from otp WHERE user = ?", cuser);

            res = co_await rctx.dbh->exec(
                "SELECT email FROM user WHERE id = ?", cuser);
            if (res.rows().empty()) {
                throw server_err{pb::Error::MISSING_USER_EMAIL, "Email for the current user was not found"};
            }

            pb::OtpResponse resp;
            resp.set_email(res.rows().front().front().as_string());

            // Generate a new OTP
            const auto otp = getRandomStr(8, "0123456789");
            const auto uuid = newUuidStr();
            auto hash = getOtpHash(cuser, uuid, otp);
            co_await rctx.dbh->exec(
                "INSERT INTO otp (id, user, otp_hash, email, kind) VALUES (?, ?, ?, ?, 'new_device') ",
                rctx.uctx->dbOptions(), uuid, cuser, hash, resp.email());

            LOG_DEBUG << "Generated OTP, for new device, with hash " << hash << ", for user " << cuser;
            resp.set_otp(otp);
            reply->mutable_otpresponse()->CopyFrom(resp);

            co_return;
        }, __func__);
}


} // ns
