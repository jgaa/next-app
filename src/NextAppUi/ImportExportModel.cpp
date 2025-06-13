#include <QDir>
#include <QProtobufJsonSerializer>

#include "ImportExportModel.h"
#include "ServerComm.h"

#include "logging.h"
#include "util.h"

namespace {


// Static variables are OK here, as exports are not called concurrently.
bool first_record = true;

void WriteNextapp(const nextapp::pb::Status& msg, QFile& file) {

    if (first_record) {
        ImportExportModel::FileHeader hdr;
        const auto *ptr = reinterpret_cast<const char*>(&hdr);
        const size_t len = sizeof(ImportExportModel::FileHeader);
        assert(len == 8);
        file.write(ptr, len);
    }

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

void ImportExportModel::importData(const QUrl &url)
{
    const auto path = url.toLocalFile();
    LOG_INFO_N << "Importing data from " << path;

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
    enum State {
        INIT,
        SENDING_DATA,
        DONE,
        ERROR
    };

    auto state = INIT;

    LOG_DEBUG_N << "Importing data from " << fileName;

    setWorking(true);
    ScopedExit workingGuard([this]() {
        setWorking(false);
    });

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR_N << "Failed to open file for import: " << file.errorString();
        co_return;
    }

    if (file.size() < sizeof(ImportExportModel::FileHeader)) {
        LOG_ERROR_N << "Not a valid .nextapp import file: " << fileName;
        co_return;
    }

    ImportExportModel::FileHeader hdr;
    if (file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
        LOG_ERROR_N << "Failed to read file header from " << fileName;
        co_return;
    }
    if (std::string_view(hdr.magic, sizeof(hdr.magic)) != "NextApp") {
        LOG_ERROR_N << "Not a valid .nextapp import file: " << fileName;
        co_return;
    }
    if (hdr.version != ImportExportModel::current_file_version) {
        LOG_ERROR_N << "Unsupported file version: " << static_cast<int>(hdr.version);
        co_return;
    }

    // This is called repeatedly until we return false when all the messages are read from our file.
    auto read = [&] (nextapp::pb::Status& msg) -> bool {
        if (state == DONE) {
            return false; // No more data to read
        }

        if (state < SENDING_DATA) {
            LOG_DEBUG_N << "Sending data to server.";
            state = SENDING_DATA;
        } else if (state > SENDING_DATA) {
            LOG_TRACE_N << "Unexpected state: " << static_cast<int>(state);
            return false; // stop reading
        }

        // Read message-length
        quint32 binary_len{};
        if (file.read(reinterpret_cast<char*>(&binary_len), sizeof(binary_len)) != sizeof(binary_len)) {
            LOG_ERROR_N << "Failed to read message length from file.";
            state = ERROR;
            return false;
        }
        const auto len = qFromBigEndian<quint32>(binary_len);
        constexpr decltype(len) ten_megabytes = 1024u * 1024u * 10u;
        if (len > ten_megabytes) {
            LOG_ERROR_N << "Message-length is too large: " << len;
            state = ERROR;
            return false;
        }

        QByteArray data;
        if (len > 0) {
            data.resize(len);
            if (file.read(data.data(), len) != len) {
                LOG_ERROR_N << "Failed to read message data from file.";
                state = ERROR;
                return false;
            }

            QProtobufSerializer serializer;
            if (!msg.deserialize(&serializer, data)) {
                LOG_ERROR_N << "Failed to parse protobuf message.";
                state = ERROR;
                return false;
            }

            const bool last = msg.hasHasMore() && !msg.hasMore();
            if (last || file.atEnd()) {
                if (!last && file.atEnd()) {
                    LOG_ERROR_N << "Invalid end of data-file. Last=" << last
                                << ", eof=" << file.atEnd();
                    state = ERROR;
                    return false;
                }

                state = DONE;
                LOG_DEBUG_N << "End of file reached.";
            }

            return true;
        }

        LOG_ERROR_N << "Message-length cannot be zero.";
        state = ERROR;
        return false;
    };

    try {
        co_await ServerComm::instance().importData(read);
    } catch (const std::exception &e) {
        LOG_ERROR_N << "Import failed: " << e.what();
        state = ERROR;
    }

    co_return;
}
