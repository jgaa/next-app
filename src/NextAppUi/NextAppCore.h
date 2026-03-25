#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QQmlApplicationEngine>
#include <QGuiApplication>
#include <QVariantMap>
#include <optional>

#include "DbStore.h"
#include "RuntimeServices.h"
#include "WorkModel.h"
#include "WeeklyWorkReportModel.h"
#include "ReviewModel.h"
#include "DevicesModel.h"

#ifdef LINUX_BUILD
#include <QDBusConnection>
#endif

#include "qcorotask.h"

class CalendarModel;
class ServerComm;

class NextAppCore : public QObject
    , public RuntimeServices
{
    Q_OBJECT
    QML_ELEMENT
public:

    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(bool isMobile READ isMobile CONSTANT)
    Q_PROPERTY(bool isDebugBuild READ isDebugBuild CONSTANT)
    Q_PROPERTY(bool hasPushNotifications READ hasPushNotifications CONSTANT)
    Q_PROPERTY(bool isMobileSimulation READ isMobileSimulation CONSTANT)
    Q_PROPERTY(int width READ width NOTIFY widthChanged FINAL)
    Q_PROPERTY(int height READ height NOTIFY heightChanged FINAL)
    Q_PROPERTY(bool dragEnabled READ dragEnabled WRITE setDragEnabled NOTIFY dragEnabledChanged)
    Q_PROPERTY(bool lookupRelated MEMBER lookupRelated_ WRITE setLookupRelated  NOTIFY lookupRelatedChanged)
    Q_PROPERTY(nextapp::pb::UserDataInfo dbInfo READ getDbInfo NOTIFY dbInfoChanged)
    Q_PROPERTY(QModelIndex selectNode MEMBER selectNode_ NOTIFY selectNodeChanged)
    Q_PROPERTY(QString selectAction MEMBER selectAction_ NOTIFY selectActionChanged)
    Q_PROPERTY(bool plansEnabled READ plansEnabled NOTIFY plansEnabledChanged)
    Q_PROPERTY(nextapp::pb::Subscription currentPlan READ currentPlan NOTIFY currentPlanChanged)
    Q_PROPERTY(QVariantMap currentPlanView READ currentPlanView NOTIFY currentPlanChanged)
    Q_PROPERTY(bool hasCurrentPlan READ hasCurrentPlan NOTIFY currentPlanChanged)
    Q_PROPERTY(bool currentPlanLoading READ currentPlanLoading NOTIFY currentPlanLoadingChanged)
    Q_PROPERTY(bool paymentsPageLoading READ paymentsPageLoading NOTIFY paymentsPageLoadingChanged)
    Q_PROPERTY(QString lastPaymentsUrl READ lastPaymentsUrl NOTIFY lastPaymentsUrlChanged)

    enum class ClickInitiator {
        NONE,
        TREE,
        ACTIONS,
        SESSIONS
    };

    Q_ENUM(ClickInitiator)

    Q_PROPERTY(ClickInitiator clickInitiator MEMBER clickInitiator_ WRITE setClickInitiator NOTIFY selectNodeChanged)

    // True if the app was build with the CMAKE option DEVEL_SETTINGS enabled
    Q_PROPERTY(bool develBuild READ isDevelBuild CONSTANT)
    enum class State {
        STARTING_UP,
        ACTIVE,
        HIDDEN,
        INACTIVE,
        SUSPENDED,
        SHUTTING_DOWN
    };

    Q_ENUM(State)

    NextAppCore(QQmlApplicationEngine& engine);
    ~NextAppCore() override;

    static QString qtVersion() {
        return QT_VERSION_STR;
    }

    static Q_INVOKABLE QDateTime dateFromWeek(int year, int week);
    static Q_INVOKABLE int weekFromDate(const QDateTime& date);
    static Q_INVOKABLE WorkModel *createWorkModel();
    static Q_INVOKABLE WeeklyWorkReportModel *createWeeklyWorkReportModel();
    static Q_INVOKABLE QString toHourMin(int duration);
    static Q_INVOKABLE CalendarModel *createCalendarModel();
    static Q_INVOKABLE ReviewModel *getReviewModel();
    static Q_INVOKABLE DevicesModel *getDevicesModel();
    Q_INVOKABLE void openFile(const QString& path);
    //Q_INVOKABLE void emitSettingsChanged();
    Q_INVOKABLE void setProperty(const QString& name, const QVariant& value);
    Q_INVOKABLE QVariant getProperty(const QString& name) const noexcept;
    static Q_INVOKABLE void debugLog(const QString message);
    static Q_INVOKABLE void settingsWasChanged();

    // returns -1 on error
    static Q_INVOKABLE time_t parseDateOrTime(const QString& str, time_t defaultDate = 0);
    static Q_INVOKABLE time_t parseTime(const QString& str);
    static Q_INVOKABLE QString toDateAndTime(time_t when, time_t defaultDate = 0);
    static Q_INVOKABLE QString toTime(time_t when);
    Q_INVOKABLE void playSound(double volume, const QString& soundFile) override;
    Q_INVOKABLE void playSoundDelayed(int delayMs, double volume, const QString& soundFile);
    // Called when signup is complete
    Q_INVOKABLE void bootstrapDevice(bool newUser);
    Q_INVOKABLE void deleteAccount();
    Q_INVOKABLE void deleteLocalData();
    Q_INVOKABLE void factoryReset();
    Q_INVOKABLE void currentActionSelected(const QString& uuid);
    Q_INVOKABLE void setSelectAction(const QString& uuid);

    // returns -1 on error
    static Q_INVOKABLE time_t parseHourMin(const QString& str);

    bool isMobile() const noexcept {
#if defined (__ANDROID__) || defined (USE_ANDROID_UI)
        return true;
#else
        return false;
#endif
    }

    bool isMobileSimulation() const noexcept {
#if defined (USE_ANDROID_UI)
        return true;
#else
        return false;
#endif
}

    bool isDebugBuild() const noexcept {
#if defined (_DEBUG)
        return true;
#else
        return false;
#endif
    }

    bool hasPushNotifications() const noexcept {
#if defined (WITH_FCM)
        return true;
#else
        return isMobileSimulation();
#endif
    }

    static constexpr bool isDevelBuild() {
#if defined(DEVEL_SETTINGS)
        return true;
#else
        return false;
#endif
    }

    void setOnline(bool online) override {
        emit onlineChanged(online);
    }

    QCoro::Task<void> modelsAreCreated();

    static NextAppCore *instance() {
        assert(instance_ != nullptr);
        return instance_;
    }

    QQmlApplicationEngine& engine();

    int width() const noexcept;
    int height() const noexcept;
    void setWidth(int width);
    void setHeight(int height);

    bool dragEnabled() const noexcept {
        return drag_enabled_;
    }

    void setDragEnabled(bool drag_enabled);

    DbStore& db() const noexcept override {
        assert(db_);
        return *db_;
    }

    ServerCommAccess& serverComm() const noexcept override;
    SettingsAccess& settings() const noexcept override;
    QVariant appProperty(const QString& name) const noexcept override;
    void setAppProperty(const QString& name, const QVariant& value) override;
    QQmlEngine& qmlEngine() const noexcept override;
    bool isMobileUi() const noexcept override;

    void showSyncPopup(bool visible) override;

    State state() const noexcept {
        return state_;
    }

    bool isActive() const noexcept {
        return state() == State::ACTIVE;
    }

    QObject * openQmlComponent(const QUrl& resourcePath);

    void onWokeFromSleep();
    QCoro::Task<void> onAccountDeleted() override;
    void showUnrecognizedDeviceError() override;
    QObject& appEventSource() noexcept override;

    static void handleSharedFile(const QString& path);
    static void tryImportWhenReady(const QString& path);

    /*! Run the function is the app is instattiated and in ACTIVE state, otherwise queue it to run later.
     *  This is useful for functions that need to be run after the app is fully initialized.
     */
    static void runOrQueueFunction(std::function<void()> fn);

    void setLookupRelated(bool lookupRelated);
    void setClickInitiator(ClickInitiator initiator);

    nextapp::pb::UserDataInfo getDbInfo();

    bool plansEnabled() const noexcept {
        return plans_enabled_;
    }

    void setPlansEnabled(bool enable) override;

    nextapp::pb::Subscription currentPlan();
    QVariantMap currentPlanView() const;
    Q_INVOKABLE void ensureCurrentPlanLoaded(bool forceRefresh = false);
    Q_INVOKABLE void getPaymentsUrl();

    bool hasCurrentPlan() const noexcept {
        return current_plan_.has_value();
    }

    bool currentPlanLoading() const noexcept {
        return current_plan_fetching_;
    }

    bool paymentsPageLoading() const noexcept {
        return payments_page_loading_;
    }

    QString lastPaymentsUrl() const noexcept {
        return last_payments_url_;
    }

public slots:
    void handlePrepareForSleep(bool sleep);

signals:
    void allBaseModelsCreated();
    void onlineChanged(bool online);
    void widthChanged();
    void heightChanged();
    void dragEnabledChanged();
    void settingsChanged();
    void propertyChanged(const QString& name);
    void stateChanged();
    void wokeFromSleep();
    void suspending();
    void currentDateChanged(); // Should happen just after midnight
    void accountDeleted();
    void factoryResetDone();
    void accountDeletionFailed(const QString& message);
    void importEvent(const QUrl& url);
    void lookupRelatedChanged();
    void dbInfoChanged();
    void selectNodeChanged();
    void selectActionChanged();
    void clickInitiatorChanged();
    void plansEnabledChanged();
    void currentPlanChanged();
    void currentPlanLoadingChanged();
    void paymentsPageLoadingChanged();
    void lastPaymentsUrlChanged();
    void paymentsPageOpened(const QString& url);

private:
    void setState(State state);
    void resetTomorrowTimer();
    QCoro::Task<void> doDeleteAccount();
    QCoro::Task<void> doFactoryReset();
    void emitSettingsChanged();
    QCoro::Task<void> refreshDbData();
    void selectNode(const QString& uuid);
    QCoro::Task<void> fetchCurrentPlan(bool forceRefresh);
    QCoro::Task<void> doGetPaymentsUrl();

    static NextAppCore *instance_;
    State state_{State::STARTING_UP};
    std::unique_ptr<DbStore> db_;
    std::unique_ptr<SettingsAccess> settings_;
    std::unique_ptr<ServerComm> server_comm_;
    QObject* sync_popup_{} ;

    int height_{0};
    int width_{0};
    bool drag_enabled_{};
    QTimer audio_play_delay_;
    QTimer tomorrow_timer_;
    QString sound_file_;
    double volume_{};
    std::map<QString, QVariant> properties_;
    std::optional<QDate> today_;
#ifdef LINUX_BUILD
    std::unique_ptr<QDBusConnection> dbus_connection_;
#endif
    QQmlApplicationEngine *engine_{};
    static std::deque<std::function<void()>> pre_instance_callbacks_;
    bool lookupRelated_{true}; // magnet
    nextapp::pb::UserDataInfo db_info_cached_;
    time_t db_info_last_update_{0};
    QModelIndex selectNode_;
    QString selectAction_;
    ClickInitiator clickInitiator_{ClickInitiator::NONE};
    bool plans_enabled_{false};
    std::optional<nextapp::pb::Subscription> current_plan_;
    bool current_plan_fetching_{false};
    bool payments_page_loading_{false};
    QString last_payments_url_;
};
