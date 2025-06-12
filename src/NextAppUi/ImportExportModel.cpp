#include <QDir>
#include <QProtobufJsonSerializer>

#include "ImportExportModel.h"
#include "ServerComm.h"

#include "logging.h"
#include "util.h"

namespace {

void WriteNextapp(const nextapp::pb::Status& msg, QFile& file) {
    QProtobufSerializer serializer;
    const auto data = msg.serialize(&serializer);

    // Save the length of the next protobuf message in big-endian format.
    quint32 be_len = qToBigEndian<quint32>(data.size());
    const auto be_ptr = reinterpret_cast<const char*>(&be_len);
    LOG_TRACE_N << "Writing " << data.size() << " bytes to file.";
    file.write(be_ptr, sizeof(be_ptr));
    file.write(data.data(), data.size());
}


enum class JsonSections {
    BEGIN,
    USER,
    SETTINGS,
    GREEN_DAYS_COLORS,
    GREEN_DAYS,
    NODES,
    CATEGORIES,
    ACTIONS,
    WORK_SESSIONS,
    TIME_BLOCKS,
    DONE
};

// Static variables are OK here, as exports are not called concurrently.
JsonSections json_current_section = JsonSections::BEGIN;
bool json_in_array = false;
bool json_in_array_list = false;

template <typename T>
void WriteItems(const T& items, QFile& file) {
    for(const auto item : items) {
        QProtobufJsonSerializer jsonSer;
        auto json = item.serialize(&jsonSer);

        if (json_in_array_list) {
            file.write(",\n");
        } else {
            json_in_array_list = true;
        }

        file.write(json.data(), json.size());
    }
}

void WriteJson(const nextapp::pb::Status& msg, QFile& file) {
    QProtobufJsonSerializer jsonSer;

    bool last_item = false;

    if (msg.hasHasMore() && !msg.hasMore()) {
        last_item = true;
    }

    auto clear_array = [&] () {
        if (json_in_array) {
            file.write("]");
            json_in_array = false;
            json_in_array_list = false;
        }
    };

    ScopedExit jsonGuard([&] () {
        if (last_item) {
            clear_array();
            file.write("\n}\n");
            json_current_section = JsonSections::DONE;
        }
    });

    if (json_current_section == JsonSections::DONE) {
        LOG_ERROR_N << "Attempting to write JSON after completion.";
        return;
    }

    if (json_current_section == JsonSections::BEGIN) {
        file.write("{\n");
    }

    // Only an object for a user-export.
    if (msg.hasUser()) {
        json_current_section = JsonSections::USER;
        auto json = msg.user().serialize(&jsonSer);
        file.write(R"("user": )");
        file.write(json.data(), json.size());
        file.write("\n");
        return;
    }

    if (msg.hasUserGlobalSettings()) {
        json_current_section = JsonSections::SETTINGS;
        auto json = msg.userGlobalSettings().serialize(&jsonSer);
        file.write(R"(, "settings": )");
        file.write(json.data(), json.size());
        file.write("\n");
        return;
    }

    if (msg.hasDayColorDefinitions()) {
        json_current_section = JsonSections::GREEN_DAYS_COLORS;
        auto json = msg.dayColorDefinitions().serialize(&jsonSer);
        file.write(R"(, "dayColorDefinitions": )");
        file.write(json.data(), json.size());
        file.write("\n");
        return;
    }

    if (msg.hasDays()) {
        if (json_current_section != JsonSections::GREEN_DAYS) {
            clear_array();
            file.write(R"(, "greenDays": [)");
            json_in_array = true;
            json_current_section = JsonSections::GREEN_DAYS;
        }

        WriteItems(msg.days().days(), file);
        return;
    }

    if (msg.hasNodes()) {
        if (json_current_section != JsonSections::NODES) {
            clear_array();
            file.write(R"(, "nodes": [)");
            json_in_array = true;
            json_current_section = JsonSections::NODES;
        }

        WriteItems(msg.nodes().nodes(), file);
        return;
    }

    if (msg.hasActionCategories()) {
        if (json_current_section != JsonSections::CATEGORIES) {
            clear_array();
            file.write(R"(, "categories": [)");
            json_in_array = true;
            json_current_section = JsonSections::CATEGORIES;
        }

        WriteItems(msg.actionCategories().categories(), file);
        return;
    }

    if (msg.hasActions()) {
        if (json_current_section != JsonSections::ACTIONS) {
            clear_array();
            file.write(R"(, "actions": [)");
            json_in_array = true;
            json_current_section = JsonSections::ACTIONS;
        }

        WriteItems(msg.actions().actions(), file);
        return;
    }

    if (msg.hasWorkSessions()) {
        if (json_current_section != JsonSections::WORK_SESSIONS) {
            clear_array();
            file.write(R"(, "workSessions": [)");
            json_in_array = true;
            json_current_section = JsonSections::WORK_SESSIONS;
        }

        WriteItems(msg.workSessions().sessions(), file);
        return;
    }

    if (msg.hasTimeBlocks()) {
        if (json_current_section != JsonSections::TIME_BLOCKS) {
            clear_array();
            file.write(R"(, "timeBlocks": [)");
            json_in_array = true;
            json_current_section = JsonSections::TIME_BLOCKS;
        }

        WriteItems(msg.timeBlocks().blocks(), file);
        return;
    }
}

} // anon ns

ImportExportModel::ImportExportModel() {
    QDir baseDir(QDir::homePath());
    const auto path = baseDir.filePath("NextApp/Backups");
    data_path_ = QUrl::fromLocalFile(path);

    // Make sure data_path_ exists
    const auto actual_path = data_path_.toLocalFile();
    if (!QDir{actual_path}.exists()) {
        QDir{}.mkpath(actual_path);
    }
}

void ImportExportModel::exportData(const QUrl &url)
{
    const auto path = url.toLocalFile();
    LOG_INFO_N << "Exporting data to " << path;

    doExport(path);
}

void ImportExportModel::setWorking(bool working)
{
    if (working_ != working) {
        working_ = working;
        emit workingChanged();
    }
}

QCoro::Task<void> ImportExportModel::doExport(QString fileName)
{
    LOG_DEBUG_N << "Exporting data to " << fileName;

    setWorking(true);
    ScopedExit workingGuard([this]() {
        setWorking(false);
    });

    try {
        if (fileName.endsWith(".json", Qt::CaseInsensitive)) {
            json_current_section = JsonSections::BEGIN;
            co_await ServerComm::instance().exportData(fileName, WriteJson);
        } else {
            co_await ServerComm::instance().exportData(fileName, WriteNextapp);
        }
    } catch (const std::exception &e) {
        LOG_ERROR_N << "Export failed: " << e.what();
        co_return;
    }

    LOG_INFO_N << "Export completed successfully.";
}

QCoro::Task<void> ImportExportModel::doImport(QString fileName)
{
    co_return;
}
