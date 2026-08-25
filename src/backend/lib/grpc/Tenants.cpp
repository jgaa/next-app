
#include <deque>
#include <array>

#include <boost/asio/co_spawn.hpp>

#include <boost/url.hpp>

#include "nextapp/Plans.h"
#include "payments/v1/payments.pb.h"
#include "payments/v1/payments.grpc.pb.h"
#include "shared_grpc_server.h"
#include "nextapp/logging.h"

using namespace std;
using namespace std::string_literals;

namespace nextapp::grpc {

namespace {

constexpr auto otp_ttl_minutes = 15;

string getOtpHash(string_view user, string_view uuid, string_view otp)
{
    return sha256(format("{}/{}/{}",user, uuid, otp), true);
}

std::array<pb::ActionCategory, 4> getDefaultActionCategories()
{
    std::array<pb::ActionCategory, 4> categories;

    categories[0].set_color("dodgerblue");
    categories[0].set_name("Work");

    categories[1].set_color("yellow");
    categories[1].set_name("Private");

    categories[2].set_color("green");
    categories[2].set_name("Hobby");

    categories[3].set_color("wheat");
    categories[3].set_name("Family");

    return categories;
}

uint32_t getUint32(const boost::mysql::field_view& field)
{
    if (field.is_uint64()) {
        return static_cast<uint32_t>(field.as_uint64());
    }
    if (field.is_int64()) {
        const auto value = field.as_int64();
        if (value < 0) {
            throw runtime_error{"Expected non-negative integer from database"};
        }
        return static_cast<uint32_t>(value);
    }
    throw runtime_error{"Expected integer value from database"};
}

void setUnixTimeIfPresent(common::Time* time, const boost::mysql::field_view& field)
{
    if (field.is_datetime()) {
        time->set_unixtime(toTimeT(field.as_datetime()));
    }
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

    return mutatingUnaryHandler(ctx, req, reply,
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
                (void)pb::Tenant::State_Parse(res.rows().front().at(STATE).as_string(), &state);
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
                    (void)pb::Tenant::State_Parse(toUpper(state_name), &state);
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

            std::optional<string> plan;
            std::optional<string> trial_end;
            if (owner_.server().config().payment.enable_plan) {
                if (auto p = owner_.server().plans()) {
                    auto [pfs, is_trial] = p->getPlanForSignup();
                    plan = pfs;
                    LOG_TRACE_N << "Assigning plan " << (plan ? *plan : "[NULL]"s) << " to tenant " << tenant.name() << " during creation";

                    if (is_trial) {
                        if (auto pc = p->activePlans()) {
                            if (pc->trial_days > 0) {
                                auto trial_end_time = std::chrono::system_clock::now() + std::chrono::hours(24 * pc->trial_days);
                                std::time_t trial_end_time_t = std::chrono::system_clock::to_time_t(trial_end_time);
                                //Round up to midnight on the last day
                                trial_end_time_t = ((trial_end_time_t + 86399) / 86400) * 86400;
                                trial_end = toAnsiTime(trial_end_time_t);
                                LOG_TRACE_N << "Setting trial end to " << *trial_end << " for tenant " << tenant.name() << " during creation";
                            }
                        } else {
                            assert(false); // This should not happen, because getPlanForSignup should throw if there is no active plan for signup, and if there is an active plan for signup, there should be an active plans snapshot.
                        }
                    } // is_trial
                } else {
                    LOG_ERROR_N << "Payment plans are enabled but failed to load plans from payment service. No plan will be assigned to tenant " << tenant.uuid() << " during creation.";
                }
            }

            co_await owner_.server().db().exec(
                "INSERT INTO tenant (id, name, kind, descr, state, registration_state, properties, plan, plan_expires, next_registration_retry) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                tenant.uuid(),
                tenant.name(),
                toLower(pb::Tenant::Kind_Name(tenant.kind())),
                tenant.descr(),
                toLower(pb::Tenant::State_Name(tenant.state())),
                owner_.server().config().payment.enable_plan ? "pending_reg" : "local_only",
                properties,
                plan,
                trial_end,
                owner_.server().config().payment.enable_plan ? std::optional<std::string>{toAnsiTime(std::time(nullptr))} : std::optional<std::string>{});

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

                LOG_DEBUG << "Creating default action categories for user " << user.uuid();
                for (const auto& category : getDefaultActionCategories()) {
                    [[maybe_unused]] auto created =
                        co_await owner_.addActionCategory(*rctx.dbh, user.uuid(), category);
                }

                tenant.add_users()->CopyFrom(user);            }

            // TODO: Publish the creation of tenant and users to and logged in admins
            // TODO: Add the creations to an event-log available for admins

            *reply->mutable_tenant() = tenant;

            co_await trx.commit();

            if (owner_.server().config().payment.enable_plan) {
                const auto tenant_id = toUuid(tenant.uuid());
                boost::asio::co_spawn(owner_.server().ctx(),
                    [plans = owner_.server().plans(), tenant_id]() -> boost::asio::awaitable<void> {
                        try {
                            co_await plans->queueTenantRegistration(tenant_id);
                        } catch (const std::exception& ex) {
                            LOG_WARN_N << "Failed to queue tenant registration for tenant "
                                       << tenant_id << ": " << ex.what();
                        }
                    },
                    boost::asio::detached);
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SetTenantState(::grpc::CallbackServerContext *ctx,
                                                                    const pb::SetTenantStateReq *req,
                                                                    pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
        [this, req, ctx](pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            if (req->state() == pb::Tenant::State::Tenant_State_PENDING_ACTIVATION) {
                throw server_err{pb::Error::INVALID_REQUEST, "PENDING_ACTIVATION cannot be set via SetTenantState"};
            }

            string selector_desc;
            boost::mysql::results res;
            constexpr string_view tenant_cols =
                "t.id, t.name, t.kind, t.descr, t.state, t.properties, t.system_tenant";

            switch (req->what_case()) {
            case pb::SetTenantStateReq::WhatCase::kUuid:
                selector_desc = req->uuid().uuid();
                res = co_await rctx.dbh->exec(
                    format("SELECT {} FROM tenant t WHERE t.id = ?", tenant_cols),
                    rctx.uctx->dbOptions(),
                    selector_desc);
                break;
            case pb::SetTenantStateReq::WhatCase::kUserEmail:
                selector_desc = req->useremail();
                res = co_await rctx.dbh->exec(
                    format("SELECT {} FROM tenant t JOIN user u ON u.tenant = t.id WHERE u.email = ? LIMIT 1", tenant_cols),
                    rctx.uctx->dbOptions(),
                    selector_desc);
                break;
            case pb::SetTenantStateReq::WhatCase::WHAT_NOT_SET:
                throw server_err{pb::Error::INVALID_REQUEST, "Missing tenant selector"};
            }

            if (res.rows().empty()) {
                throw server_err{pb::Error::NOT_FOUND, "Tenant not found"};
            }

            enum Cols { TENANT_ID, TENANT_NAME, TENANT_KIND, TENANT_DESCR, TENANT_STATE, TENANT_PROPERTIES, TENANT_SYSTEM_TENANT };
            const auto& row = res.rows().front();
            pb::Tenant tenant;
            tenant.set_uuid(toStringIfValue(row, TENANT_ID));
            tenant.set_name(toStringIfValue(row, TENANT_NAME));
            tenant.set_descr(toStringIfValue(row, TENANT_DESCR));
            if (auto kv = KeyValueFromBlob(row.at(TENANT_PROPERTIES))) {
                tenant.mutable_properties()->CopyFrom(*kv);
            }
            tenant.set_system_tenant(!row.at(TENANT_SYSTEM_TENANT).is_null() && row.at(TENANT_SYSTEM_TENANT).as_int64() > 0);

            pb::Tenant::Kind kind{};
            if (pb::Tenant::Kind_Parse(toUpper(toStringIfValue(row, TENANT_KIND)), &kind)) {
                tenant.set_kind(kind);
            }

            pb::Tenant::State current_state{};
            if (!pb::Tenant::State_Parse(toUpper(toStringIfValue(row, TENANT_STATE)), &current_state)) {
                throw runtime_error{"Failed to parse tenant state from database"};
            }

            if (current_state == req->state()) {
                tenant.set_state(current_state);
                reply->mutable_tenant()->CopyFrom(tenant);
                co_return;
            }

            tenant.set_state(req->state());
            co_await rctx.dbh->exec(
                "UPDATE tenant SET state = ? WHERE id = ?",
                rctx.uctx->dbOptions(),
                toLower(pb::Tenant::State_Name(req->state())),
                tenant.uuid());

            const bool has_active_sessions = co_await owner_.sessionManager().applyTenantStateAndPublish(tenant.uuid(), tenant);

            if (req->state() == pb::Tenant::State::Tenant_State_SUSPENDED && has_active_sessions) {
                pb::Notification notification;
                notification.mutable_uuid()->set_uuid(newUuidStr());
                notification.mutable_totenant()->set_uuid(tenant.uuid());
                notification.set_subject("Tenant suspended");
                notification.set_message("This tenant has been suspended. Existing sessions are now in read-only mode.");
                notification.set_sendertype(pb::Notification::SenderType::Notification_SenderType_SYSTEM);
                notification.set_senderid(Server::instance().serverId());
                notification.set_kind(pb::Notification::Kind::Notification_Kind_WARNING);

                const string sender_type = toLower(pb::Notification::SenderType_Name(notification.sendertype()));
                const string kind_name = toLower(pb::Notification::Kind_Name(notification.kind()));
                auto ins = co_await rctx.dbh->exec(R"(INSERT INTO notification
                    (valid_to, subject, message, sender_type, sender_id, to_tenant, to_user, uuid, kind, data)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?))",
                    rctx.uctx->dbOptions(),
                    std::optional<std::string>{},
                    notification.subject(),
                    notification.message(),
                    sender_type,
                    notification.senderid(),
                    std::optional<string>{tenant.uuid()},
                    std::optional<string>{},
                    notification.uuid().uuid(),
                    kind_name,
                    notification.data());

                notification.set_id(ins.last_insert_id());
                enum NotificationCols { UPDATED, CREATED_TIME };
                auto updated_res = co_await rctx.dbh->exec(
                    "SELECT updated, created_time FROM notification WHERE id=?",
                    rctx.uctx->dbOptions(),
                    notification.id());
                notification.set_updated(toMsTimestamp(updated_res.rows().front().at(UPDATED).as_datetime(), rctx.uctx->tz()));
                notification.mutable_createdtime()->set_unixtime(
                    toTimeT(updated_res.rows().front().at(CREATED_TIME).as_datetime(), rctx.uctx->tz()));

                owner_.setLastNotificationUpdated(notification.updated());
                co_await owner_.sessionManager().publishNotification(notification);
            }

            reply->mutable_tenant()->CopyFrom(tenant);
            co_return;
        }, __func__, true /* allow new session */, true /* admin only */);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateDevice(::grpc::CallbackServerContext *ctx, const pb::CreateDeviceReq *req, pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            // NB: This is the userid for the admin-account that is creating the device
            const auto& cuser = rctx.uctx->userUuid();

            if (!rctx.uctx->isAdmin()) {
                throw server_err{nextapp::pb::Error::PERMISSION_DENIED, "Only Admin users can create devices"};
            }
            rctx.session().requireWritableForAdd("device");

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
                        format("SELECT id, user, otp_hash FROM otp "
                               "WHERE email=? AND kind='new_device' "
                               "AND created >= UTC_TIMESTAMP() - INTERVAL {} MINUTE", otp_ttl_minutes),
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
            optional<UserContext::ResourceReservation> reservation;
            if (user_id == cuser) {
                reservation.emplace(rctx.uctx->reserveAddition(1, UserContext::PlanResource::DEVICE));
            }
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
            if (reservation) {
                reservation->commit();
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
    return mutatingUnaryHandler(ctx, req, reply,
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
    return mutatingUnaryHandler(ctx, req, reply,
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

    if (res.affected_rows() > 0) {
        pb::UserGlobalSettings settings;
        co_await getGlobalSettings(settings, rctx);
        sessionManager().setUserSettings(toUuid(rctx.uctx->userUuid()), settings);
    }

    co_return res.affected_rows() > 0;
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetOtpForNewDevice(
    ::grpc::CallbackServerContext *ctx, const pb::OtpRequest *req, pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
        [this] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            auto res = co_await rctx.dbh->exec(
                "SELECT email FROM user WHERE id = ?", cuser);
            if (res.rows().empty()) {
                throw server_err{pb::Error::MISSING_USER_EMAIL, "Email for the current user was not found"};
            }

            reply->mutable_otpresponse()->CopyFrom(co_await issueOtpForNewDevice(
                rctx, cuser, res.rows().front().front().as_string(), false));

            co_return;
        }, __func__);
}

boost::asio::awaitable<pb::OtpResponse> GrpcServer::NextappImpl::issueOtpForNewDevice(
    RequestCtx& rctx, string_view userId, string_view email, bool notifyUserOfAdminRecovery)
{
    auto trx = co_await rctx.dbh->transaction();

    // Only new-device OTPs compete with each other; do not invalidate other OTP kinds.
    co_await rctx.dbh->exec("DELETE FROM otp WHERE user=? AND kind='new_device'", userId);

    pb::OtpResponse response;
    response.set_email(pb_adapt(email));
    const auto otp = getRandomStr(8, "0123456789");
    const auto otpId = newUuidStr();
    const auto hash = getOtpHash(userId, otpId, otp);
    co_await rctx.dbh->exec(
        "INSERT INTO otp (id, user, otp_hash, email, kind) VALUES (?, ?, ?, ?, 'new_device')",
        rctx.uctx->dbOptions(), otpId, userId, hash, email);

    auto otpCreated = co_await rctx.dbh->exec("SELECT created FROM otp WHERE id=?", rctx.uctx->dbOptions(), otpId);
    const auto issuedAt = toTimeT(otpCreated.rows().front().front().as_datetime(), rctx.uctx->tz());
    response.mutable_issuedat()->set_unixtime(issuedAt);
    response.set_otp(otp);

    std::optional<pb::Notification> notification;
    if (notifyUserOfAdminRecovery) {
        notification.emplace();
        notification->mutable_uuid()->set_uuid(newUuidStr());
        notification->mutable_touser()->set_uuid(pb_adapt(userId));
        notification->set_subject("Account recovery OTP issued");
        notification->set_message(format(
            "An administrator obtained a one-time password for account recovery at {} UTC.",
            *toAnsiTime(issuedAt, rctx.uctx->tz(), true)));
        notification->set_sendertype(pb::Notification::SenderType::Notification_SenderType_SYSTEM);
        notification->set_senderid(Server::instance().serverId());
        notification->set_kind(pb::Notification::Kind::Notification_Kind_WARNING);
        notification->set_data(format("event=admin_otp_issued;issued_at={}", issuedAt));

        const auto ins = co_await rctx.dbh->exec(R"(INSERT INTO notification
            (valid_to, subject, message, sender_type, sender_id, to_tenant, to_user, uuid, kind, data)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?))",
            rctx.uctx->dbOptions(), std::optional<string>{}, notification->subject(), notification->message(),
            "system", notification->senderid(), std::optional<string>{}, userId, notification->uuid().uuid(),
            "warning", notification->data());
        notification->set_id(ins.last_insert_id());

        auto created = co_await rctx.dbh->exec(
            "SELECT updated, created_time FROM notification WHERE id=?", rctx.uctx->dbOptions(), notification->id());
        notification->set_updated(toMsTimestamp(created.rows().front().at(0).as_datetime(), rctx.uctx->tz()));
        notification->mutable_createdtime()->set_unixtime(toTimeT(created.rows().front().at(1).as_datetime(), rctx.uctx->tz()));
    }

    co_await trx.commit();

    if (notification) {
        owner_.setLastNotificationUpdated(notification->updated());
        co_await owner_.sessionManager().publishNotification(*notification);
    }

    co_return response;
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetOtpForUser(
    ::grpc::CallbackServerContext *ctx, const pb::AdminOtpRequest *req, pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
        [this, ctx, req] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            string userId;
            string email;
            boost::mysql::results result;

            switch (req->user_case()) {
            case pb::AdminOtpRequest::kUserId:
                validatedUuid(req->userid().uuid());
                result = co_await rctx.dbh->exec(
                    "SELECT id, email FROM user WHERE id=?", rctx.uctx->dbOptions(), req->userid().uuid());
                break;
            case pb::AdminOtpRequest::kEmail:
                if (!isValidEmail(req->email())) {
                    throw server_err{pb::Error::CONSTRAINT_FAILED, "Invalid email"};
                }
                result = co_await rctx.dbh->exec(
                    "SELECT id, email FROM user WHERE email=?", rctx.uctx->dbOptions(), req->email());
                break;
            case pb::AdminOtpRequest::USER_NOT_SET:
                throw server_err{pb::Error::INVALID_ARGUMENT, "User ID or email is required"};
            }

            if (result.rows().empty()) {
                throw server_err{pb::Error::MISSING_USER_ID, "User was not found"};
            }

            userId = result.rows().front().at(0).as_string();
            email = result.rows().front().at(1).as_string();
            reply->mutable_otpresponse()->CopyFrom(co_await issueOtpForNewDevice(rctx, userId, email, true));

            LOG_INFO << "Admin account-recovery OTP issued by user " << rctx.uctx->userUuid()
                     << " for user " << userId << " from " << ctx->peer();
            co_return;
        }, __func__, true /* allow new session */, true /* admin only */);
}

::grpc::ServerWriteReactor<pb::Status> *GrpcServer::NextappImpl::ListTenants(
    ::grpc::CallbackServerContext *ctx, const pb::ListTenantsReq *req)
{
    return writeStreamHandler(ctx, req,
    [this, req, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {
        const auto stream_scope = owner_.server().metrics().data_streams_actions().scoped();
        const auto uctx = rctx.uctx;
        const auto& cuser = uctx->userUuid();
        const bool include_plan_info = req->has_show_plan_info()
            && req->show_plan_info()
            && owner_.server().config().payment.enable_plan;

        assert(uctx->isAdmin());

        constexpr string_view query = R"(SELECT
    t.id AS tenant_id,
    t.name AS tenant_name,
    t.kind AS tenant_kind,
    t.descr AS tenant_descr,
    t.properties AS tenant_properties,
    t.state AS tenant_state,
    t.system_tenant AS tenant_system_tenant,
    t.plan AS tenant_plan,
    t.plan_updated AS tenant_plan_updated,
    t.plan_expires AS tenant_plan_expires,
    t.plan_seats AS tenant_plan_seats,
    t.grace_period_expires AS tenant_grace_period_expires,
    t.account_expires AS tenant_account_expires,
    t.registration_state AS tenant_registration_state,
    t.registration_attempts AS tenant_registration_attempts,
    t.last_registration_attempt AS tenant_last_registration_attempt,
    t.next_registration_retry AS tenant_next_registration_retry,
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
            TENANT_PLAN, TENANT_PLAN_UPDATED, TENANT_PLAN_EXPIRES, TENANT_PLAN_SEATS,
            TENANT_GRACE_PERIOD_EXPIRES, TENANT_ACCOUNT_EXPIRES, TENANT_REGISTRATION_STATE,
            TENANT_REGISTRATION_ATTEMPTS, TENANT_LAST_REGISTRATION_ATTEMPT, TENANT_NEXT_REGISTRATION_RETRY,
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
                    tenant->set_uuid(pb_adapt(tenant_id));
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
                    if (include_plan_info) {
                        auto* info = tenant->mutable_subscription_info();
                        const auto registration_state = toUpper(toStringIfValue(row, TENANT_REGISTRATION_STATE));
                        pb::SubscriptionInfo::RegistrationState registration_state_value{};
                        if (pb::SubscriptionInfo::RegistrationState_Parse(registration_state, &registration_state_value)) {
                            info->set_registration_state(registration_state_value);
                        }
                        info->set_registration_attempts(getUint32(row.at(TENANT_REGISTRATION_ATTEMPTS)));
                        setUnixTimeIfPresent(info->mutable_last_registration_attempt(), row.at(TENANT_LAST_REGISTRATION_ATTEMPT));
                        setUnixTimeIfPresent(info->mutable_next_registration_retry(), row.at(TENANT_NEXT_REGISTRATION_RETRY));

                        const auto plan_name = toStringIfValue(row, TENANT_PLAN);
                        if (!plan_name.empty()) {
                            auto* subscription = info->mutable_subscription();
                            auto* plan = subscription->mutable_plan();
                            plan->set_name(plan_name);

                            if (const auto cached_plan = owner_.server().grpc().sessionManager().getPlan(plan_name)) {
                                plan->set_active(cached_plan->active);
                                plan->mutable_createdat()->set_unixtime(std::chrono::system_clock::to_time_t(cached_plan->created_at));
                                plan->set_maxusers(cached_plan->max_users);
                                plan->set_maxdevices(cached_plan->max_devices);
                                plan->set_maxnodes(cached_plan->max_nodes);
                                plan->set_nodexmonthlygrowth(cached_plan->nodes_monthly_growth);
                                plan->set_maxactions(cached_plan->max_actions);
                                plan->set_actionsmonthlygrowth(cached_plan->actions_monthly_growth);
                                plan->set_maxworksessions(cached_plan->max_worksessions);
                                plan->set_worksessionsmonthlygrowth(cached_plan->work_sessions_monthly_growth);
                                plan->set_maxtimeblocks(cached_plan->max_time_blocks);
                                plan->set_timeblocksmonthlygrowth(cached_plan->time_blocks_monthly_growth);
                                plan->set_mobileonly(cached_plan->mobile_only);
                            }

                            setUnixTimeIfPresent(subscription->mutable_planupdatedat(), row.at(TENANT_PLAN_UPDATED));
                            setUnixTimeIfPresent(subscription->mutable_planexpires(), row.at(TENANT_PLAN_EXPIRES));
                            subscription->set_planseats(getUint32(row.at(TENANT_PLAN_SEATS)));
                            setUnixTimeIfPresent(subscription->mutable_graceperiodexpires(), row.at(TENANT_GRACE_PERIOD_EXPIRES));
                            setUnixTimeIfPresent(subscription->mutable_accountexpires(), row.at(TENANT_ACCOUNT_EXPIRES));
                        }
                    }

                    // End of tenant data
                };

                ++total_rows;
                ++num_rows_in_batch;

                const auto user_id = row.at(USER_ID).as_string();
                if (!user_id.empty()) {
                    // Fill in the user data
                    auto * u = tenant->add_users();
                    u->set_uuid(pb_adapt(user_id));
                    u->set_tenant(pb_adapt(tenant_id));
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

    }, __func__, /* allow new session */ true, /* admin only */ true);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteAccount(::grpc::CallbackServerContext *ctx,
                                                                   const pb::Empty *req,
                                                                   pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
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

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SetPushNotificationConfig(
    ::grpc::CallbackServerContext *ctx, const pb::PushNotificationConfig *req, pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            LOG_DEBUG_EX(rctx) << "Setting push notification kind to " << static_cast<int>(req->kind());
            co_await rctx.session().processPushState(*req);
            auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
            update.set_push(rctx.session().hasPush());
            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SendFeedback(::grpc::CallbackServerContext *ctx,
                                                                  const pb::Feedback *req,
                                                                  pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            LOG_DEBUG_EX(rctx) << "Storing feedback.";

            const auto dev_id = toString(rctx.session().deviceId());

            auto res = co_await rctx.dbh->exec(
                "INSERT INTO feedback (id, user, message, log, deviceId, kind, emoji, requestsAnswer) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)", rctx.uctx->dbOptions(),
                newUuidStr(),
                rctx.uctx->userUuid(),
                req->message(),
                toBlobOrNull(req->log(), owner_.server().config().options.max_feedback_log_size),
                dev_id,
                nextapp::pb::Feedback::Kind_Name(req->kind()),
                nextapp::pb::Feedback::Emoji_Name(req->emoji()),
                req->requestsanswer());

            owner_.server().metrics().user_feedbacks().inc();

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetSubscription(
    ::grpc::CallbackServerContext *ctx, const pb::GetSubscriptionReq *req, pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            if (req->forcerefresh() && owner_.server().config().payment.enable_plan) {
                const auto tenant_id = toUuid(rctx.uctx->tenantUuid());
                co_await owner_.server().plans()->refreshTenantSubscription(*rctx.dbh, tenant_id);
            }

            if (auto *p = reply->mutable_subscription()) {
                *p = rctx.uctx->getSubscription();
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetPaymentsPage(
    ::grpc::CallbackServerContext *ctx, const pb::PaymentsPageReq *req, pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
        const auto& pconf = owner_.server().config().payment;
            if (!pconf.enable_plan) {
                setError(*reply, pb::Error::INVALID_REQUEST, "Payments and plans are disabled");
            }

            ::payments::v1::CreateCheckoutContextRequest preq;
            preq.set_product_id(pconf.product_id);
            preq.set_tenant_id(rctx.uctx->tenantUuid());
            preq.set_initial_seats(1);
            preq.set_min_seats(1);
            preq.set_max_seats(1);
            preq.set_return_url(pconf.return_url);
            preq.set_cancel_url(pconf.cancel_url);
            preq.set_success_url(pconf.success_url);

            // Throws on error, but should be handled by the unaryHandler wrapper
            const auto checkout_context = co_await owner_.server().plans()->createCheckoutContext(preq);

            // TODO: Do we need to use a different client reference id here to make requests idempotent?
            preq.set_client_reference_id(to_string(rctx.session().sessionId()));

            if (auto *p = reply->mutable_paymentspage()) {
                p->set_url(checkout_context.hosted_page_url());

                LOG_TRACE_EX(rctx) << "Received checkout URL " << p->url();
            }
            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetPlans(
    ::grpc::CallbackServerContext *ctx, const pb::GetPlansReq *req, pb::Status *reply)
{
    return mutatingUnaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            auto *plans = reply->mutable_plans();
            if (!plans) {
                throw runtime_error{"Could not allocate plans"};
            }

            auto res = co_await rctx.dbh->exec(
                "SELECT name, active, createdAt, max_users, max_devices, max_nodes, nodes_monthly_growth, "
                "max_actions, actions_monthly_growth, max_worksessions, work_sessions_monthly_growth, "
                "max_time_blocks, time_blocks_monthly_growth, mobile_only "
                "FROM plan ORDER BY name");
            enum Cols {
                NAME,
                ACTIVE,
                CREATED_AT,
                MAX_USERS,
                MAX_DEVICES,
                MAX_NODES,
                NODES_MONTHLY_GROWTH,
                MAX_ACTIONS,
                ACTIONS_MONTHLY_GROWTH,
                MAX_WORKSESSIONS,
                WORK_SESSIONS_MONTHLY_GROWTH,
                MAX_TIME_BLOCKS,
                TIME_BLOCKS_MONTHLY_GROWTH,
                MOBILE_ONLY
            };

            for (const auto& row : res.rows()) {
                auto *plan = plans->add_plans();
                assert(plan);

                plan->set_name(pb_adapt(row[NAME].as_string()));
                plan->set_active(row[ACTIVE].as_int64() != 0);
                plan->mutable_createdat()->set_unixtime(toTimeT(row[CREATED_AT].as_datetime()));
                plan->set_maxusers(getUint32(row[MAX_USERS]));
                plan->set_maxdevices(getUint32(row[MAX_DEVICES]));
                plan->set_maxnodes(getUint32(row[MAX_NODES]));
                plan->set_nodexmonthlygrowth(getUint32(row[NODES_MONTHLY_GROWTH]));
                plan->set_maxactions(getUint32(row[MAX_ACTIONS]));
                plan->set_actionsmonthlygrowth(getUint32(row[ACTIONS_MONTHLY_GROWTH]));
                plan->set_maxworksessions(getUint32(row[MAX_WORKSESSIONS]));
                plan->set_worksessionsmonthlygrowth(getUint32(row[WORK_SESSIONS_MONTHLY_GROWTH]));
                plan->set_maxtimeblocks(getUint32(row[MAX_TIME_BLOCKS]));
                plan->set_timeblocksmonthlygrowth(getUint32(row[TIME_BLOCKS_MONTHLY_GROWTH]));
                plan->set_mobileonly(row[MOBILE_ONLY].as_int64() != 0);
            }

            if (const auto cached = owner_.server().plans() ? owner_.server().plans()->activePlans() : nullptr) {
                plans->set_trial_days(cached->trial_days);
                plans->set_defaultforsignup(cached->default_for_signup);
                plans->set_defaultforfree(cached->default_for_free);
            }

            co_return;
        }, __func__, true /* allow new session */, true /* admin only */);
}

::grpc::ServerWriteReactor<pb::Status> *GrpcServer::NextappImpl::GetFeedback(::grpc::CallbackServerContext *ctx, const pb::GetFeedbackReq *req)
{
    return writeStreamHandler(ctx, req,
    [this, req, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {
        const auto stream_scope = owner_.server().metrics().data_streams_actions().scoped();
        const auto uctx = rctx.uctx;
        const auto& cuser = uctx->userUuid();

        assert(uctx->isAdmin());

        constexpr string_view query = R"(SELECT f.id, f.user, u.email, u.tenant, u.active, f.deviceId, f.kind, f.emoji, f.log, f.message, f.requestsAnswer, f.createdAt
FROM feedback f LEFT JOIN user u ON f.user = u.id ORDER BY f.createdAt DESC)";

        enum Cols { ID, USER, EMAIL, TENANT, ACTIVE, DEVICEID, KIND, EMOJI, LOG, MESSAGE, REQUESTS_ANSWER, CREATED_AT };

        // Use batched reading from the database, so that we can get all the data, but
        // without running out of memory.
        assert(rctx.dbh);
        co_await  rctx.dbh->start_exec(query, rctx.uctx->dbOptions());

        nextapp::pb::Status reply;
        auto total_rows = 0u;

        bool read_more = true;
        for(auto rows = co_await rctx.dbh->readSome()
             ; read_more
             ; rows = co_await rctx.dbh->readSome()) {

            read_more = rctx.dbh->shouldReadMore(); // For next iteration

            if (rows.empty()) {
                LOG_TRACE_N << "Out of rows to iterate...";
                break;
            }

            for(const auto& row : rows) {
                ++total_rows;

                auto * f = reply.mutable_feedback();
                f->set_id(pb_adapt(row.at(ID).as_string()));
                f->set_userid(pb_adapt(row.at(USER).as_string()));
                f->set_useremail(pb_adapt(row.at(EMAIL).as_string()));
                f->set_userisactive(row.at(ACTIVE).as_int64() != 0);
                f->set_tenantid(pb_adapt(row.at(TENANT).as_string()));
                f->mutable_created()->set_unixtime(toTimeT(row[CREATED_AT].as_datetime(), uctx->tz()));

                auto * fb = f->mutable_feedback();

                // kind enum
                {
                    const auto kind = toUpper((toStringIfValue(row, KIND)));
                    pb::Feedback::Kind kind_value{};
                    if (pb::Feedback::Kind_Parse(kind, &kind_value)) {
                        fb->set_kind(kind_value);
                    }
                }

                // emoji enum
                {
                    const auto emoji = toUpper((toStringIfValue(row, EMOJI)));
                    pb::Feedback::Emoji emoji_value{};
                    if (pb::Feedback::Emoji_Parse(emoji, &emoji_value)) {
                        fb->set_emoji(emoji_value);
                    }
                }

                if (row.at(LOG).is_blob()) {
                    const auto& blob = row.at(LOG).as_blob();
                    if (!blob.empty()) {
                        fb->set_log(blob.data(), blob.size());
                        fb->set_haslog(true);
                    } else {
                        fb->set_haslog(false);
                    }
                }
                fb->set_message(toStringIfValue(row, MESSAGE));
                fb->set_requestsanswer(row.at(REQUESTS_ANSWER).as_int64() != 0);

                co_await stream->sendMessage(std::move(reply), boost::asio::use_awaitable);
                reply.Clear();
            }
        } // read more from db loop


    }, __func__, /* allow new session */ true, /* admin only */ true);
}

::grpc::ServerReadReactor<pb::ImportDataMsg> *GrpcServer::NextappImpl::ImportData(
    ::grpc::CallbackServerContext *ctx, pb::Status *reply)
{
    return mutatingReadStreamHandler<pb::ImportDataMsg>(ctx, reply,
    [this, reply, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {

        LOG_INFO_EX(rctx) << "Starting data import";
        bool success = false;
        rctx.session().requireWritableForAdd("imported data");

        auto trx = co_await rctx.dbh->transaction();

        ScopedExit onExit([&] {
            if (success) {
                owner_.server().metrics().data_imports_success().inc();
            } else {
                owner_.server().metrics().data_import_errors().inc();
            }

            LOG_INFO_EX(rctx) << "Data import finished " << (success ? "successfully" : "with errors");
        });

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

            bool containsAction(const boost::uuids::uuid& uid) const noexcept {
                return actions_.contains(uid);
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
        deque<pair<boost::uuids::uuid, boost::uuids::uuid>> action_origin;
        deque<pair<boost::uuids::uuid, boost::uuids::uuid>> node_parent;
        deque<pair<boost::uuids::uuid, boost::uuids::uuid>> node_category;

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
            rctx.uctx->onMassDelete({UserContext::PlanResource::TIME_BLOCK,
                                     UserContext::PlanResource::ACTION,
                                     UserContext::PlanResource::NODE,
                                     UserContext::PlanResource::WORK_SESSION});
        };

        co_await clear_user_data();

        std::exception_ptr eptr;

        auto post_updates = [&](const nextapp::pb::ImportDataMsg *msg = nullptr) -> boost::asio::awaitable<void> {

            if (!node_parent.empty() && (!msg || !msg->has_nodes())) {
                auto sql = "UPDATE node SET parent=? WHERE id=? AND user=?";

                auto generator = jgaa::mysqlpool::BindTupleGenerator(node_parent,
                                                                     [&] (const auto& np) {
                                                                         return make_tuple (
                                                                             mapper.node(np.second),
                                                                             toString(np.first),
                                                                             cuser
                                                                             );
                                                                     });

                try {
                    co_await rctx.dbh->exec(sql, generator);
                    node_parent.clear();
                } catch (const std::exception& ex) {
                    LOG_ERROR_EX(rctx) << "Failed to update node parents: " << ex.what();
                    throw server_err{pb::Error::GENERIC_ERROR, "Failed to update node parents"};
                }
            }

            // wash action_origin. Remove any entries that point to non-existing actions
            action_origin.erase(
                std::remove_if(action_origin.begin(), action_origin.end(),
                               [&mapper] (const auto& ao) {
                                   if (!mapper.containsAction(ao.second)) [[unlikely]] {
                                        LOG_DEBUG_EX() << "Removing action origin mapping for action "
                                                       << toString(ao.first) << " -> " << toString(ao.second)
                                                       << ", since the origin action does not exist in the mapping-table.";
                                       return true;
                                   }
                                   return false;
                               }),
                action_origin.end());

            if (!action_origin.empty() && (!msg || !msg->has_actions())) {
                auto sql = "UPDATE action SET origin=? WHERE id=? AND user=?";

                auto generator = jgaa::mysqlpool::BindTupleGenerator(action_origin,
                                                                     [&] (const auto& ao) {
                                                                         return make_tuple (
                                                                             toString(ao.first),
                                                                             mapper.action(ao.second),
                                                                             cuser
                                                                             );
                                                                     });
                try {
                    co_await rctx.dbh->exec(sql, generator);
                    action_origin.clear();
                } catch (const std::exception& ex) {
                    LOG_ERROR_EX(rctx) << "Failed to update actions origin: " << ex.what();
                    throw server_err{pb::Error::GENERIC_ERROR, "Failed to actions origin"};
                }
            };

            if (!node_category.empty() && !msg) {
                auto sql = "UPDATE node SET category=? WHERE id=? AND user=?";

                auto generator = jgaa::mysqlpool::BindTupleGenerator(node_category,
                                                                     [&] (const auto& nc) {
                                                                         return make_tuple (
                                                                             mapper.actionCategory(toString(nc.second)),
                                                                             toString(nc.first),
                                                                             cuser
                                                                             );
                                                                     });
                try {
                    co_await rctx.dbh->exec(sql, generator);
                    node_category.clear();
                } catch (const std::exception& ex) {
                    LOG_ERROR_EX(rctx) << "Failed to update node categories: " << ex.what();
                    throw server_err{pb::Error::GENERIC_ERROR, "Failed to update node categories"};
                }
            }

        };

        try {
            while(true) {
                auto msg = co_await stream->read();
                if (!msg) {
                    // Unexpected end of stream
                    LOG_WARN_N << "Unexpected end of stream while importing data for user " << cuser;
                    co_await clear_user_data();
                    throw server_err{pb::Error::GENERIC_ERROR, "Unexpected end of stream"};
                }

                co_await post_updates(&msg.value());

                if (msg->has_user()) {
                    const auto& u = msg->user();
                    mapper.addUser(u.uuid());
                    LOG_TRACE_N << "Importing user " << u.uuid() << " as user " << cuser;
                } else if (msg->has_userglobalsettings()) {
                    co_await owner_.saveUserGlobalSettings(rctx.dbh.value(), msg->userglobalsettings(), rctx);
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
                                if (!item.category().empty()) {
                                    node_category.emplace_back(toUuid(item.uuid()), toUuid(item.category()));
                                    item.clear_category();
                                }

                                if (!item.parent().empty()) {
                                    if (auto node = mapper.node(item.parent(), false); !node.empty()) {
                                        item.set_parent(node);
                                    } else {
                                        // Delay until all nodes are imported
                                        node_parent.emplace_back(toUuid(item.uuid()), toUuid(item.parent()));
                                        item.clear_parent();
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
                                    if (mapper.containsAction(toUuid(item.origin()))) {
                                        item.set_origin(mapper.action(item.origin()));
                                    } else {
                                        // Delay until all actions are imported
                                        action_origin.emplace_back(toUuid(item.id()), toUuid(item.origin()));
                                        item.clear_origin();
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
                                if (!tb.category().empty()) {
                                    tb.set_category(mapper.actionCategory(tb.category()));
                                }

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

            co_await post_updates();
        } catch (const exception& ex) {
            LOG_DEBUG_EX(rctx) << "Exception while importing data: " << ex.what();
            eptr = std::current_exception();
        }

        if (eptr) {
            rctx.updates.clear();
            std::rethrow_exception(eptr);
        }

        co_await rctx.dbh->exec("UPDATE `user` SET data_sync_epoch = data_sync_epoch + 1 WHERE id = ?", cuser);
        auto epoch_res = co_await rctx.dbh->exec("SELECT data_sync_epoch FROM `user` WHERE id = ?", cuser);
        if (epoch_res.rows().empty()) {
            throw server_err{pb::Error::GENERIC_ERROR, "Failed to load updated data sync epoch"};
        }
        const auto data_sync_epoch = epoch_res.rows().front().at(0).as_uint64();

        co_await trx.commit();

        rctx.updates.clear();
        co_await rctx.uctx->publishFullResync(data_sync_epoch);

        success = true;
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

        pb::GetNewReq export_req;

        // Green days
        co_await owner_.exportDays(export_req, dbh, flush, rctx);

        // Nodes
        co_await owner_.exportNodes(export_req, dbh, flush, rctx, true);

        // Categories
        msg.Clear();
        msg.set_error(pb::Error::OK);
        msg.mutable_actioncategories()->CopyFrom(
            co_await owner_.getActionCategories(dbh, rctx.uctx->userUuid()));
        co_await stream->sendMessage(std::move(msg), boost::asio::use_awaitable);

        // Actions
        co_await owner_.exportActions(export_req, dbh, flush, rctx, true);

        // Work sessions
        co_await owner_.exportWork(export_req, dbh, flush, rctx, true);

        // Time blocks
        co_await owner_.exportTimeBlocks(export_req, dbh, flush, rctx, true);

        msg.Clear();
        msg.set_error(pb::Error::OK);
        msg.set_hasmore(false); // No more messages to send
        co_await stream->sendMessage(std::move(msg), boost::asio::use_awaitable);

        owner_.server().metrics().data_exports().inc();
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
    u.set_uuid(pb_adapt(row.at(ID).as_string()));
    u.set_tenant(pb_adapt(row.at(TENANT).as_string()));
    u.set_name(pb_adapt(row.at(NAME).as_string()));
    u.set_email(pb_adapt(row.at(EMAIL).as_string()));
    pb::User::Kind kind_value{};
    if (pb::User::Kind_Parse(toUpper(row.at(KIND).as_string()), &kind_value)) {
        u.set_kind(kind_value);
    }
    u.set_active(row.at(ACTIVE).as_int64() > 0);
    u.set_descr(pb_adapt(row.at(DESCR).as_string()));
    if (auto kv = KeyValueFromBlob(row.at(PROPERTIES))) {
        u.mutable_properties()->CopyFrom(*kv);
    }
    if (row.at(SYSTEM_USER).is_int64()) {
        u.set_system_user(row.at(SYSTEM_USER).as_int64() > 0);
    }
    co_return u;
}

} // ns
