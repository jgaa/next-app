
#include "ServerComm.h"
#include "logging.h"

#include <QDebug>
#include <QGrpcHttp2Channel>
#include <QtConcurrent/QtConcurrent>

using namespace std;

ServerComm *ServerComm::instance_;

ServerComm::ServerComm()
    : client_{new nextapp::pb::Nextapp::Client} {

    instance_ = this;

    connect(client_.get(), &nextapp::pb::Nextapp::Client::errorOccurred,
            this, &ServerComm::errorOccurred);

}

ServerComm::~ServerComm()
{
    instance_ = {};
}

void ServerComm::start()
{
    QGrpcChannelOptions channelOptions(QUrl("http://127.0.0.1:10321", QUrl::StrictMode));
    client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(channelOptions));
#ifdef ASYNC_GRPC
    auto info_call = client_->GetServerInfo({});
    info_call->subscribe(this, [info_call, this]() {
            nextapp::pb::ServerInfo se = info_call->read<nextapp::pb::ServerInfo>();
            assert(se.properties().front().key() == "version");
            server_version_ = se.properties().front().value();
            LOG_INFO << "Connected to server version " << server_version_;
            emit versionChanged();
        }, [this](QGrpcStatus status) {
            LOG_ERROR_N << "Comm error: " << status.message();
        });

    auto day_color_defs_call = client_->GetDayColorDefinitions({});
    day_color_defs_call->subscribe(this, [day_color_defs_call, this]() {
            day_color_definitions_ = day_color_defs_call->read<nextapp::pb::DayColorDefinitions>();
            LOG_DEBUG_N << "Received " << day_color_definitions_.dayColors().size()
                        << " day-color definitions.";

            colors_.clear();
            for(const auto& color: day_color_definitions_.dayColors()) {
                colors_[QUuid{color.id_proto()}] = color.color();
            }

            emit dayColorDefinitionsChanged();
            onGrpcReady();
        }, [this](QGrpcStatus status) {
            LOG_ERROR_N << "Comm error: " << status.message();
        });

#else
    {
        nextapp::pb::ServerInfo se;
        auto res = client_->GetServerInfo({}, &se);
        if (res == QGrpcStatus::Ok) {
            assert(se.properties().front().key() == "version");
            server_version_ = se.properties().front().value();
            LOG_INFO << "Connected to server version " << server_version_;
            emit versionChanged();
        }
    }

    {
        nextapp::pb::DayColorDefinitions dcd;
        auto res = client_->GetDayColorDefinitions({}, &dcd);
        if (res == QGrpcStatus::Ok) {
            day_color_definitions_ = dcd;
            LOG_DEBUG_N << "Received " << day_color_definitions_.dayColors().size()
                        << " day color definitions.";
            emit dayColorDefinitionsChanged();
        }
    }

#endif
}

QString ServerComm::version()
{
    emit errorRecieved({});
    return server_version_;
}

nextapp::pb::DayColorRepeated ServerComm::getDayColorsDefinitions()
{
    return day_color_definitions_.dayColors();
}

MonthModel *ServerComm::getMonthModel(int year, int month)
{
    return new MonthModel{static_cast<unsigned>(year), static_cast<unsigned>(month)};
}

ServerComm::colors_in_months_t
ServerComm::getColorsInMonth(unsigned int year, unsigned int month, bool force)
{
    LOG_TRACE_N << "Requesting year=" << year << ", month=" << month;
    if (!grpc_is_ready_) {
        grpc_queue_.push([this, year, month] {
            getColorsInMonth(year, month);
        });
    }

    if (!force) {
        if (auto it = colors_in_months_.find({year, month}); it != colors_in_months_.end()) {
            return it->second;
        }
    }

    nextapp::pb::MonthReq req;
    req.setYear(year);
    req.setMonth(month);
    auto call = client_->GetMonth(req);
    call->subscribe(this, [call, this, y=year, m=month]() {
            auto month = call->read<nextapp::pb::Month>();
            LOG_DEBUG_N << "Received colors for " << month.days().size()
                        << " days for month: " << y << "-" << (m + 1);

            assert(month.year() == y);
            assert(month.month() == m);
            auto current = make_shared<QList<QUuid>>();
            current->resize(31);
            for(const auto& day : month.days()) {
                assert(day.date().mday() > 0);
                assert(day.date().mday() <= 31);
                current->replace(day.date().mday() - 1, QUuid{day.color()});
            }
            colors_in_months_[{y, m}] = current;
            emit monthColorsChanged(y, m, current);
        }, [this](QGrpcStatus status) {
            LOG_ERROR_N << "Comm error: " << status.message();
        });

    return {};
}

QString ServerComm::toDayColorName(const QUuid &uuid) const
{
    if (auto it = colors_.find(uuid); it != colors_.end()) {
        return it->second;
    }


    LOG_WARN << "Color " << uuid.toString() << " not found in colors_";
    return "lightpink";
}

void ServerComm::setDayColor(int year, int month, int day, QUuid colorUuid)
{
    if (!grpc_is_ready_) {
        grpc_queue_.push([&] {
            setDayColor(year, month, day, colorUuid);
        });
    }

    nextapp::pb::SetColorReq req;
    nextapp::pb::Date date;
    date.setYear(year);
    date.setMonth(month);
    date.setMday(day);
    req.setDate(date);
    if (!colorUuid.isNull()) {
        req.setColor(colorUuid.toString(QUuid::WithoutBraces));
    }

    auto call = client_->SetColorOnDay(req);
    call->subscribe(this, [call, this, year, month]() {
            call->read<nextapp::pb::Empty>();

            // TODO: Listen for changes from the server in stead...
            QTimer::singleShot(100, [this, year, month] {
                LOG_DEBUG << "Calling getColorsInMonth() from timer.";
                getColorsInMonth(year, month, true);
            });

        }, [this](QGrpcStatus status) {
            LOG_ERROR_N << "Comm error: " << status.message();
        });
}

void ServerComm::errorOccurred(const QGrpcStatus &status)
{
    LOG_ERROR_N << "Call to gRPC server failed: " << status.message();

    emit errorRecieved(tr("Call to gRPC server failed: %1").arg(status.message()));
}

void ServerComm::onGrpcReady()
{
    grpc_is_ready_ = true;
    for(; !grpc_queue_.empty(); grpc_queue_.pop()) {
        grpc_queue_.front()();
    }
}
