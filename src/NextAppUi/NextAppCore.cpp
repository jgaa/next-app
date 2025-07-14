#include "NextAppCore.h"

#include <CalendarModel.h>
#include <QDateTime>
#include <QDesktopServices>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

#ifdef LINUX_BUILD
#include <QDBusConnection>
#include <QDBusInterface>
#endif

#ifdef WITH_FCM
#   include "AndroidFcmBridge.h"
#endif

#include "SoundPlayer.h"
#include "WeeklyWorkReportModel.h"
#include "ActionCategoriesModel.h"
#include "util.h"

#include "qcorotimer.h"

using namespace std;

namespace {
QDate getDefaultDate(time_t when) {
    if (when > 0) {
        return QDateTime::fromSecsSinceEpoch(when).date();
    }
    return QDate::currentDate();
}

#ifdef WIN32
#include <QAbstractNativeEventFilter>
class PowerEventFilter : public QAbstractNativeEventFilter {
public:
    using fn_t = std::function<void()>;
    PowerEventFilter(fn_t fn)
        : callback_{std::move(fn)} {}

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override {
        LOG_TRACE_N << "Received event: " << string_view(eventType.constData(), eventType.size());
        if (eventType == "windows_generic_MSG") {
            auto *msg = static_cast<MSG*>(message);
            if (msg->message == WM_POWERBROADCAST
                && (msg->wParam == PBT_APMRESUMEAUTOMATIC
                    || msg->wParam == PBT_APMRESUMECRITICAL
                    || msg->wParam == PBT_APMRESUMESUSPEND)) {
                LOG_DEBUG_N << "Received wake-up event";
                assert(callback_);
                callback_();
            }
        }
        return false;
    }

private:
    fn_t callback_;
};
#endif
} // anon ns

NextAppCore *NextAppCore::instance_;

NextAppCore::NextAppCore(QQmlApplicationEngine& engine)
    : engine_{&engine}
{
    assert(instance_ == nullptr);
    instance_ = this;
    drag_enabled_ = !isMobile();

#ifdef USE_ANDROID_UI
    LOG_DEBUG_N << "Android UI";
    width_ = 350;
    height_ = 750;
#else
    connect(qApp, &QGuiApplication::primaryScreenChanged, [this](QScreen *screen) {
        emit widthChanged();
        emit heightChanged();
    });
#endif

    connect(&audio_play_delay_, &QTimer::timeout, [this]() {
        playSound(volume_, sound_file_);
    });

    db_ = make_unique<DbStore>();

    connect(db_.get(), &DbStore::error, [this](DbStore::Error error) {
        LOG_ERROR_N << "DB error: " << static_cast<int>(error);
        // TODO: Show error. Do not connect to server.
    });

    connect(db_.get(), &DbStore::initialized, [this]() {
        // TODO: Connect to server

    });

#ifdef LINUX_BUILD
    dbus_connection_ = make_unique<QDBusConnection>(QDBusConnection::systemBus());
    if (dbus_connection_->isConnected()) {
        LOG_DEBUG << "Connected to the D-Bus system bus";
        if (!dbus_connection_->connect(
                "org.freedesktop.login1",                   // Service name
                "/org/freedesktop/login1",                  // Object path
                "org.freedesktop.login1.Manager",           // Interface name
                "PrepareForSleep",                          // Signal name
                this,
                SLOT(handlePrepareForSleep(bool))
                )) {
            LOG_WARN << "Failed to connect to PrepareForSleep signal";
        }
    } else {
        LOG_WARN << "Cannot connect to the D-Bus system bus";
    }

#endif

#ifdef WIN32
    auto *pef = new PowerEventFilter([this]() {
        if (instance_ && state_ != State::SHUTTING_DOWN) {
            // LOG_TRACE_N << "Emitting wokeFromSleep()";
            // emit wokeFromSleep();
            onWokeFromSleep();
        }
    });

    LOG_INFO << "Installing event-listener for wakup.";
    qApp->installNativeEventFilter(pef);
#endif

#ifdef WITH_FCM
    auto &bridge = AndroidFcmBridge::instance();
    QObject::connect(&bridge, &AndroidFcmBridge::tokenRefreshed,
                     [](const QString &token){
                         LOG_DEBUG_N << "New FCM token:" << token;
                         // send to your server…
                     });
    QObject::connect(&bridge, &AndroidFcmBridge::messageReceived,
                     [](const QString &id, const QString &notif, const QString &data){
                         LOG_DEBUG_N << "Push arrived:" << id << notif << data;
                         // show an in-app UI or fire a local notification
                     });
#endif

    connect(qApp, &QGuiApplication::applicationStateChanged, [this](Qt::ApplicationState state) {
        const auto old_state = state_;

#ifdef LINUX_BUILD
        if (old_state == State::SUSPENDED) {
            LOG_TRACE_N << "Ignoring state change from SUSPENDED on Linux because it is not reliable.";
            // Wait for the DBus signal to confirm that we are waking up.
            return;
        }
#endif

        switch(state) {
        case Qt::ApplicationActive:
            setState(State::ACTIVE);
            LOG_DEBUG << "NextAppCore: The application is active.";
            if (old_state == State::SUSPENDED) {
                emit wokeFromSleep();
            }
            break;

        case Qt::ApplicationSuspended:
            LOG_DEBUG << "NextAppCore: The application is is suspended.";
            setState(State::SUSPENDED);
            emit suspending();
            break;

        case Qt::ApplicationHidden:
            LOG_DEBUG << "NextAppCore: The application is is hidden.";
            setState(State::HIDDEN);
            if (old_state == State::SUSPENDED) {
                emit wokeFromSleep();
            }
            break;

        case Qt::ApplicationInactive:
            LOG_DEBUG << "NextAppCore: The application is is inactive.";
            setState(State::INACTIVE);
            if (old_state == State::SUSPENDED) {
                emit wokeFromSleep();
            }
            break;
        }

        resetTomorrowTimer();
    });

    resetTomorrowTimer();

    auto today = QDateTime::currentDateTime().date();
    setProperty("primaryForActionList", today);
}

