
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/json.hpp>

#include "nextapp/GrpcServer.h"
#include "nextapp/Server.h"

using namespace std;
using namespace std::literals;
using namespace std;
namespace json = boost::json;
namespace asio = boost::asio;

namespace nextapp::grpc {

boost::uuids::uuid newUuid()
{
    static boost::uuids::random_generator uuid_gen_;
    return uuid_gen_();
}

string newUuidStr()
{
    return boost::uuids::to_string(newUuid());
}


namespace {

template <typename T>
concept ProtoMessage = std::is_base_of_v<google::protobuf::Message, T>;

template <ProtoMessage T>
std::string toJson(const T& obj) {
    std::string str;
    auto res = google::protobuf::util::MessageToJsonString(obj, &str);
    if (!res.ok()) {
        LOG_DEBUG << "Failed to convert object to json: "
                  << typeid(T).name() << ": "
                  << res.ToString();
        throw std::runtime_error{"Failed to convert object to json"};
    }
    return str;
}

template <typename T>
concept ProtoStringStringMap = std::is_same_v<std::remove_cv<T>, std::remove_cv<::google::protobuf::Map<std::string, std::string>>>;


template <ProtoStringStringMap T>
string toJson(const T& map) {
    json::object o;

    for(const auto [key, value] : map) {
        o[key] = value;
    }

    return json::serialize(o);
}

std::string toAnsiDate(const nextapp::pb::Date& date) {
    return format("{:0>4d}-{:0>2d}-{:0>2d}", date.year(), date.month() + 1, date.mday());
}

::nextapp::pb::Date toDate(const boost::mysql::date& from) {
    assert(from.valid());
    ::nextapp::pb::Date date;
    date.set_year(from.year());
    date.set_month(from.month());
    date.set_mday(from.day());

    return date;
}

void setError(pb::Status& status, pb::Error err, const std::string& message = {}) {


    status.set_error(err);
    if (message.empty()) {
        status.set_message(pb::Error_Name(err));
    } else {
        status.set_message(message);
    }

    LOG_DEBUG << "Setting error " << status.message() << " on request.";
}

} // anon ns

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetServerInfo(::grpc::CallbackServerContext *ctx,
                                       const pb::Empty *,
                                       pb::ServerInfo *reply)
{
    assert(ctx);
    assert(reply);

    auto add = [&reply](string key, string value) {
        auto prop = reply->mutable_properties()->Add();
        prop->set_key(key);
        prop->set_value(value);
    };

    add("version", NEXTAPP_VERSION);

    auto* reactor = ctx->DefaultReactor();
    reactor->Finish(::grpc::Status::OK);
    return reactor;
}

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetDayColorDefinitions(::grpc::CallbackServerContext *ctx,
                                                const pb::Empty *req,
                                                pb::DayColorDefinitions *reply)
{
    auto rval = unaryHandler(ctx, req, reply,
                        [this] (auto *reply) -> boost::asio::awaitable<void> {
        auto res = co_await owner_.server().db().exec(
            "SELECT id, name, color, score FROM day_colors WHERE tenant IS NULL ORDER BY score DESC");

        enum Cols {
            ID, NAME, COLOR, SCORE
        };

        for(const auto row : res.rows()) {
            auto *dc = reply->add_daycolors();
            dc->set_id(row.at(ID).as_string());
            dc->set_color(row.at(COLOR).as_string());
            dc->set_name(row.at(NAME).as_string());
            dc->set_score(static_cast<int32_t>(row.at(SCORE).as_int64()));
        }

        boost::asio::deadline_timer timer{owner_.server().ctx()};
        timer.expires_from_now(boost::posix_time::seconds{2});
        //co_await timer.async_wait(asio::use_awaitable);

        LOG_TRACE_N << "Finish day colors lookup.";
        LOG_TRACE << "Reply is: " << toJson(*reply);
        co_return;
    });

    LOG_TRACE_N << "Leaving the coro do do it's magic...";
    return rval;
}

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetDay(::grpc::CallbackServerContext *ctx,
                                const pb::Date *req,
                                pb::CompleteDay *reply)
{
    return unaryHandler(ctx, req, reply,
                        [this, req, ctx] (pb::CompleteDay *reply) -> boost::asio::awaitable<void> {
        auto res = co_await owner_.server().db().execs(
            "SELECT date, user, color, notes, report FROM day WHERE user=? AND date=? ORDER BY date",
            owner_.currentUser(ctx), toAnsiDate(*req));

        enum Cols {
            DATE, USER, COLOR, NOTES, REPORT
        };

        if (!res.empty()) {
            const auto& row = res.rows().front();
            const auto date_val = row.at(DATE).as_date();

            auto day = reply->mutable_day();
            *day->mutable_date() = toDate(date_val);
            if (row.at(USER).is_string()) {
                day->set_user(row.at(USER).as_string());
            }
            if (row.at(COLOR).is_string()) {
                day->set_color(row.at(COLOR).as_string());
            }
            if (row.at(NOTES).is_string()) {
                day->set_hasnotes(true);
                reply->set_notes(row.at(NOTES).as_string());
            }
            if (row.at(REPORT).is_string()) {
                day->set_hasreport(true);
                reply->set_notes(row.at(REPORT).as_string());
            }
        }

        LOG_TRACE << "Finish day lookup.";
        LOG_TRACE_N << "Reply is: " << toJson(*reply);
        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetMonth(::grpc::CallbackServerContext *ctx, const pb::MonthReq *req, pb::Month *reply)
{
    return unaryHandler(ctx, req, reply,
                        [this, req, ctx] (pb::Month *reply) -> boost::asio::awaitable<void> {

        auto res = co_await owner_.server().db().execs(
            "SELECT date, user, color, ISNULL(notes), ISNULL(report) FROM day WHERE user=? AND YEAR(date)=? AND MONTH(date)=? ORDER BY date",
            owner_.currentUser(ctx), req->year(), req->month() + 1);

        enum Cols {
            DATE, USER, COLOR, NOTES, REPORT
        };

        reply->set_year(req->year());
        reply->set_month(req->month());

        for(const auto& row : res.rows()) {
            const auto date_val = row.at(DATE).as_date();
            if (date_val.valid()) {
                auto current_day = reply->add_days();
                *current_day->mutable_date() = toDate(date_val);
                current_day->set_user(row.at(USER).as_string());
                if (row.at(COLOR).is_string()) {
                    current_day->set_color(row.at(COLOR).as_string());
                }
                current_day->set_hasnotes(row.at(NOTES).as_int64() != 1);
                current_day->set_hasreport(row.at(REPORT).as_int64() != 1);
            }
        }

        LOG_TRACE_N << "Finish month lookup.";
        LOG_TRACE << "Reply is: " << toJson(*reply);
        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SetColorOnDay(::grpc::CallbackServerContext *ctx, const pb::SetColorReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (auto *reply) -> boost::asio::awaitable<void> {

        const auto color = req->color();
        if (color.empty()) {
            co_await owner_.server().db().execs("UPDATE day SET color=NULL WHERE date=? AND user=?",
                                                toAnsiDate(req->date()), owner_.currentUser(ctx));
        } else {
            co_await owner_.server().db().execs(
                R"(INSERT INTO day (date, user, color) VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE color=?)",
                toAnsiDate(req->date()), owner_.currentUser(ctx), color, color);
        }

        LOG_TRACE_N << "Finish updating color for " << toAnsiDate(req->date());

        auto update = make_shared<pb::Update>();
        auto dc = update->mutable_daycolor();
        *dc->mutable_date() = req->date();
        dc->set_user(owner_.currentUser(ctx));
        dc->set_color(req->color());

        owner_.publish(update);
        co_return;
    });
}

::grpc::ServerWriteReactor<pb::Update> *GrpcServer::NextappImpl::SubscribeToUpdates(::grpc::CallbackServerContext *context, const pb::UpdatesReq *request)
{
    class ServerWriteReactorImpl
        : public std::enable_shared_from_this<ServerWriteReactorImpl>
        , public Publisher
        , public ::grpc::ServerWriteReactor<pb::Update> {
    public:
        enum class State {
            READY,
            WAITING_ON_WRITE,
            DONE
        };

        ServerWriteReactorImpl(GrpcServer& owner, ::grpc::CallbackServerContext *context)
            : owner_{owner}, context_{context} {
        }

        ~ServerWriteReactorImpl() {
            LOG_DEBUG_N << "Remote client " << uuid() << " is going...";
        }

        void start() {
            // Tell owner about us
            LOG_DEBUG << "Remote client " << context_->peer() << " is subscribing to updates as subscriber " << uuid();
            self_ = shared_from_this();
            owner_.addPublisher(self_);
            reply();
        }

        /*! Callback event when the RPC is completed */
        void OnDone() override {
            {
                scoped_lock lock{mutex_};
                state_ = State::DONE;
            }

            owner_.removePublisher(uuid());
            self_.reset();
        }

        /*! Callback event when a write operation is complete */
        void OnWriteDone(bool ok) override {
            if (!ok) [[unlikely]] {
                LOG_WARN << "The write-operation failed.";

                // We still need to call Finish or the request will remain stuck!
                Finish({::grpc::StatusCode::UNKNOWN, "stream write failed"});
                scoped_lock lock{mutex_};
                state_ = State::DONE;
                return;
            }

            {
                scoped_lock lock{mutex_};
                updates_.pop();
            }

            reply();
        }

        void publish(const std::shared_ptr<pb::Update>& message) override {
            {
                scoped_lock lock{mutex_};
                updates_.emplace(message);
            }

            reply();
        }

    private:
        void reply() {
            scoped_lock lock{mutex_};
            if (state_ != State::READY || updates_.empty()) {
                return;
            }

            StartWrite(updates_.front().get());

            // TODO: Implement finish if the server shuts down.
            //Finish(::grpc::Status::OK);
        }

        GrpcServer& owner_;
        State state_{State::READY};
        std::queue<std::shared_ptr<pb::Update>> updates_;
        std::mutex mutex_;
        std::shared_ptr<ServerWriteReactorImpl> self_;
        ::grpc::CallbackServerContext *context_;
    };

    try {
        auto handler = make_shared<ServerWriteReactorImpl>(owner_, context);
        handler->start();
        return handler.get(); // The object maintains ownership over itself
    } catch (const exception& ex) {
        LOG_ERROR_N << "Caught exception while adding subscriber to update: " << ex.what();
    }

    return {};
}

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

        pb::Tenant tenant{req->tenant()};
        if (tenant.uuid().empty()) {
            tenant.set_uuid(newUuidStr());
        }
        if (tenant.properties().empty()) {
            tenant.mutable_properties();
        }

        const auto properties = toJson(*tenant.mutable_properties());
        if (!tenant.has_kind()) {
            tenant.set_kind(pb::Tenant::Tenant::Kind::Tenant_Kind_guest);
        }

        co_await owner_.server().db().execs(
            "INSERT INTO tenant (id, name, kind, descr, active, properties) VALUES (?, ?, ?, ?, ?, ?)",
                tenant.uuid(),
                tenant.name(),
                pb::Tenant::Kind_Name(tenant.kind()),
                tenant.descr(),
                tenant.active(),
                properties);

        LOG_INFO << "User " << owner_.currentUser(ctx)
                 << " has created tenant name=" << tenant.name() << ", id=" << tenant.uuid()
                 << ", kind=" << pb::Tenant::Kind_Name(tenant.kind());

        // create users
        for(const auto& user_template : req->users()) {
            pb::User user{user_template};

            if (user.uuid().empty()) {
                user.set_uuid(newUuidStr());
            }

            user.set_tenant(tenant.uuid());
            if (!user.has_kind()) {
                user.set_kind(pb::User::Kind::User_Kind_regular);
            }

            if (!user.has_active()) {
                user.set_active(true);
            }

            auto user_props = toJson(*user.mutable_properties());
            co_await owner_.server().db().execs(
                "INSERT INTO user (id, tenant, name, email, kind, active, descr, properties) VALUES (?,?,?,?,?,?,?,?)",
                    user.uuid(),
                    user.tenant(),
                    user.name(),
                    user.email(),
                    user.kind(),
                    user.active(),
                    user.descr(),
                    user_props);

            LOG_INFO << "User " << owner_.currentUser(ctx)
                     << " has created user name=" << user.name() << ", id=" << user.uuid()
                     << ", kind=" << pb::User::Kind_Name(user.kind())
                     << ", tenant=" << user.tenant();
        }

        // TODO: Publish the new tenant and users

        *reply->mutable_tenant() = tenant;

        co_return;
    });
}

GrpcServer::GrpcServer(Server &server)
    : server_{server}
{

}

void GrpcServer::start() {
    ::grpc::ServerBuilder builder;

    // Tell gRPC what TCP address/port to listen to and how to handle TLS.
    // grpc::InsecureServerCredentials() will use HTTP 2.0 without encryption.
    builder.AddListeningPort(config().address, ::grpc::InsecureServerCredentials());

    // Feed gRPC our implementation of the RPC's
    service_ = std::make_unique<NextappImpl>(*this);
    builder.RegisterService(service_.get());

    // Finally assemble the server.
    grpc_server_ = builder.BuildAndStart();
    LOG_INFO
        // Fancy way to print the class-name.
        // Useful when I copy/paste this code around ;)
        << boost::typeindex::type_id_runtime(*this).pretty_name()

        // The useful information
        << " listening on " << config().address;
}

void GrpcServer::stop() {
    LOG_INFO << "Shutting down "
             << boost::typeindex::type_id_runtime(*this).pretty_name();
    grpc_server_->Shutdown();
    grpc_server_->Wait();
}

void GrpcServer::addPublisher(const std::shared_ptr<Publisher> &publisher)
{
    LOG_TRACE_N << "Adding publisher " << publisher->uuid();
    scoped_lock lock{mutex_};
    publishers_[publisher->uuid()] = publisher;
}

void GrpcServer::removePublisher(const boost::uuids::uuid &uuid)
{
    LOG_TRACE_N << "Removing publisher " << uuid;
    scoped_lock lock{mutex_};
    publishers_.erase(uuid);
}

void GrpcServer::publish(const std::shared_ptr<pb::Update>& update)
{
    scoped_lock lock{mutex_};

    LOG_DEBUG_N << "Publishing update to " << publishers_.size() << " subscribers, Json: "
                << toJson(*update);

    for(auto& [uuid, weak_pub]: publishers_) {
        if (auto pub = weak_pub.lock()) {
            pub->publish(update);
        } else {
            LOG_WARN_N << "Failed to get a pointer to publisher " << uuid;
        }
    }
}



} // ns
