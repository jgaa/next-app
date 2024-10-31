#include "NextAppCore.h"

#include <CalendarModel.h>
#include <QDateTime>
#include <QDesktopServices>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

#include "WeeklyWorkReportModel.h"
#include "util.h"

using namespace std;

namespace {
QDate getDefaultDate(time_t when) {
    if (when > 0) {
        return QDateTime::fromSecsSinceEpoch(when).date();
    }
    return QDate::currentDate();
}
} // anon ns

NextAppCore *NextAppCore::instance_;

NextAppCore::NextAppCore() {
    assert(instance_ == nullptr);
    instance_ = this;
    drag_enabled_ = !isMobile();

#ifdef USE_ANDROID_UI
    LOG_DEBUG_N << "Android UI";
    width_ = 350;
    height_ = 750;
#else
    connect(app_, &QGuiApplication::primaryScreenChanged, [this](QScreen *screen) {
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

    db_->init();
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
    auto model = make_unique<WorkModel>();
    QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::JavaScriptOwnership);
    return model.release();
}

WeeklyWorkReportModel *NextAppCore::createWeeklyWorkReportModel()
{
    LOG_DEBUG_N << "Creating WeeklyWorkReportModel";
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
    LOG_DEBUG_N << "Creating CalendarModel";
    auto model = new CalendarModel();
    QQmlEngine::setObjectOwnership(model, QQmlEngine::JavaScriptOwnership);
    return model;
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
    const auto actualTime = QDateTime::fromSecsSinceEpoch(when);
    return actualTime.time().toString("hh:mm");
}

void NextAppCore::playSound(double volume, const QString &soundFile)
{
    if (!audio_output_) {
        audio_output_.emplace();
    }
    if (!audio_player_) {
        audio_player_.emplace();
    }

    audio_output_->setVolume(volume);
    audio_player_->setAudioOutput(&audio_output_.value());

    // QUrl don't support QStringView
    QString file = soundFile;
    if (file.isEmpty()) {
        file = "qrc:/qt/qml/NextAppUi/sounds/387351__cosmicembers__simple-ding.wav";
    }

    audio_player_->setSource(QUrl(file));

    LOG_DEBUG_N << "Playing sound " << file
                << " at volume " << volume;
    audio_player_->play();
}

void NextAppCore::playSoundDelayed(int delayMs, double volume, const QString &soundFile)
{
    volume_ = volume;
    sound_file_ = soundFile;
    audio_play_delay_.setSingleShot(true);
    audio_play_delay_.start(delayMs);
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

QQmlApplicationEngine &NextAppCore::engine()
{
    static QQmlApplicationEngine engine_;
    return engine_;
}

int NextAppCore::width() const noexcept
{
    if (width_ > 0) {
        return width_;
    }
    return app_->primaryScreen()->geometry().width();
}

int NextAppCore::height() const noexcept
{
    if (height_ > 0) {
        return height_;
    }
    return app_->primaryScreen()->geometry().height();
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
    if (visible && !sync_popup_component_) {
        sync_popup_component_ = make_unique<QQmlComponent>(&engine(), QUrl(QStringLiteral("qrc:/qt/qml/NextAppUi/qml/SynchPopup.qml")));
        auto* sync_popup_ = sync_popup_component_->create();
    }

    //assert(sync_popup_ != nullptr);
    if (!sync_popup_) {
        LOG_WARN_N << "Sync popup not created";
        return;
    }
    auto * rootObject = engine().rootObjects().first();

    if (QQuickWindow *window = qobject_cast<QQuickWindow*>(rootObject)) {
        if (QQuickItem* item = qobject_cast<QQuickItem*>(sync_popup_)) {
            item->setParentItem(window->contentItem());
            sync_popup_->setProperty("visible", visible);
        }
    }
}


