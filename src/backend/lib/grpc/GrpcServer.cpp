
#include "nextapp/GrpcServer.h"
#include "nextapp/Server.h"

using namespace std;
using namespace std::literals;
using namespace std;
namespace asio = boost::asio;

namespace nextapp::grpc {

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

std::string toAnsiDate(const nextapp::pb::Date& date) {
    return format("{:0>4d}-{:0>2d}-{:0>2d}", date.year(), date.month(), date.mday());
}

::nextapp::pb::Date toDate(const boost::mysql::date& from) {
    assert(from.valid());
    ::nextapp::pb::Date date;
    date.set_year(from.year());
    date.set_month(from.month());
    date.set_mday(from.day());

    return date;
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
    return unaryHandler(ctx, req, reply,
                        [this] (auto *reply) -> boost::asio::awaitable<void> {
        auto res = co_await owner_.server().db().exec(
            "SELECT id, name, color, score FROM day_colors WHERE tenant IS NULL ORDER BY score DESC");

        enum Cols {
            ID, NAME, COLOR, SCORE
        };

        for(const auto row : res.rows()) {
            auto *dc = reply->add_daycolors();
            dc->set_id(row.at(COLOR).as_string());
            dc->set_color(row.at(COLOR).as_string());
            dc->set_name(row.at(NAME).as_string());
            dc->set_score(static_cast<int32_t>(row.at(SCORE).as_int64()));
        }

        LOG_TRACE_N << "Finish day colors lookup.";
        //LOG_TRACE_N << "Reply is: " << toJson(*reply);
        co_return;
    });
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
        LOG_TRACE << "Reply is: " << toJson(*reply);
        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetMonth(::grpc::CallbackServerContext *ctx, const pb::MonthReq *req, pb::Month *reply)
{
    return unaryHandler(ctx, req, reply,
                        [this, req, ctx] (pb::Month *reply) -> boost::asio::awaitable<void> {

        auto res = co_await owner_.server().db().execs(
            "SELECT date, user, color, ISNULL(notes), ISNULL(report) FROM day WHERE user=? AND YEAR(date)=? AND MONTH(date)=? ORDER BY date",
            owner_.currentUser(ctx), req->year(), req->month());

        enum Cols {
            DATE, USER, COLOR, NOTES, REPORT
        };

        for(const auto& row : res.rows()) {
            const auto date_val = row.at(DATE).as_date();
            if (date_val.valid()) {
                auto current_day = reply->add_days();
                *current_day->mutable_date() = toDate(date_val);
                if (row.at(USER).is_string()) {
                    current_day->set_user(row.at(USER).as_string());
                }
                current_day->set_hasnotes(row.at(NOTES).as_int64() != 1);
                current_day->set_hasreport(row.at(REPORT).as_int64() != 1);
            }
        }

        LOG_TRACE << "Finish month lookup.";
        LOG_TRACE << "Reply is: " << toJson(*reply);
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



} // ns
