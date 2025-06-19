
#include <deque>

#include "shared_grpc_server.h"
#include "nextapp/logging.h"

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
            if (!tenant.has_properties()) {
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

            auto added = co_await owner_.saveUserGlobalSettings(rctx.dbh.value(), *req, rctx);

            auto update = newUpdate(added ? pb::Update::Operation::Update_Operation_ADDED
                                          : pb::Update::Operation::Update_Operation_UPDATED);

            // Re-read from database so we get everything right. The version is updated by a trigger.
            pb::UserGlobalSettings settings;
            co_await owner_.getGlobalSettings(settings, rctx);

            update->mutable_userglobalsettings()->CopyFrom(settings);
            rctx.publishLater(update);

            owner_.sessionManager().setUserSettings(toUuid(cuser), settings);

            co_return;
        }, __func__);
}

boost::asio::awaitable<bool> GrpcServer::saveUserGlobalSettings(
    jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::UserGlobalSettings& settings, RequestCtx& rctx) {

    const auto blob = toBlob(settings);
    auto res = co_await rctx.dbh->exec(
        "INSERT INTO user_settings (user, settings) VALUES (?, ?) "
        "ON DUPLICATE KEY UPDATE settings = ?",
        rctx.uctx->dbOptions(), rctx.uctx->userUuid(), blob, blob);

    co_return res.affected_rows() == 1 ;
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
            resp.set_email(pb_adapt(res.rows().front().front().as_string()));

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

::grpc::ServerWriteReactor<pb::Status> *GrpcServer::NextappImpl::ListTenants(
    ::grpc::CallbackServerContext *ctx, const pb::ListTenantsReq *req)
{
    return writeStreamHandler(ctx, req,
    [this, req, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {
        const auto stream_scope = owner_.server().metrics().data_streams_actions().scoped();
        const auto uctx = rctx.uctx;
        const auto& cuser = uctx->userUuid();

        if (!uctx->isAdmin()) {
            throw server_err{nextapp::pb::Error::PERMISSION_DENIED, "Permission denied"};
        };

        constexpr string_view query = R"(SELECT
    t.id AS tenant_id,
    t.name AS tenant_name,
    t.kind AS tenant_kind,
    t.descr AS tenant_descr,
    t.properties AS tenant_properties,
    t.state AS tenant_state,
    t.system_tenant AS tenant_system_tenant,
    u.id AS user_id,
    u.name AS user_name,
    u.kind AS user_kind,
    u.descr AS user_descr,
    u.active AS user_active,
    u.email AS user_email,
    u.properties AS user_properties,
    u.system_user AS user_system_user
FROM tenant t
LEFT JOIN user u ON t.id = u.tenant
ORDER BY t.id;
)";
        enum Cols {
            TENANT_ID, TENANT_NAME, TENANT_KIND, TENANT_DESCR, TENANT_PROPERTIES, TENANT_STATE, TENANT_SYSTEM_TENANT,
            USER_ID, USER_NAME, USER_KIND, USER_DESCR, USER_ACTIVE, USER_EMAIL, USER_PROPERTIES, USER_SYSTEM_USER
        };

        // Use batched reading from the database, so that we can get all the data, but
        // without running out of memory.
        assert(rctx.dbh);
        co_await  rctx.dbh->start_exec(query, rctx.uctx->dbOptions());

        string current_tenant_id;

        nextapp::pb::Status reply;

        auto *tenant = reply.mutable_tenant();
        auto num_rows_in_batch = 0u;
        auto total_rows = 0u;
        auto batch_num = 0u;

        auto flush = [&]() -> boost::asio::awaitable<void> {
            reply.set_error(::nextapp::pb::Error::OK);
            if (!num_rows_in_batch) {
                co_return;
            }
            assert(reply.has_tenant());
            ++batch_num;
            reply.set_message(format("Fetched {} users for tenant #{}", reply.tenant().users().size(), batch_num));
            co_await stream->sendMessage(std::move(reply), boost::asio::use_awaitable);
            reply.Clear();
            tenant = reply.mutable_tenant();
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
                const auto tenant_id = row.at(TENANT_ID).as_string();
                if (tenant_id != current_tenant_id) {
                    if (!current_tenant_id.empty()) {
                        co_await flush();
                    }
                    // Fill in the tenant-data
                    current_tenant_id = tenant_id;
                    tenant->set_uuid(tenant_id);
                    tenant->set_name(toStringIfValue(row, TENANT_NAME));
                    // kind enum
                    {
                        const auto kind = toUpper((toStringIfValue(row, TENANT_KIND)));
                        pb::Tenant::Kind kind_value{};
                        if (pb::Tenant::Kind_Parse(kind, &kind_value)) {
                            tenant->set_kind(kind_value);
                        }
                    }
                    tenant->set_descr(toStringIfValue(row, TENANT_DESCR));
                    {
                        // Read it from protobuf binary format
                        auto *p = tenant->mutable_properties();
                        if (auto kv = KeyValueFromBlob(row.at(TENANT_PROPERTIES))) {
                            tenant->mutable_properties()->CopyFrom(*kv);
                        }
                    }
                    // state enum
                    {
                        const auto state = toUpper((toStringIfValue(row, TENANT_STATE)));
                        pb::Tenant::State state_value{};
                        if (pb::Tenant::State_Parse(state, &state_value)) {
                            tenant->set_state(state_value);
                        }
                    }
                    auto st = row.at(TENANT_SYSTEM_TENANT);
                    tenant->set_system_tenant(!st.is_null() && st.as_int64() > 0);

                    // End of tenant data
                };

                ++total_rows;
                ++num_rows_in_batch;

                const auto user_id = row.at(USER_ID).as_string();
                if (!user_id.empty()) {
                    // Fill in the user data
                    auto * u = tenant->add_users();
                    u->set_uuid(user_id);
                    u->set_tenant(tenant_id);
                    u->set_name(toStringIfValue(row, USER_NAME));

                    // kind enum
                    {
                        const auto kind = toUpper((toStringIfValue(row, USER_KIND)));
                        pb::User::Kind kind_value{};
                        if (pb::User::Kind_Parse(kind, &kind_value)) {
                            u->set_kind(kind_value);
                        }
                    }
                    u->set_descr(toStringIfValue(row, USER_DESCR));
                    auto active = row.at(USER_ACTIVE);
                    u->set_active(!active.is_null() && active.as_int64() > 0);
                    u->set_email(toStringIfValue(row, USER_EMAIL));
                    {
                        // Read it from protobuf binary format
                        auto *p = u->mutable_properties();
                        if (auto kv = KeyValueFromBlob(row.at(TENANT_PROPERTIES))) {
                            tenant->mutable_properties()->CopyFrom(*kv);
                        }
                    }
                    auto su = row.at(USER_SYSTEM_USER);
                    u->set_system_user(!su.is_null() && su.as_int64() > 0);
                }
            }

        } // read more from db loop

        co_await flush();

        LOG_DEBUG_N << "Sent " << total_rows << " tenants/users to client.";
        co_return;

    }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteAccount(::grpc::CallbackServerContext *ctx,
                                                                   const pb::Empty *req,
                                                                   pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();


            auto res = co_await rctx.dbh->exec(
                "SELECT COUNT(*) FROM user WHERE tenant = ?", rctx.uctx->tenantUuid());
            if (res.rows().empty()) {
                throw server_err{pb::Error::GENERIC_ERROR, "Could not enumerate users for the tenant"};
            }
            const auto num_users = res.rows().front().front().as_int64();
            if (num_users == 1) {
                LOG_INFO_EX(rctx) << "Deleting tenant " << rctx.uctx->tenantUuid()
                                 << " for user " << cuser << ", since it is the last user in the tenant. "
                                 << "The user is deleting their account";

                res = co_await rctx.dbh->exec(
                    "DELETE FROM tenant WHERE id = ?", rctx.uctx->tenantUuid());
                if (res.affected_rows() < 1) {
                    throw server_err{pb::Error::GENERIC_ERROR, "Could not delete tenant (database failure)"};
                }
            } else {
                LOG_INFO_EX(rctx) << "Deleting user " << cuser
                                 << " from tenant " << rctx.uctx->tenantUuid()
                                 << ". The user is deleting their account";
                res = co_await rctx.dbh->exec(
                    "DELETE FROM user WHERE id = ?", cuser);
                if (res.affected_rows() < 1) {
                    throw server_err{pb::Error::GENERIC_ERROR, "Could not delete user (database failure)"};
                }
            }

            auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_DELETED);
            update.set_accountdeleted(true);

            rctx.uctx->setAsInvalid();

            co_return;
        }, __func__);
}