NextAppCore::~NextAppCore()
{
#ifdef LINUX_BUILD
    LOG_DEBUG_N << "Disconnecting from D-Bus";
    if (dbus_connection_) {
        dbus_connection_->disconnect(
            "org.freedesktop.login1",                   // Service name
            "/org/freedesktop/login1",                  // Object path
            "org.freedesktop.login1.Manager",           // Interface name
            "PrepareForSleep",                          // Signal name
            this,
            SLOT(handlePrepareForSleep(bool)));
        dbus_connection_.reset();
    }
    LOG_DEBUG_N << "Done disconnecting from D-Bus";
#endif

    if (db_) {
        db_->close();
        db_.reset();
    }

    SoundPlayer::instance().close();
}


QDateTime NextAppCore::dateFromWeek(int year, int week)
{
    QDate date(year, 1, 1);
    date = date.addDays(7 * (week - 1));
    while (date.dayOfWeek() != Qt::Monday) {
        date = date.addDays(-1);
    }
    return QDateTime(date, QTime{0,0});
}

int NextAppCore::weekFromDate(const QDateTime &date)
{
    QDate d = date.date();
    int week = d.weekNumber();
    if (d.dayOfWeek() == Qt::Monday) {
        return week;
    }
    return week - 1;
}

WorkModel *NextAppCore::createWorkModel()
{
    // TODO: Use QQmlEngine::JavaScriptOwnership or track lifetime. Right now we leak memory.
    LOG_TRACE_N << "Creating a new WorkModel.";
    auto model = make_unique<WorkModel>();
    // Causes crash!
    //QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::JavaScriptOwnership);
    QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::CppOwnership);
    return model.release();
}

WeeklyWorkReportModel *NextAppCore::createWeeklyWorkReportModel()
{
    LOG_TRACE_N << "Creating WeeklyWorkReportModel";
    auto model = new WeeklyWorkReportModel(instance());
    //QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::JavaScriptOwnership);
    return model;
}

QString NextAppCore::toHourMin(int duration)
{
    return ::toHourMin(duration);
}

CalendarModel *NextAppCore::createCalendarModel()
{
    LOG_TRACE_N << "Creating CalendarModel";
    auto model = new CalendarModel();
    QQmlEngine::setObjectOwnership(model, QQmlEngine::JavaScriptOwnership);
    return model;
}

ReviewModel *NextAppCore::getReviewModel()
{
    auto *rm = &ReviewModel::instance();
    // Set CPP as ownership, because we manage the lifetime of the model.
    QQmlEngine::setObjectOwnership(rm, QQmlEngine::CppOwnership);
    return rm;
}

DevicesModel *NextAppCore::getDevicesModel()
{
    auto *dm = DevicesModel::instance();
    // Set CPP as ownership, because we manage the lifetime of the model.
    QQmlEngine::setObjectOwnership(dm, QQmlEngine::CppOwnership);
    return dm;
}

void NextAppCore::openFile(const QString &path)
{
    // Does not work in release mode under Ubuntu 23.10
    // if (const auto url = QUrl::fromLocalFile(path); url.isValid()) {
    //     LOG_DEBUG_N << "Opening file " << url.toString();
    //     try {
    //         QDesktopServices::openUrl(path);
    //     } catch (const std::exception &e) {
    //         LOG_ERROR_N << "Failed to open file " << path << " - " << e.what();
    //     }
    // }
}

void NextAppCore::emitSettingsChanged() {
    emit settingsChanged();
}

