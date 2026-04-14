#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <variant>

#include <QFile>
#include <QObject>
#include <QDate>
#include <QUuid>

#include "nextapp_client.grpc.qpb.h"
#include "qcorotask.h"
#include "GrpcIncomingStream.h"

class ServerCommAccess : public QObject
{
    Q_OBJECT

public:
    enum Status {
        MANUAL_OFFLINE,
        OFFLINE,
        READY_TO_CONNECT,
        CONNECTING,
        INITIAL_SYNC,
        ONLINE,
        ERROR
    };

    struct CbError {
        nextapp::pb::ErrorGadget::Error error;
        QString message;
    };

    template <typename T>
    using callback_arg_t = std::variant<CbError, T>;

    template <typename T>
    using callback_t = std::function<void(callback_arg_t<T>)>;

    using colors_in_months_t = std::shared_ptr<QList<QUuid>>;

    struct MetaData {
        std::optional<bool> more;
        QUuid requester;
    };

    using write_export_fn_t = std::function<void(const nextapp::pb::Status& msg, QFile& file)>;
    using read_export_fn_t = std::function<bool(nextapp::pb::Status& msg)>;

    using QObject::QObject;
    ~ServerCommAccess() override = default;

    [[nodiscard]] virtual bool connected() const noexcept = 0;
    virtual Status status() const noexcept = 0;
    virtual nextapp::pb::UserGlobalSettings getGlobalSettings() const = 0;
    virtual const nextapp::pb::UserGlobalSettings& globalSettings() const noexcept = 0;
    virtual const nextapp::pb::DataVersionsInfo& getServerDataVersions() const noexcept = 0;
    virtual void setLocalActionCategoryVersion(uint64_t version) = 0;
    virtual bool shouldUseUpdatedIdSync() const noexcept = 0;
    virtual void resync() = 0;
    virtual void fetchDay(int year, int month, int day) = 0;
    virtual void getDayColorDefinitions() = 0;
    virtual void setDay(const nextapp::pb::CompleteDay& day) = 0;
    virtual void addNode(const nextapp::pb::Node& node) = 0;
    virtual void updateNode(const nextapp::pb::Node& node) = 0;
    virtual void moveNode(const QUuid& uuid, const QUuid& toParentUuid) = 0;
    virtual void deleteNode(const QUuid& uuid) = 0;
    virtual void addAction(const nextapp::pb::Action& action) = 0;
    virtual void updateAction(const nextapp::pb::Action& action) = 0;
    virtual void updateActions(const nextapp::pb::UpdateActionsReq& action) = 0;
    virtual void deleteAction(const QString& actionUuid) = 0;
    virtual void markActionAsDone(const QString& actionUuid, bool done) = 0;
    virtual void markActionAsFavorite(const QString& actionUuid, bool favorite) = 0;
    virtual void startWork(const QString& actionId, bool activate = false) = 0;
    virtual void addWorkFromTimeBlock(const QString& timeBlockUuid) = 0;
    virtual void pauseWork(const QString& sessionId) = 0;
    virtual void resumeWork(const QString& sessionId) = 0;
    virtual void doneWork(const QString& sessionId) = 0;
    virtual void touchWork(const QString& sessionId) = 0;
    virtual void sendWorkEvent(const QString& sessionId, const nextapp::pb::WorkEvent& event) = 0;
    virtual void sendWorkEvents(const nextapp::pb::AddWorkEventReq& reqt) = 0;
    virtual void deleteWork(const QString& sessionId) = 0;
    virtual void getDetailedWorkSummary(const nextapp::pb::DetailedWorkSummaryRequest& req, const QUuid& requester) = 0;
    virtual void addWork(const nextapp::pb::WorkSession& ws) = 0;
    virtual void moveAction(const QString& actionUuid, const QString& nodeUuid) = 0;
    virtual void addTimeBlock(const nextapp::pb::TimeBlock& tb) = 0;
    virtual void updateTimeBlock(const nextapp::pb::TimeBlock& tb) = 0;
    virtual void deleteTimeBlock(const QString& timeBlockUuid) = 0;
    virtual void createActionCategory(const nextapp::pb::ActionCategory& category, callback_t<nextapp::pb::Status>&& done) = 0;
    virtual void updateActionCategory(const nextapp::pb::ActionCategory& category, callback_t<nextapp::pb::Status>&& done) = 0;
    virtual void deleteActionCategory(const QString& id, callback_t<nextapp::pb::Status>&& done) = 0;
    virtual void requestOtp(callback_t<nextapp::pb::Status>&& done) = 0;
    virtual std::shared_ptr<GrpcIncomingStream> synchGreenDays(const nextapp::pb::GetNewReq& req) = 0;
    virtual QCoro::Task<nextapp::pb::Status> getNewDayColorDefinitions(const nextapp::pb::GetNewReq& req) = 0;
    virtual std::shared_ptr<GrpcIncomingStream> synchNodes(const nextapp::pb::GetNewReq& req) = 0;
    virtual std::shared_ptr<GrpcIncomingStream> synchActions(const nextapp::pb::GetNewReq& req) = 0;
    virtual QCoro::Task<nextapp::pb::Status> getActionCategories(const nextapp::pb::Empty& req) = 0;
    virtual std::shared_ptr<GrpcIncomingStream> synchWorkSessions(const nextapp::pb::GetNewReq& req) = 0;
    virtual std::shared_ptr<GrpcIncomingStream> synchTimeBlocks(const nextapp::pb::GetNewReq& req) = 0;
    virtual std::shared_ptr<GrpcIncomingStream> synchNotifications(const nextapp::pb::GetNewReq& req) = 0;
    virtual QCoro::Task<nextapp::pb::Status> fetchDevices() = 0;
    virtual QCoro::Task<nextapp::pb::Status> enableDevice(const QString& deviceId, bool enabled) = 0;
    virtual QCoro::Task<nextapp::pb::Status> deleteDevice(const QString& deviceId) = 0;
    virtual QCoro::Task<void> setLastReadNotification(uint32_t id) = 0;
    virtual QCoro::Task<void> createNodesFromTemplate(nextapp::pb::NodeTemplate root) = 0;
    virtual QCoro::Task<void> resetNodes(nextapp::pb::ResetNodesReq req) = 0;
    virtual QCoro::Task<void> exportData(std::shared_ptr<QFile> file, const write_export_fn_t& write) = 0;
    virtual QCoro::Task<void> importData(const read_export_fn_t& read) = 0;

signals:
    void connectedChanged();
    void globalSettingsChanged();
    void firstDayOfWeekChanged();
    void statusChanged();
    void receivedDay(const nextapp::pb::CompleteDay& day);
    void receivedDayColorDefinitions(const nextapp::pb::DayColorDefinitions& defs);
    void receivedCurrentWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions>& sessions);
    void receivedDetailedWorkSummary(const nextapp::pb::DetailedWorkSummary& summary, const MetaData& meta);
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);
    void dataUpdated();
};
