#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaEnum>
#include <limits>

#include "mcp/McpHelp.h"
#include "mcp/McpProtocol.h"
#include "nextapp.qpb.h"

using namespace nextapp::mcp;
namespace {
QJsonObject definition(const QString& name) {
    for (const auto& entry : toolList().value("tools").toArray())
        if (entry.toObject().value("name") == name) return entry.toObject();
    return {};
}
QJsonObject help(const QString& subject, const QString& name) {
    return mcpHelp({{"subject", subject}, {"name", name}}).value("structuredContent").toObject();
}
QJsonObject actionPatch() {
    return {{"idempotencyKey", "intent-001"}, {"id", "11111111-1111-4111-8111-111111111111"},
            {"baseVersion", 1}, {"name", "Revised name"}};
}
} // namespace

class tst_NextAppUiMcp final : public QObject {
    Q_OBJECT
private slots:
    void discoveryAndInvocation() {
        const auto tool = definition("get_mcp_help");
        QVERIFY(!tool.isEmpty());
        QCOMPARE(tool.value("inputSchema").toObject().value("required").toArray(), QJsonArray({"subject", "name"}));
        // Invoke the help handler used by McpGateway from a parsed tools/call
        // in each supported protocol revision, without needing a live backend.
        for (const auto modern : {false, true}) {
            QJsonObject params{{"name", "get_mcp_help"}, {"arguments", QJsonObject{{"subject", "tool"}, {"name", "nextapp_update_action"}}}};
            HeaderMap headers;
            if (modern) {
                params.insert("_meta", QJsonObject{{QString::fromLatin1(protocol_version_key), QString::fromLatin1(protocol_version)},
                    {QString::fromLatin1(client_capabilities_key), QJsonObject{}}});
                headers = {{"mcp-protocol-version", protocol_version}, {"mcp-method", "tools/call"}, {"mcp-name", "get_mcp_help"}};
            }
            const auto parsed = parseRequest(QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1},
                {"method", "tools/call"}, {"params", params}}).toJson(), headers);
            QVERIFY(std::holds_alternative<Request>(parsed));
            const auto request = std::get<Request>(parsed);
            QCOMPARE(request.tool_name, QStringLiteral("get_mcp_help"));
            const auto reply = mcpHelp(request.params.value("arguments").toObject());
            QVERIFY(!reply.value("isError").toBool());
            const auto data = reply.value("structuredContent").toObject();
            QCOMPARE(data.value("name").toString(), QStringLiteral("nextapp_update_action"));
            QCOMPARE(data.value("inputSchema"), definition("nextapp_update_action").value("inputSchema"));
            QCOMPARE(QJsonDocument::fromJson(reply.value("content").toArray().first().toObject().value("text").toString().toUtf8()).object(), data);
            QVERIFY(!data.value("examples").toArray().isEmpty());
            QVERIFY(!data.value("related").toArray().isEmpty());
        }
    }
    void sharedSchemasAndConcepts() {
        const auto action = help("schema", "action");
        const auto fields = action.value("fields").toObject();
        for (const auto* key : {"id", "nodeId", "name", "description", "status", "completedAt", "version", "updatedAt"})
            QVERIFY(fields.contains(key));
        QVERIFY(help("schema", "node").value("fields").toObject().contains("inbox"));
        QVERIFY(help("schema", "category").value("fields").toObject().contains("version"));
        QVERIFY(help("concept", "mutation").value("constraints").toArray().size() >= 4);
        const auto meta = QMetaEnum::fromType<nextapp::pb::ActionStatusGadget::ActionStatus>();
        const auto statuses = help("concept", "action_status").value("values").toArray();
        QCOMPARE(statuses.size(), meta.keyCount());
        for (int i = 0; i < meta.keyCount(); ++i) {
            QCOMPARE(statuses.at(i).toObject().value("name").toString(), QString::fromLatin1(meta.key(i)));
            QCOMPARE(statuses.at(i).toObject().value("value").toInt(), meta.value(i));
        }
        const auto allowed = help("concept", "node_kind").value("allowed").toArray();
        QCOMPARE(allowed, nodeKindNames());
        for (const auto* name : {"nextapp_create_node", "nextapp_update_node"})
            QCOMPARE(definition(name).value("inputSchema").toObject().value("properties").toObject().value("kind").toObject().value("enum").toArray(), allowed);
        for (const auto& name : allowed) {
            const auto value = nodeKindValue(name.toString());
            QVERIFY(value.has_value());
            QCOMPARE(nodeKindName(*value), name.toString());
        }
        QVERIFY(!nodeKindValue("PROJECT"));
    }
    void invalidHelpArguments() {
        for (const auto& args : {QJsonObject{}, QJsonObject{{"subject", "other"}, {"name", "action"}},
                QJsonObject{{"subject", 1}, {"name", "action"}}, QJsonObject{{"subject", "schema"}, {"name", QJsonValue::Null}},
                QJsonObject{{"subject", "schema"}, {"name", ""}}, QJsonObject{{"subject", "schema"}, {"name", "action"}, {"extra", true}}}) {
            const auto reply = mcpHelp(args);
            QVERIFY(reply.value("isError").toBool());
            QVERIFY(reply.value("structuredContent").toObject().contains("field"));
        }
        for (const auto* subject : {"tool", "schema", "concept"}) {
            const auto reply = mcpHelp({{"subject", subject}, {"name", "missing"}});
            QVERIFY(reply.value("isError").toBool());
            const auto error = reply.value("structuredContent").toObject();
            QCOMPARE(error.value("error").toString(), QStringLiteral("unknown_help_name"));
            QVERIFY(!error.value("available").toObject().value(subject).toArray().isEmpty());
        }
    }
    void registryHelpAndExamplesStayConsistent() {
        const auto all = mcpHelp({{"subject", "schema"}, {"name", "missing"}}).value("structuredContent").toObject().value("available").toObject();
        for (auto subjects = all.begin(); subjects != all.end(); ++subjects) {
            for (const auto& name : subjects.value().toArray()) {
                const auto reply = mcpHelp({{"subject", subjects.key()}, {"name", name}});
                QVERIFY2(!reply.value("isError").toBool(), qPrintable(name.toString()));
                const auto document = reply.value("structuredContent").toObject();
                QVERIFY(!document.value("summary").toString().isEmpty());
                QCOMPARE(document.value("version").toString(), QString::fromLatin1(NEXTAPP_VERSION));
                for (const auto& related : document.value("related").toArray())
                    QVERIFY(!mcpHelp(related.toObject()).value("isError").toBool());
                if (subjects.key() == "tool") {
                    const auto input = definition(name.toString()).value("inputSchema").toObject();
                    const auto properties = input.value("properties").toObject();
                    const auto fields = document.value("fields").toObject();
                    for (auto field = properties.begin(); field != properties.end(); ++field)
                        QVERIFY2(!fields.value(field.key()).toString().isEmpty(), qPrintable(name.toString() + "/" + field.key()));
                    for (const auto& example : document.value("examples").toArray())
                        QVERIFY(!validateMutationArguments(name.toString(), example.toObject().value("arguments").toObject()));
                }
            }
        }
    }
    void mutationValidation() {
        QVERIFY(!validateMutationArguments("nextapp_update_action", actionPatch()));
        for (const auto& version : QJsonArray{-1, 1.5, double(std::numeric_limits<int>::max()) + 1, "1", QJsonValue::Null}) {
            auto patch = actionPatch(); patch.insert("baseVersion", version);
            const auto error = validateMutationArguments("nextapp_update_action", patch);
            QVERIFY(error); QCOMPARE(error->value("field").toString(), QStringLiteral("baseVersion"));
        }
        for (const auto& key : QJsonArray{"", QJsonValue::Null, 12}) {
            auto patch = actionPatch(); patch.insert("idempotencyKey", key);
            const auto error = validateMutationArguments("nextapp_update_action", patch);
            QVERIFY(error); QCOMPARE(error->value("field").toString(), QStringLiteral("idempotencyKey"));
        }
        auto patch = actionPatch(); patch.remove("name");
        const auto empty = validateMutationArguments("nextapp_update_action", patch);
        QVERIFY(empty); QCOMPARE(empty->value("error").toString(), QStringLiteral("empty_patch"));
        patch.insert("description", "");
        QVERIFY(!validateMutationArguments("nextapp_update_action", patch));
        patch.insert("description", QString(22000, QChar(0x20ac))); // Three UTF-8 bytes per character.
        const auto too_large = validateMutationArguments("nextapp_update_action", patch);
        QVERIFY(too_large); QCOMPARE(too_large->value("constraints").toObject().value("maxUtf8Bytes").toInt(), 65536);
        QJsonObject node{{"idempotencyKey", "node-001"}, {"name", "A project"}, {"kind", "invalid"}};
        const auto kind = validateMutationArguments("nextapp_create_node", node);
        QVERIFY(kind); QCOMPARE(kind->value("allowed").toArray(), nodeKindNames());
        QCOMPARE(kind->value("help").toObject().value("name").toString(), QStringLiteral("node_kind"));
        node.remove("kind"); QVERIFY(!validateMutationArguments("nextapp_create_node", node));
        node.insert("name", ""); QVERIFY(validateMutationArguments("nextapp_create_node", node));
        QJsonObject update{{"idempotencyKey", "node-002"}, {"nodeId", "11111111-1111-4111-8111-111111111111"}, {"baseVersion", 1}, {"categoryId", ""}};
        QVERIFY(!validateMutationArguments("nextapp_update_node", update));
        update.insert("categoryId", "invalid"); QVERIFY(validateMutationArguments("nextapp_update_node", update));
        update.insert("categoryId", QJsonValue::Null); QVERIFY(validateMutationArguments("nextapp_update_node", update));
    }
    void existingInterfacePreserved() {
        const QStringList existing{"nextapp_get_action", "nextapp_get_request_status", "nextapp_list_actions", "nextapp_list_nodes",
            "nextapp_search_nodes", "nextapp_list_categories", "nextapp_search_categories", "nextapp_search_actions",
            "nextapp_create_action", "nextapp_create_node", "nextapp_update_node", "nextapp_update_action", "nextapp_complete_action"};
        QCOMPARE(toolList().value("tools").toArray().size(), existing.size() + 1);
        for (const auto& name : existing) QVERIFY(!definition(name).isEmpty());
        // Compatibility: unknown action fields were ignored, node fields rejected.
        auto patch = actionPatch(); patch.insert("extra", true);
        QVERIFY(!validateMutationArguments("nextapp_update_action", patch));
        QVERIFY(validateMutationArguments("nextapp_create_node", {{"idempotencyKey", "n"}, {"name", "Node"}, {"extra", true}}));
        QVERIFY(!validateMutationArguments("nextapp_list_actions", {{"pageSize", 500}})); // Reads still clamp in the gateway.
        const auto parsed = parseRequest(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"nextapp_update_action","arguments":{}}})", {});
        QVERIFY(std::holds_alternative<Request>(parsed));
        QCOMPARE(std::get<Request>(parsed).tool_name, QStringLiteral("nextapp_update_action"));
        QVERIFY(std::holds_alternative<ProtocolError>(parseRequest(R"({"jsonrpc":"2.0","id":2,"method":"unsupported"})", {})));
        const auto init = initializeResult(QString::fromLatin1(legacy_protocol_version));
        QCOMPARE(init.value("protocolVersion").toString(), QString::fromLatin1(legacy_protocol_version));
    }
};
QTEST_GUILESS_MAIN(tst_NextAppUiMcp)
#include "tst_nextappui_mcp.moc"