void NextAppCore::setProperty(const QString &name, const QVariant &value)
{
    LOG_TRACE_N << "Setting property " << name;
    properties_[name] = value;
    emit propertyChanged(name);
}

QVariant NextAppCore::getProperty(const QString &name) const noexcept {
    if (auto it = properties_.find(name); it != properties_.end()) {
        return it->second;
    }
    return {};
}

time_t NextAppCore::parseDateOrTime(const QString &str, time_t defaultDate)
{
    try {
        return ::parseDateOrTime(str, getDefaultDate(defaultDate));
    } catch (const std::exception &e) {
        LOG_DEBUG << "Could not parse time/date: " << str;
    }
    return -1;
}

time_t NextAppCore::parseTime(const QString &str)
{
    try {
        return ::parseDuration(str);
    } catch (const std::exception &e) {
        LOG_DEBUG << "Could not parse time: " << str;
    }
    return -1;
}

QString NextAppCore::toDateAndTime(time_t when, time_t defaultDate)
{
    const auto dd = getDefaultDate(defaultDate);
    const auto actualTime = QDateTime::fromSecsSinceEpoch(when);

    if (dd == actualTime.date()) {
        return actualTime.time().toString("hh:mm");
    }

    return QDateTime::fromSecsSinceEpoch(when).toString("yyyy-MM-dd hh:mm");
}

QString NextAppCore::toTime(time_t when)
{
    LOG_TRACE_N << "Converting time " << when;
    const auto actualTime = QDateTime::fromSecsSinceEpoch(when);
    return actualTime.time().toString("hh:mm");
}

void NextAppCore::playSound(double volume, const QString &soundFile)
{
    LOG_DEBUG_N << "Playing sound " << soundFile
                << " at volume " << volume;

    SoundPlayer::instance().playSound(soundFile, volume);
}

void NextAppCore::playSoundDelayed(int delayMs, double volume, const QString &soundFile)
{
    volume_ = volume;
    sound_file_ = soundFile.isEmpty() ? QStringLiteral("qrc:/qt/qml/NextAppUi/sounds/387351__cosmicembers__simple-ding.wav") : soundFile;
    audio_play_delay_.setSingleShot(true);
    audio_play_delay_.start(delayMs);
}

void NextAppCore::bootstrapDevice(bool newUser)
{
    LOG_INFO_N << "Signup is completed";

    if (newUser) {
        LOG_DEBUG_N << "Creating default categories";
        // Creating default categories
        nextapp::pb::ActionCategory work, priv, hobby, family;
        work.setColor("dodgerblue");
        work.setName("Work");

        priv.setColor("yellow");
        priv.setName("Private");

        hobby.setColor("green");
        hobby.setName("Hobby");

        family.setColor("wheat");
        family.setName("Family");

        ServerComm::instance().createActionCategory(work);
        ServerComm::instance().createActionCategory(priv);
        ServerComm::instance().createActionCategory(hobby);
        ServerComm::instance().createActionCategory(family);
    }
}

void NextAppCore::deleteAccount()
{
    doDeleteAccount();
}

void NextAppCore::deleteLocalData()
{
    onAccountDeleted();
}

time_t NextAppCore::parseHourMin(const QString &str)
{
    try {
        return ::parseDuration(str);
    } catch (const std::exception &e) {
        LOG_DEBUG << "Could not parse hour/min: " << str;
    }

    return -1;
}

QCoro::Task<void> NextAppCore::modelsAreCreated()
{
    LOG_INFO << "All models are created. Starting to initialize NextappCore...";
    co_await db_->init();
    bool emitted = false;

    QTimer::singleShot(100ms, this, [this] {
        if (ServerComm::instance().status() == ServerComm::Status::READY_TO_CONNECT) {
            LOG_DEBUG << "Starting server-comm...";
            ServerComm::instance().start();
        }
    });

    co_return;
}

QQmlApplicationEngine &NextAppCore::engine()
{
    assert(engine_);
    return *engine_;
}

int NextAppCore::width() const noexcept
{
    if (width_ > 0) {
        return width_;
    }
    return qApp->primaryScreen()->geometry().width();
}

int NextAppCore::height() const noexcept
{
    if (height_ > 0) {
        return height_;
    }
    return qApp->primaryScreen()->geometry().height();
}

void NextAppCore::setDragEnabled(bool drag_enabled) {
    if (drag_enabled_ == drag_enabled) {
        return;
    }
    LOG_TRACE_N << "Drag enabled: " << drag_enabled;
    drag_enabled_ = drag_enabled;
    emit dragEnabledChanged();
}

