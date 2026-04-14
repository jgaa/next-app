#pragma once

#include <optional>
#include <utility>

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QtGlobal>

#include "RuntimeServices.h"

class FakeSettingsAccess final : public SettingsAccess
{
public:
    QVariant value(const QString& key, const QVariant& default_value = {}) const override
    {
        return values_.value(key, default_value);
    }

    void setValue(const QString& key, const QVariant& value) override
    {
        values_.insert(key, value);
    }

    void remove(const QString& key) override
    {
        values_.remove(key);
    }

    void sync() override
    {
        ++sync_calls_;
    }

    QHash<QString, QVariant> values_;
    int sync_calls_{0};
};

class FakeServerComm final : public ServerCommAccess
{
public:
    using ServerCommAccess::ServerCommAccess;

    bool connected() const noexcept override { return connected_; }
    Status status() const noexcept override { return status_; }
    nextapp::pb::UserGlobalSettings getGlobalSettings() const override { return global_settings_; }
    const nextapp::pb::UserGlobalSettings& globalSettings() const noexcept override { return global_settings_; }
    const nextapp::pb::DataVersionsInfo& getServerDataVersions() const noexcept override { return data_versions_; }
    void setLocalActionCategoryVersion(uint64_t version) override { local_action_category_version_ = version; }
    bool shouldUseUpdatedIdSync() const noexcept override { return should_use_updated_id_sync_; }
    void resync() override { ++resync_calls_; }
    void fetchDay(int year, int month, int day) override { last_fetch_day_ = QDate{year, month, day}; }
    void getDayColorDefinitions() override { ++get_day_color_definitions_calls_; }
    void setDay(const nextapp::pb::CompleteDay& day) override { last_day_ = day; }
    void addNode(const nextapp::pb::Node& node) override { added_nodes_.append(node); }
    void updateNode(const nextapp::pb::Node& node) override { updated_nodes_.append(node); }
    void moveNode(const QUuid& uuid, const QUuid& toParentUuid) override { moved_nodes_.append({uuid, toParentUuid}); }
    void deleteNode(const QUuid& uuid) override { deleted_nodes_.append(uuid); }
    void addAction(const nextapp::pb::Action& action) override { added_actions_.append(action); }
    void updateAction(const nextapp::pb::Action& action) override { updated_actions_.append(action); }
    void updateActions(const nextapp::pb::UpdateActionsReq& action) override { last_update_actions_req_ = action; }
    void deleteAction(const QString& actionUuid) override { deleted_actions_.append(actionUuid); }
    void markActionAsDone(const QString& actionUuid, bool done) override { marked_done_.append({actionUuid, done}); }
    void markActionAsFavorite(const QString& actionUuid, bool favorite) override { marked_favorite_.append({actionUuid, favorite}); }
    void startWork(const QString& actionId, bool activate = false) override { started_work_.append({actionId, activate}); }
    void addWorkFromTimeBlock(const QString& timeBlockUuid) override { added_work_from_time_block_.append(timeBlockUuid); }
    void pauseWork(const QString& sessionId) override { paused_work_.append(sessionId); }
    void resumeWork(const QString& sessionId) override { resumed_work_.append(sessionId); }
    void doneWork(const QString& sessionId) override { done_work_.append(sessionId); }
    void touchWork(const QString& sessionId) override { touched_work_.append(sessionId); }
    void sendWorkEvent(const QString& sessionId, const nextapp::pb::WorkEvent& event) override {
        sent_work_events_.append({sessionId, event});
    }
    void sendWorkEvents(const nextapp::pb::AddWorkEventReq& reqt) override { last_add_work_event_req_ = reqt; }
    void deleteWork(const QString& sessionId) override { deleted_work_.append(sessionId); }
    void getDetailedWorkSummary(const nextapp::pb::DetailedWorkSummaryRequest& req, const QUuid& requester) override {
        last_work_summary_request_ = req;
        last_work_summary_requester_ = requester;
    }
    void addWork(const nextapp::pb::WorkSession& ws) override { added_work_.append(ws); }
    void moveAction(const QString& actionUuid, const QString& nodeUuid) override { moved_actions_.append({actionUuid, nodeUuid}); }
    void addTimeBlock(const nextapp::pb::TimeBlock& tb) override { added_time_blocks_.append(tb); }
    void updateTimeBlock(const nextapp::pb::TimeBlock& tb) override { updated_time_blocks_.append(tb); }
    void deleteTimeBlock(const QString& timeBlockUuid) override { deleted_time_blocks_.append(timeBlockUuid); }
    void createActionCategory(const nextapp::pb::ActionCategory& category, callback_t<nextapp::pb::Status>&& done) override {
        created_categories_.append(category);
        if (done) {
            done(nextapp::pb::Status{});
        }
    }
    void updateActionCategory(const nextapp::pb::ActionCategory& category, callback_t<nextapp::pb::Status>&& done) override {
        updated_categories_.append(category);
        if (done) {
            done(nextapp::pb::Status{});
        }
    }
    void deleteActionCategory(const QString& id, callback_t<nextapp::pb::Status>&& done) override {
        deleted_categories_.append(id);
        if (done) {
            done(nextapp::pb::Status{});
        }
    }
    void requestOtp(callback_t<nextapp::pb::Status>&& done) override {
        ++request_otp_calls_;
        if (done) {
            if (request_otp_error_) {
                done(*request_otp_error_);
            } else {
                done(request_otp_response_);
            }
        }
    }
    std::shared_ptr<GrpcIncomingStream> synchGreenDays(const nextapp::pb::GetNewReq& req) override {
        last_green_days_req_ = req;
        return {};
    }
    QCoro::Task<nextapp::pb::Status> getNewDayColorDefinitions(const nextapp::pb::GetNewReq& req) override {
        last_day_color_defs_req_ = req;
        co_return nextapp::pb::Status{};
    }
    std::shared_ptr<GrpcIncomingStream> synchNodes(const nextapp::pb::GetNewReq& req) override {
        last_nodes_req_ = req;
        return {};
    }
    std::shared_ptr<GrpcIncomingStream> synchActions(const nextapp::pb::GetNewReq& req) override {
        last_actions_req_ = req;
        return {};
    }
    QCoro::Task<nextapp::pb::Status> getActionCategories(const nextapp::pb::Empty& req) override {
        last_empty_req_ = req;
        co_return get_action_categories_response_;
    }
    std::shared_ptr<GrpcIncomingStream> synchWorkSessions(const nextapp::pb::GetNewReq& req) override {
        last_work_sessions_req_ = req;
        return {};
    }
    std::shared_ptr<GrpcIncomingStream> synchTimeBlocks(const nextapp::pb::GetNewReq& req) override {
        last_time_blocks_req_ = req;
        return {};
    }
    std::shared_ptr<GrpcIncomingStream> synchNotifications(const nextapp::pb::GetNewReq& req) override {
        last_notifications_req_ = req;
        return {};
    }
    QCoro::Task<nextapp::pb::Status> fetchDevices() override {
        ++fetch_devices_calls_;
        co_return fetch_devices_response_;
    }
    QCoro::Task<nextapp::pb::Status> enableDevice(const QString& deviceId, bool enabled) override {
        enabled_devices_.append({deviceId, enabled});
        co_return enable_device_response_;
    }
    QCoro::Task<nextapp::pb::Status> deleteDevice(const QString& deviceId) override {
        deleted_devices_.append(deviceId);
        co_return delete_device_response_;
    }
    QCoro::Task<void> setLastReadNotification(uint32_t id) override {
        last_read_notification_id_ = id;
        co_return;
    }
    QCoro::Task<void> createNodesFromTemplate(nextapp::pb::NodeTemplate root) override {
        created_node_template_ = std::move(root);
        co_return;
    }
    QCoro::Task<void> resetNodes(nextapp::pb::ResetNodesReq req) override {
        reset_nodes_req_ = std::move(req);
        co_return;
    }
    QCoro::Task<void> exportData(std::shared_ptr<QFile> file, const write_export_fn_t& write) override {
        last_export_file_ = std::move(file);
        last_write_export_fn_ = write;
        co_return;
    }
    QCoro::Task<void> importData(const read_export_fn_t& read) override {
        last_read_export_fn_ = read;
        imported_messages_.clear();
        if (consume_import_read_immediately_) {
            nextapp::pb::Status msg;
            while (read(msg)) {
                imported_messages_.append(msg);
                msg = nextapp::pb::Status{};
            }
        }
        co_return;
    }