::grpc::ServerReadReactor<pb::ImportDataMsg> *GrpcServer::NextappImpl::ImportData(
    ::grpc::CallbackServerContext *ctx, pb::Status *reply)
{
    return readStreamHandler<pb::ImportDataMsg>(ctx, reply,
    [this, reply, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {

        // Map the old uuid's to new uuids.
        // TODO: For large data-sets, we can't have all the mappings in memory.
        //       We need to use a LRU cache over a kv-store. Alternatively, we cound
        //       send the entire job to another process.
        //       It would also be good to do the entire mapping and validation before
        //       making any changes to the database.
        class Mapper {
            using mapping_t = std::unordered_map<boost::uuids::uuid, boost::uuids::uuid>;
        public:
            Mapper(RequestCtx& rctx) : rctx_{rctx} {}

            string addUser(string_view user) {
                const auto newid = newUuid();
                users_[toUuid(user)] = newid;
                return toString(newid);
            }

            string user(string_view user) {
                const auto uid = toUuid(user);
                if (auto it = users_.find(uid); it != users_.end()) {
                    return toString(it->second);
                }
                LOG_DEBUG_EX(rctx_) << "User " << user << " not found in mapping-table.";
                throw server_err{pb::Error::NOT_FOUND, format("User {} not found in mapping-table.", user)};
            }

            string addActionCategory(string_view category) {
                const auto newid = newUuid();
                LOG_TRACE << "Mapping old category " << category << " to new id " << newid;
                action_categories_[toUuid(category)] = newid;
                return toString(newid);
            }

            string actionCategory(string_view category) {
                const auto uid = toUuid(category);
                if (auto it = action_categories_.find(uid); it != action_categories_.end()) {
                    return toString(it->second);
                }
                LOG_DEBUG_EX(rctx_) << "Action category " << category << " not found in mapping-table.";
                throw server_err{pb::Error::NOT_FOUND, format("Action category {} not found in mapping-table.", category)};
            }

            string addNode(string_view node) {
                const auto newid = newUuid();
                nodes_[toUuid(node)] = newid;
                LOG_TRACE_N << "Mapping old node-id " << node << " to new node-id " << newid;
                return toString(newid);
            }

            string node(string_view node, bool doThrow = true) {
                const auto uid = toUuid(node);
                if (auto it = nodes_.find(uid); it != nodes_.end()) {
                    return toString(it->second);
                }
                if (doThrow) {
                    LOG_DEBUG_EX(rctx_) << "Node " << node << " not found in mapping-table.";
                    throw server_err{pb::Error::NOT_FOUND, format("Node {} not found in mapping-table.", node)};
                }
                return {};
            }

            string node(const boost::uuids::uuid& id) {
                if (auto it = nodes_.find(id); it != nodes_.end()) {
                    return toString(it->second);
                }
                LOG_DEBUG_EX(rctx_) << "Node " << id << " not found in mapping-table.";
                throw server_err{pb::Error::NOT_FOUND, format("Node {} not found in mapping-table.", toString(id))};
            }

            string addAction(string_view action) {
                const auto newid = newUuid();
                actions_[toUuid(action)] = newid;
                return toString(newid);
            }

            string action(string_view action, bool doThrow = true) {
                const auto uid = toUuid(action);
                if (auto it = actions_.find(uid); it != actions_.end()) {
                    return toString(it->second);
                }
                if (doThrow) {
                    LOG_DEBUG_EX(rctx_) << "Action " << action << " not found in mapping-table.";
                    throw server_err{pb::Error::NOT_FOUND, format("Action {} not found in mapping-table.", action)};
                }
                return {};
            }

            string action(const boost::uuids::uuid& uid) {
                if (auto it = actions_.find(uid); it != actions_.end()) {
                    return toString(it->second);
                }
                LOG_DEBUG_EX(rctx_) << "Action " << uid << " not found in mapping-table.";
                throw server_err{pb::Error::NOT_FOUND, format("Action {} not found in mapping-table.", toString(uid))};
            }

            string addWorkSession(string_view session) {
                const auto newid = newUuid();
                work_sessions_[toUuid(session)] = newid;
                return toString(newid);
            }

            string workSession(string_view session) {
                const auto uid = toUuid(session);
                if (auto it = work_sessions_.find(uid); it != work_sessions_.end()) {
                    return toString(it->second);
                }
                LOG_DEBUG_EX(rctx_) << "Work session " << session << " not found in mapping-table.";
                throw server_err{pb::Error::NOT_FOUND, format("Work session {} not found in mapping-table.", session)};
            }

            string addTimeBlock(string_view timeblock) {
                const auto newid = newUuid();
                timeblocks_[toUuid(timeblock)] = newid;
                return toString(newid);
            }

            string timeblock(string_view timeblock) {
                const auto uid = toUuid(timeblock);
                if (auto it = timeblocks_.find(uid); it != timeblocks_.end()) {
                    return toString(it->second);
                }
                LOG_DEBUG_EX(rctx_) << "Timeblock " << timeblock << " not found in mapping-table.";
                throw server_err{pb::Error::NOT_FOUND, format("Timeblock {} not found in mapping-table.", timeblock)};
            }

        private:
            mapping_t users_;
            mapping_t action_categories_;
            mapping_t nodes_;
            mapping_t actions_;
            mapping_t work_sessions_;
            mapping_t timeblocks_;
            const RequestCtx& rctx_;
        }

        mapper{rctx};
        deque<pair<boost::uuids::uuid, boost::uuids::uuid>> node_parent;
        deque<pair<boost::uuids::uuid, boost::uuids::uuid>> action_origin;

        // Delete the users current data
        const auto& cuser = rctx.uctx->userUuid();

        auto msg = co_await stream->read();
        if (!msg->has_request()) {
            LOG_WARN_N << "Missing request in ImportData stream for user " << cuser;
            throw server_err{pb::Error::GENERIC_ERROR, "Missing request in ImportData stream"};
        }

        auto clear_user_data = [&] () -> boost::asio::awaitable<void> {
            co_await rctx.dbh->exec("DELETE FROM time_block WHERE user = ?", cuser);
            co_await rctx.dbh->exec("DELETE FROM action WHERE user = ?", cuser);
            co_await rctx.dbh->exec("DELETE FROM node WHERE user = ?", cuser);
            co_await rctx.dbh->exec("DELETE FROM day WHERE user = ?", cuser);
            co_await rctx.dbh->exec("DELETE FROM request_state WHERE userid = ?", cuser);
            co_await rctx.dbh->exec("DELETE FROM action_category WHERE user = ?", cuser);
            co_await rctx.dbh->exec("DELETE FROM user_settings WHERE user = ?", cuser);
        };

        co_await clear_user_data();

        std::exception_ptr eptr;

        try {
            while(true) {
                auto msg = co_await stream->read();
                if (!msg) {
                    // Unexpected end of stream
                    LOG_WARN_N << "Unexpected end of stream while importing data for user " << cuser;
                    co_await clear_user_data();
                    throw server_err{pb::Error::GENERIC_ERROR, "Unexpected end of stream"};
                }

                if (!action_origin.empty() && !msg->has_actions()) {
                    auto sql = "UPDATE action SET origin=? WHERE id=?";

                    auto generator = jgaa::mysqlpool::BindTupleGenerator(action_origin,
                                                                         [&] (const auto& ao) {
                                                                             return make_tuple (
                                                                                 toString(ao.first),
                                                                                 mapper.action(ao.second)
                                                                                 );
                                                                         });
                    try {
                        co_await rctx.dbh->exec(sql, generator);
                    } catch (const std::exception& ex) {
                        LOG_ERROR_EX(rctx) << "Failed to update actions origin: " << ex.what();
                        throw server_err{pb::Error::GENERIC_ERROR, "Failed to actions origin"};
                    }
                    action_origin.clear();
                };

                if (msg->has_user()) {
                    const auto& u = msg->user();
                    mapper.addUser(u.uuid());
                    LOG_TRACE_N << "Importing user " << u.uuid() << " as user " << cuser;
                } else if (msg->has_userglobalsettings()) {
                    auto res = owner_.saveUserGlobalSettings(rctx.dbh.value(), msg->userglobalsettings(), rctx);
                } else if (msg->has_daycolordefinitions()) {
                    // Currently set globally
                    //co_await owner_.saveDayColorDefinitions(rctx.dbh.value(), msg->daycolordefinitions(), rctx);
                } else if (msg->has_actioncategories()) {
                    if (auto *items = msg->mutable_actioncategories()) {
                        if (auto *rows = items->mutable_categories()) {
                            for(auto& item : *rows) {
                                item.set_id(mapper.addActionCategory(item.id()));
                            }
                            co_await owner_.saveActionCategories(rctx.dbh.value(), *items, rctx);
                        }
                    }
                } else if (msg->has_days()) {
                    co_await owner_.saveDays(rctx.dbh.value(), msg->days(), rctx);
                } else if (msg->has_nodes()) {
                    if (auto *items = msg->mutable_nodes()) {
                        if (auto *rows = items->mutable_nodes()) {
                            for(auto& item : *rows) {
                                item.set_user(mapper.user(item.user()));
                                item.set_uuid(mapper.addNode(item.uuid()));
                                if (!item.parent().empty()) {
                                    const auto parent = mapper.node(item.parent(), false);
                                    if (parent.empty()) {
                                        // Delay until all nodes are imported
                                        node_parent.emplace_back(toUuid(item.uuid()), toUuid(item.parent()));
                                    } else {
                                        item.set_parent(parent);
                                    }
                                }
                            }

                            co_await owner_.saveNodes(rctx.dbh.value(), msg->nodes(), rctx);
                        }
                    }
                } else if (msg->has_actions()) {
                    if (auto *items = msg->mutable_actions()) {
                        if (auto *rows = items->mutable_actions()) {
                            for(auto& item : *rows) {
                                item.set_id(mapper.addAction(item.id()));
                                item.set_node(mapper.node(item.node()));
                                if (item.has_origin() && !item.origin().empty()) {
                                    auto origin = mapper.action(item.origin(), false);
                                    if (!origin.empty()) {
                                        item.set_origin(mapper.action(item.origin()));
                                    } else {
                                        // Delay until all actions are imported
                                        action_origin.emplace_back(toUuid(item.id()), toUuid(item.origin()));
                                    }
                                }
                                if (!item.category().empty()) {
                                    item.set_category(mapper.actionCategory(item.category()));
                                }
                            }
                            co_await owner_.saveActions(rctx.dbh.value(), msg->actions(), rctx);
                        }
                    }
                } else if (msg->has_worksessions()) {
                    if (auto * item = msg->mutable_worksessions() ) {
                        if (auto * rows = item->mutable_sessions()) {
                            for(auto& ws : *rows) {
                                ws.set_id(mapper.addWorkSession(ws.id()));
                                ws.set_user(mapper.user(ws.user()));
                                ws.set_action(mapper.action(ws.action()));
                            }
                            co_await owner_.saveWorkSessions(rctx.dbh.value(), msg->worksessions(), rctx);
                        }
                    }
                } else if (msg->has_timeblocks()) {
                    if (auto * items = msg->mutable_timeblocks()) {
                        if (auto *rows = items->mutable_blocks()) {
                            for(auto& tb : *rows) {
                                tb.set_id(mapper.addTimeBlock(tb.id()));
                                tb.set_user(mapper.user(tb.user()));
                                tb.set_category(mapper.actionCategory(tb.category()));

                                // The actions in a time-block are just a list of uuid-strings.
                                pb::StringList actions;
                                if (auto *adder = actions.mutable_list()) {
                                    for(const auto& action : tb.actions().list()) {
                                        auto mapped_action = mapper.action(action, false);
                                        if (!mapped_action.empty()) {
                                            adder->Add(std::move(mapped_action));
                                        } else {
                                            LOG_DEBUG_EX(rctx) << "Action " << action << " not found in mapping-table while adding time-block actions. Skipping.";
                                        }
                                    }
                                }
                                if (auto *a = tb.mutable_actions() ) {
                                    a->CopyFrom(std::move(actions));
                                };
                            }

                            co_await owner_.saveTimeBlocks(rctx.dbh.value(), msg->timeblocks(), rctx);
                        }
                    }
                } else if (msg->has_completed()) {
                    // We'll get to it...
                } else {
                    LOG_WARN_N << "Unexpected message type in ImportData stream for user " << cuser
                               << ", what=" << static_cast<int>(msg->what_case());

                    throw server_err{pb::Error::GENERIC_ERROR,
                                     format("Unexpected message type {} in ImportData stream",
                                            static_cast<int>(msg->what_case()))};
                }

                if (msg->has_completed()) {
                    if (!msg->completed()) {
                        LOG_INFO_N << "ImportData stream for user " << cuser
                                   << " was aborted by the user.";
                        co_await clear_user_data();
                    }
                    auto last = co_await stream->read();
                    if (last) {
                        LOG_WARN_N << "Unexpected message after completed in ImportData stream for user " << cuser
                                   << ", what=" << static_cast<int>(last->what_case());
                    }

                    LOG_DEBUG_EX(rctx) << "ImportData stream completed successfully. Now proceeding with post-linking steps...";
                    break;
                }
            } // while ...

            // Link nodes that failed early lookup to their parents
            if (!node_parent.empty()) {
                auto sql = "UPDATE node SET parent=? WHERE id=?";

                auto generator = jgaa::mysqlpool::BindTupleGenerator(node_parent,
                    [&] (const auto& np) {
                        return make_tuple (
                            mapper.node(np.second),
                            toString(np.first)
                        );
                    });

                try {
                    co_await rctx.dbh->exec(sql, generator);
                } catch (const std::exception& ex) {
                    LOG_ERROR_EX(rctx) << "Failed to update node parents: " << ex.what();
                    throw server_err{pb::Error::GENERIC_ERROR, "Failed to update node parents"};
                }
            }
        } catch (const exception& ex) {
            LOG_DEBUG_EX(rctx) << "Exception while importing data: " << ex.what();
            eptr = std::current_exception();
        }

        if (eptr) {
            co_await clear_user_data();
            std::rethrow_exception(eptr);
        }

        co_return;
    }, __func__);
}

::grpc::ServerWriteReactor<pb::Status> *GrpcServer::NextappImpl::ExportData(
    ::grpc::CallbackServerContext *ctx, const pb::ExportDataReq *req)
{
    return writeStreamHandler(ctx, req,
    [this, req, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {

        pb::Status msg;
        assert(rctx.dbh);
        auto &dbh = rctx.dbh.value();

        auto flush = [&](pb::Status& status) -> boost::asio::awaitable<void> {
            co_await stream->sendMessage(std::move(status), boost::asio::use_awaitable);
        };

        // User account data
        msg.Clear();
        msg.set_error(pb::Error::OK);
        msg.mutable_user()->CopyFrom(co_await owner_.getUser(dbh, rctx.uctx->userUuid()));
        co_await stream->sendMessage(std::move(msg), boost::asio::use_awaitable);

        // Global settings
        msg.Clear();
        msg.set_error(pb::Error::OK);
        msg.mutable_userglobalsettings()->CopyFrom(rctx.uctx->settings());
        co_await stream->sendMessage(std::move(msg), boost::asio::use_awaitable);

        // Green day colors
        msg.Clear();
        msg.set_error(pb::Error::OK);
        msg.mutable_daycolordefinitions()->CopyFrom(
            co_await owner_.getDayColorDefinitions(dbh, rctx.uctx->tenantUuid()));

        // Green days
        co_await owner_.exportDays(0, dbh, flush, rctx);

        // Nodes
        co_await owner_.exportNodes(0, dbh, flush, rctx, true);

        // Categories
        msg.Clear();
        msg.set_error(pb::Error::OK);
        msg.mutable_actioncategories()->CopyFrom(
            co_await owner_.getActionCategories(dbh, rctx.uctx->userUuid()));
        co_await stream->sendMessage(std::move(msg), boost::asio::use_awaitable);

        // Actions
        co_await owner_.exportActions(0, dbh, flush, rctx, true);

        // Work sessions
        co_await owner_.exportWork(0, dbh, flush, rctx, true);

        // Time blocks
        co_await owner_.exportTimeBlocks(0, dbh, flush, rctx, true);

        msg.Clear();
        msg.set_error(pb::Error::OK);
        msg.set_hasmore(false); // No more messages to send
        co_await stream->sendMessage(std::move(msg), boost::asio::use_awaitable);

    }, __func__);
}


boost::asio::awaitable<pb::User> GrpcServer::getUser(jgaa::mysqlpool::Mysqlpool::Handle& dbh, std::string_view uuid) {

    enum Cols { ID, TENANT, NAME, EMAIL, KIND, ACTIVE, DESCR, PROPERTIES, SYSTEM_USER };
    auto res = co_await dbh.exec("SELECT id, tenant, name, email, kind, active, descr, properties, system_user FROM user WHERE id = ?",
                                 uuid);
    if (res.rows().empty()) {
        throw server_err{pb::Error::NOT_FOUND, "User not found"};
    }

    pb::User u;
    const auto& row = res.rows().front();
    u.set_uuid(row.at(ID).as_string());
    u.set_tenant(row.at(TENANT).as_string());
    u.set_name(row.at(NAME).as_string());
    u.set_email(row.at(EMAIL).as_string());
    pb::User::Kind kind_value{};
    if (pb::User::Kind_Parse(toUpper(row.at(KIND).as_string()), &kind_value)) {
        u.set_kind(kind_value);
    }
    u.set_active(row.at(ACTIVE).as_int64() > 0);
    u.set_descr(row.at(DESCR).as_string());
    if (auto kv = KeyValueFromBlob(row.at(PROPERTIES))) {
        u.mutable_properties()->CopyFrom(*kv);
    }
    if (row.at(SYSTEM_USER).is_int64()) {
        u.set_system_user(row.at(SYSTEM_USER).as_int64() > 0);
    }
    co_return u;
}

} // ns