void NextAppCore::showSyncPopup(bool visible)
{
    if (visible && !sync_popup_) {
        sync_popup_ = openQmlComponent(QUrl(QStringLiteral("qrc:/qt/qml/NextAppUi/qml/SynchPopup.qml")));
        if (!sync_popup_) {
            return;
        }
    }

    assert(sync_popup_ != nullptr);
    auto * rootObject = engine().rootObjects().first();

    if (QQuickWindow *window = qobject_cast<QQuickWindow*>(rootObject)) {
        if (QQuickItem* item = qobject_cast<QQuickItem*>(sync_popup_)) {
            item->setParentItem(window->contentItem());
            sync_popup_->setProperty("visible", visible);
        }
    }
}

QObject *NextAppCore::openQmlComponent(const QUrl &resourcePath)
{
    QQmlComponent qcomponent(&engine(), resourcePath);

    if (qcomponent.isError()) {
        LOG_WARN_N << "Failed to create QML component: " << resourcePath.toString();
        for(const auto& err : qcomponent.errors()) {
            LOG_WARN_N << "  " << err.toString();
        }
        return {};
    }

    if (auto item = qcomponent.create()) {
        auto * root_object = engine().rootObjects().first();
        assert(root_object);

        if (QQuickWindow *window = qobject_cast<QQuickWindow*>(root_object)) {
            //auto name = item->metaObject()->className();
            if (QQuickItem *qitem = qobject_cast<QQuickItem*>(item)) {
                qitem->setParentItem(window->contentItem());  // Attach to main window
                qitem->setProperty("visible", true);
            }
        }
        return item;
    }

    LOG_WARN_N << "Failed to create QML component ["
               << resourcePath.toString()
               << "]: " << qcomponent.errorString();
    return {};
}

void NextAppCore::onWokeFromSleep()
{
    LOG_TRACE_N << "Emitting wokeFromSleep()";
    emit wokeFromSleep();
}

QCoro::Task<void> NextAppCore::onAccountDeleted()
{
    co_await db().closeAndDeleteDb();
    QSettings settings;
    settings.clear(); // Clear all settings
    settings.sync();
    ServerComm::instance().resetSignupStatus();
    emit accountDeleted();
}

void NextAppCore::handlePrepareForSleep(bool sleep)
{
    LOG_DEBUG_N << "Prepare for sleep: " << sleep;

    if (sleep) {
        LOG_INFO_N << "The system is going to sleep/suspended state! Disconnecting from server...";
        setState(State::SUSPENDED);
        emit suspending();
        //ServerComm::instance().stop();
    }
    if (!sleep) {
        LOG_INFO << "The system just wake up from sleep!";
        setState(State::ACTIVE);
        emit wokeFromSleep();
        resetTomorrowTimer();
    }
}

void NextAppCore::setState(State state)
{
    if (state_ != state) {
        LOG_DEBUG_N << "State changed to " << static_cast<int>(state) << " from " << static_cast<int>(state_);
        state_ = state;
        emit stateChanged();
    }
}

void NextAppCore::resetTomorrowTimer() {

    tomorrow_timer_.setTimerType(Qt::VeryCoarseTimer);

    const auto today = QDateTime::currentDateTime().date();
    const auto tomorrow = today.addDays(1);

    if (today_) {
        if (*today_ != today) {
            LOG_INFO << "The current date changed before the timer went off.";
            emit currentDateChanged();
            today_.reset();
        }
    }

    // Find the chrono time-point of the next day at 00:00:10
    const auto tomorrow_time = QDateTime(tomorrow, QTime(0, 0, 10));
    LOG_TRACE_N << "Setting tomorrow timer at " << tomorrow_time.toString();

    // How many seconds from now until tomorrow_time?
    const auto seconds = QDateTime::currentDateTime().secsTo(tomorrow_time);

    tomorrow_timer_.singleShot(chrono::seconds(seconds), [this]() {
        LOG_DEBUG << "Tomorrow timer fired!";
        const auto today = QDateTime::currentDateTime().date();
        if (today_ && *today_ != today) {
            LOG_INFO << "The current date has changed.";
            emit currentDateChanged();
            today_.reset();
        };
        if (state_ != State::SHUTTING_DOWN) {
            resetTomorrowTimer();
        }
    });
}

QCoro::Task<void> NextAppCore::doDeleteAccount()
{
    try {
        LOG_DEBUG_N << "Deleting account...";

        auto result = co_await ServerComm::instance().deleteAccount();
        if (result.error() != nextapp::pb::ErrorGadget::Error::OK) {
            LOG_ERROR_N << "Failed to delete account: " << result.message();
            emit accountDeletionFailed(tr("Failed to delete the account: %1").arg(result.message()));
            co_return;
        }
        LOG_INFO_N << "Account successfully deleted.";
        co_return;

    } catch (const std::exception &e) {
        LOG_ERROR_N << "Failed to delete account: " << e.what();
        emit accountDeletionFailed(tr("Failed to delete the account: %1").arg(e.what()));
        co_return;
    }

    emit accountDeletionFailed("Failed to delete the account: Unknown error. Please try again later.");
}