    void setConnectedForTest(bool connected)
    {
        if (connected_ == connected) {
            return;
        }
        connected_ = connected;
        emit connectedChanged();
    }

    void setStatusForTest(Status status)
    {
        if (status_ == status) {
            return;
        }
        status_ = status;
        emit statusChanged();
    }

    void emitUpdateForTest(const std::shared_ptr<nextapp::pb::Update>& update)
    {
        emit onUpdate(update);
    }

    bool connected_{false};
    Status status_{READY_TO_CONNECT};
    bool should_use_updated_id_sync_{false};
    uint64_t local_action_category_version_{0};
    int resync_calls_{0};
    int get_day_color_definitions_calls_{0};
    int request_otp_calls_{0};
    int fetch_devices_calls_{0};
    std::optional<QDate> last_fetch_day_;
    nextapp::pb::CompleteDay last_day_;
    nextapp::pb::UserGlobalSettings global_settings_;
    nextapp::pb::DataVersionsInfo data_versions_;
    QList<nextapp::pb::Node> added_nodes_;
    QList<nextapp::pb::Node> updated_nodes_;
    QList<QPair<QUuid, QUuid>> moved_nodes_;
    QList<QUuid> deleted_nodes_;
    QList<nextapp::pb::Action> added_actions_;
    QList<nextapp::pb::Action> updated_actions_;
    nextapp::pb::UpdateActionsReq last_update_actions_req_;
    QList<QString> deleted_actions_;
    QList<QPair<QString, bool>> marked_done_;
    QList<QPair<QString, bool>> marked_favorite_;
    QList<QPair<QString, bool>> started_work_;
    QList<QString> added_work_from_time_block_;
    QList<QString> paused_work_;
    QList<QString> resumed_work_;
    QList<QString> done_work_;
    QList<QString> touched_work_;
    QList<QPair<QString, nextapp::pb::WorkEvent>> sent_work_events_;
    nextapp::pb::AddWorkEventReq last_add_work_event_req_;
    nextapp::pb::DetailedWorkSummaryRequest last_work_summary_request_;
    QUuid last_work_summary_requester_;
    QList<QString> deleted_work_;
    QList<nextapp::pb::WorkSession> added_work_;
    QList<QPair<QString, QString>> moved_actions_;
    QList<nextapp::pb::TimeBlock> added_time_blocks_;
    QList<nextapp::pb::TimeBlock> updated_time_blocks_;
    QList<QString> deleted_time_blocks_;
    QList<nextapp::pb::ActionCategory> created_categories_;
    QList<nextapp::pb::ActionCategory> updated_categories_;
    QList<QString> deleted_categories_;
    nextapp::pb::GetNewReq last_green_days_req_;
    nextapp::pb::GetNewReq last_day_color_defs_req_;
    nextapp::pb::GetNewReq last_nodes_req_;
    nextapp::pb::GetNewReq last_actions_req_;
    nextapp::pb::GetNewReq last_work_sessions_req_;
    nextapp::pb::GetNewReq last_time_blocks_req_;
    nextapp::pb::GetNewReq last_notifications_req_;
    nextapp::pb::Empty last_empty_req_;
    QList<QPair<QString, bool>> enabled_devices_;
    QList<QString> deleted_devices_;
    uint32_t last_read_notification_id_{0};
    std::optional<CbError> request_otp_error_;
    nextapp::pb::Status request_otp_response_;
    nextapp::pb::Status get_action_categories_response_;
    nextapp::pb::Status fetch_devices_response_;
    nextapp::pb::Status enable_device_response_;
    nextapp::pb::Status delete_device_response_;
    nextapp::pb::NodeTemplate created_node_template_;
    nextapp::pb::ResetNodesReq reset_nodes_req_;
    std::shared_ptr<QFile> last_export_file_;
    write_export_fn_t last_write_export_fn_;
    read_export_fn_t last_read_export_fn_;
    QList<nextapp::pb::Status> imported_messages_;
    bool consume_import_read_immediately_{false};
};

class TestAppEventSource final : public QObject
{
    Q_OBJECT

signals:
    void wokeFromSleep();
    void settingsChanged();
    void suspending();
};

class TestRuntimeServices final : public RuntimeServices
{
public:
    explicit TestRuntimeServices(QObject* parent = nullptr)
        : server_comm_{parent}
    {
    }

    DbStore& db() const noexcept override
    {
        Q_ASSERT(db_);
        return *db_;
    }

    ServerCommAccess& serverComm() const noexcept override
    {
        return const_cast<FakeServerComm&>(server_comm_);
    }

    SettingsAccess& settings() const noexcept override
    {
        return const_cast<FakeSettingsAccess&>(settings_);
    }

    QVariant appProperty(const QString& name) const noexcept override
    {
        return app_properties_.value(name);
    }

    void setAppProperty(const QString& name, const QVariant& value) override
    {
        app_properties_.insert(name, value);
    }

    void playSound(double volume, const QString& sound_file) override
    {
        played_sounds_.append({volume, sound_file});
    }

    void setOnline(bool online) override
    {
        online_ = online;
    }

    void showSyncPopup(bool visible) override
    {
        sync_popup_visible_ = visible;
    }

    void setPlansEnabled(bool enable) override
    {
        plans_enabled_ = enable;
    }

    QCoro::Task<void> onAccountDeleted() override
    {
        account_deleted_called_ = true;
        co_return;
    }

    void showUnrecognizedDeviceError() override
    {
        unrecognized_device_error_shown_ = true;
    }

    QObject& appEventSource() noexcept override
    {
        return app_event_source_;
    }

    QQmlEngine& qmlEngine() const noexcept override
    {
        Q_UNREACHABLE_RETURN(*static_cast<QQmlEngine*>(nullptr));
    }

    bool isMobileUi() const noexcept override
    {
        return mobile_ui_;
    }

    void setDbForTest(DbStore& db) noexcept
    {
        db_ = &db;
    }

    FakeServerComm server_comm_;
    FakeSettingsAccess settings_;
    QHash<QString, QVariant> app_properties_;
    QList<QPair<double, QString>> played_sounds_;
    TestAppEventSource app_event_source_;
    bool online_{false};
    bool sync_popup_visible_{false};
    bool plans_enabled_{false};
    bool account_deleted_called_{false};
    bool unrecognized_device_error_shown_{false};
    bool mobile_ui_{false};
    DbStore* db_{};
};
