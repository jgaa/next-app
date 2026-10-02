#include "McpHelp.h"
#include "McpProtocol.h"
#include "nextapp.qpb.h"

#include <QJsonDocument>
#include <QMetaEnum>
#include <QStringList>
#include <QUuid>
#include <cmath>
#include <limits>

namespace nextapp::mcp {
namespace {
QJsonObject reference(const QString& subject, const QString& name) {
    return {{QStringLiteral("subject"), subject}, {QStringLiteral("name"), name}};
}
QJsonObject result(const QJsonObject& document, bool error = false) {
    return {{QStringLiteral("structuredContent"), document}, {QStringLiteral("isError"), error},
        {QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact))}}}}};
}
QJsonObject toolDefinition(const QString& name) {
    for (const auto& entry : toolList().value(QStringLiteral("tools")).toArray()) {
        const auto definition = entry.toObject();
        if (definition.value(QStringLiteral("name")).toString() == name) return definition;
    }
    return {};
}
template<typename Enum>
QJsonArray enumValues() {
    const auto meta = QMetaEnum::fromType<Enum>();
    QJsonArray values;
    for (int i = 0; i < meta.keyCount(); ++i)
        values.append(QJsonObject{{QStringLiteral("name"), QString::fromLatin1(meta.key(i))},
                                  {QStringLiteral("value"), meta.value(i)}});
    return values;
}
QJsonArray names(const QStringList& list) {
    return QJsonArray::fromStringList(list);
}
const QStringList schemaNames{QStringLiteral("action"), QStringLiteral("node"), QStringLiteral("category")};
const QStringList conceptNames{QStringLiteral("mutation"), QStringLiteral("node_kind"),
    QStringLiteral("action_status"), QStringLiteral("action_priority"), QStringLiteral("timestamps"),
    QStringLiteral("node_relationships"), QStringLiteral("pagination")};
QJsonObject available() {
    QJsonArray tools;
    for (const auto& entry : toolList().value(QStringLiteral("tools")).toArray())
        tools.append(entry.toObject().value(QStringLiteral("name")));
    return {{QStringLiteral("tool"), tools}, {QStringLiteral("schema"), names(schemaNames)},
            {QStringLiteral("concept"), names(conceptNames)}};
}
QJsonObject field(const QString& type, const QString& summary) {
    return {{QStringLiteral("type"), type}, {QStringLiteral("summary"), summary}};
}
QJsonObject validationError(const QString& tool, const QString& key, const QJsonValue& value,
                            const QString& message, const QJsonObject& constraints = {}) {
    QJsonObject error{{QStringLiteral("error"), QStringLiteral("invalid_field_value")},
        {QStringLiteral("field"), key}, {QStringLiteral("message"), message},
        {QStringLiteral("constraints"), constraints},
        {QStringLiteral("help"), reference(QStringLiteral("tool"), tool)}};
    if (!value.isUndefined()) error.insert(QStringLiteral("value"), value);
    if (constraints.contains(QStringLiteral("enum"))) {
        error.insert(QStringLiteral("allowed"), constraints.value(QStringLiteral("enum")));
        if (key == QStringLiteral("kind"))
            error.insert(QStringLiteral("help"), reference(QStringLiteral("concept"), QStringLiteral("node_kind")));
    }
    return error;
}
} // namespace

QString nodeKindName(int value) {
    const auto meta = QMetaEnum::fromType<nextapp::pb::Node::Kind>();
    const auto key = meta.valueToKey(value);
    return key ? QString::fromLatin1(key).toLower() : QStringLiteral("unknown");
}
QJsonArray nodeKindNames() {
    const auto meta = QMetaEnum::fromType<nextapp::pb::Node::Kind>();
    QJsonArray values;
    for (int i = 0; i < meta.keyCount(); ++i) values.append(nodeKindName(meta.value(i)));
    return values;
}
std::optional<int> nodeKindValue(const QString& name) {
    const auto meta = QMetaEnum::fromType<nextapp::pb::Node::Kind>();
    for (int i = 0; i < meta.keyCount(); ++i)
        if (nodeKindName(meta.value(i)) == name) return meta.value(i);
    return std::nullopt;
}

QJsonObject mcpHelp(const QJsonObject& arguments) {
    const auto subject = arguments.value(QStringLiteral("subject")).toString();
    const auto name = arguments.value(QStringLiteral("name")).toString();
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        if (it.key() != QStringLiteral("subject") && it.key() != QStringLiteral("name"))
            return result(validationError(QStringLiteral("get_mcp_help"), it.key(), it.value(),
                QStringLiteral("Unknown help argument; use subject and name")), true);
    }
    if (!arguments.value(QStringLiteral("subject")).isString()
        || (subject != QStringLiteral("tool") && subject != QStringLiteral("schema") && subject != QStringLiteral("concept"))) {
        auto error = validationError(QStringLiteral("get_mcp_help"), QStringLiteral("subject"), arguments.value(QStringLiteral("subject")),
            QStringLiteral("Select tool, schema, or concept"),
            {{QStringLiteral("enum"), QJsonArray{QStringLiteral("tool"), QStringLiteral("schema"), QStringLiteral("concept")}}});
        error.insert(QStringLiteral("available"), available());
        return result(error, true);
    }
    if (!arguments.value(QStringLiteral("name")).isString() || name.isEmpty() || name.size() > 128)
        return result(validationError(QStringLiteral("get_mcp_help"), QStringLiteral("name"), arguments.value(QStringLiteral("name")),
            QStringLiteral("A nonempty exact subject name of at most 128 characters is required")), true);

    QJsonObject document{{QStringLiteral("subject"), subject}, {QStringLiteral("name"), name},
        {QStringLiteral("version"), QString::fromLatin1(NEXTAPP_VERSION)}};
    if (subject == QStringLiteral("tool")) {
        const auto definition = toolDefinition(name);
        if (!definition.isEmpty()) {
            document.insert(QStringLiteral("summary"), definition.value(QStringLiteral("description")));
            document.insert(QStringLiteral("inputSchema"), definition.value(QStringLiteral("inputSchema")));
            QJsonObject fields;
            const auto properties = definition.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("properties")).toObject();
            const QJsonObject semantics{
                {QStringLiteral("idempotencyKey"), QStringLiteral("Stable key for one intended mutation. Reuse with identical arguments on retry; changing the payload conflicts.")},
                {QStringLiteral("baseVersion"), QStringLiteral("Version from the latest get_action or list_nodes format=full result. A stale version fails without applying the patch; refresh before a new intended mutation.")},
                {QStringLiteral("id"), QStringLiteral("Existing action UUID returned by action reads.")},
                {QStringLiteral("requestId"), QStringLiteral("Request UUID returned by a mutation; status lookup is scoped to this agent.")},
                {QStringLiteral("nodeId"), QStringLiteral("Existing active node UUID. On create_action omit for Inbox. Never invent a destination UUID.")},
                {QStringLiteral("parentId"), QStringLiteral("Existing active parent node UUID; omit on create_node for a top-level node. Moving existing nodes is not supported.")},
                {QStringLiteral("categoryId"), QStringLiteral("Existing category UUID from category reads. On update_node, empty string clears the category; on create_node omit for no category.")},
                {QStringLiteral("name"), QStringLiteral("Display name. Mutation names must be nonempty; action names allow 255 UTF-16 code units and node names allow 128.")},
                {QStringLiteral("description"), QStringLiteral("Plain text description (protobuf descr). Empty string clears it; omitted update fields are preserved. Maximum 65536 UTF-8 bytes.")},
                {QStringLiteral("reason"), QStringLiteral("Optional explanation displayed during approval; maximum 2048 UTF-8 bytes.")},
                {QStringLiteral("done"), QStringLiteral("true marks done; false reopens. Completion uses backend rules, including recurring actions.")},
                {QStringLiteral("active"), QStringLiteral("false deactivates the node, removing it from active-node MCP listings.")},
                {QStringLiteral("kind"), QStringLiteral("Node classification; see concept/node_kind. Creation defaults to folder.")},
                {QStringLiteral("query"), QStringLiteral("Nonempty name substring; SQL wildcard characters are escaped as literals.")},
                {QStringLiteral("pageSize"), QStringLiteral("Requested result limit; configured server limit may reduce it (maximum 50).")},
                {QStringLiteral("cursor"), QStringLiteral("For node/category reads, pass returned nextCursor to fetch the next page. Action reads currently ignore cursor and return recent results.")},
                {QStringLiteral("format"), QStringLiteral("short returns identity, parent, name, kind and Inbox flag; full adds description, category, active, version and updatedAt.")},
                {QStringLiteral("subject"), QStringLiteral("Help namespace: tool, schema, or concept.")},
                {QStringLiteral("nameHelp"), QStringLiteral("Exact subject name; errors return available names.")}};
            for (auto it = properties.begin(); it != properties.end(); ++it)
                fields.insert(it.key(), semantics.value(name == QStringLiteral("get_mcp_help") && it.key() == QStringLiteral("name")
                    ? QStringLiteral("nameHelp") : it.key()));
            document.insert(QStringLiteral("fields"), fields);
            QJsonArray related;
            if (name.contains(QStringLiteral("action"))) related.append(reference(QStringLiteral("schema"), QStringLiteral("action")));
            if (name.contains(QStringLiteral("node"))) related.append(reference(QStringLiteral("schema"), QStringLiteral("node")));
            if (name.contains(QStringLiteral("categor"))) related.append(reference(QStringLiteral("schema"), QStringLiteral("category")));
            const auto mutation = properties.contains(QStringLiteral("idempotencyKey"));
            related.append(reference(QStringLiteral("concept"), mutation || name.contains(QStringLiteral("request_status"))
                ? QStringLiteral("mutation") : QStringLiteral("pagination")));
            document.insert(QStringLiteral("related"), related);
            document.insert(QStringLiteral("sideEffects"), mutation
                ? QStringLiteral("Writes synchronized domain data and local request/audit state. User configuration may disable the operation or require approval.")
                : QStringLiteral("Read only; no domain changes."));
            QJsonObject example;
            const auto required = definition.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("required")).toArray();
            for (const auto& key : required) {
                const auto k = key.toString();
                if (k == QStringLiteral("baseVersion")) example.insert(k, 1);
                else if (k == QStringLiteral("done")) example.insert(k, true);
                else if (k == QStringLiteral("idempotencyKey")) example.insert(k, QStringLiteral("agent-intent-001"));
                else if (k == QStringLiteral("subject")) example.insert(k, QStringLiteral("schema"));
                else if (k == QStringLiteral("name")) example.insert(k, name == QStringLiteral("get_mcp_help") ? QStringLiteral("action") : QStringLiteral("Plan the week"));
                else if (k == QStringLiteral("query")) example.insert(k, QStringLiteral("Plan"));
                else example.insert(k, QStringLiteral("11111111-1111-4111-8111-111111111111"));
            }
            if (name == QStringLiteral("nextapp_update_action") || name == QStringLiteral("nextapp_update_node")) example.insert(QStringLiteral("name"), QStringLiteral("Revised name"));
            document.insert(QStringLiteral("examples"), QJsonArray{QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("arguments"), example}}});
            document.insert(QStringLiteral("constraints"), QJsonArray{QStringLiteral("Example UUIDs are placeholders: replace them with IDs from reads. JSON null is not accepted for input fields."),
                QStringLiteral("Descriptions and reasons have UTF-8 byte limits in addition to schema character limits. The server validates independently.")});
            document.insert(QStringLiteral("commonErrors"), mutation
                ? QJsonArray{QStringLiteral("invalid_field_value: use field and constraints in the error to correct the request."),
                    QStringLiteral("Disabled operation or conflicting idempotency key: check operation settings; reuse identical arguments for retries."),
                    QStringLiteral("Missing or stale entity: fetch current state before making a new intended mutation.")}
                : QJsonArray{QStringLiteral("Use exact subject names and IDs returned by reads.")});
            return result(document);
        }
    } else if (subject == QStringLiteral("schema") && schemaNames.contains(name)) {
        // These are MCP projections, not the whole protobuf/domain model.
        QJsonObject fields;
        if (name == QStringLiteral("action")) {
            fields = {{QStringLiteral("id"), field(QStringLiteral("string"), QStringLiteral("Action UUID."))},
                {QStringLiteral("nodeId"), field(QStringLiteral("string"), QStringLiteral("Containing node UUID; protobuf node."))},
                {QStringLiteral("name"), field(QStringLiteral("string"), QStringLiteral("Task display name."))},
                {QStringLiteral("description"), field(QStringLiteral("string"), QStringLiteral("Plain text description; protobuf descr."))},
                {QStringLiteral("status"), field(QStringLiteral("integer"), QStringLiteral("Protobuf ActionStatus numeric value; see concept/action_status."))},
                {QStringLiteral("completedAt"), field(QStringLiteral("string"), QStringLiteral("Local-cache date/time string, empty when unset; see concept/timestamps."))},
                {QStringLiteral("version"), field(QStringLiteral("integer"), QStringLiteral("Server revision used as mutation baseVersion."))},
                {QStringLiteral("updatedAt"), field(QStringLiteral("integer"), QStringLiteral("Milliseconds since Unix epoch."))}};
            document.insert(QStringLiteral("constraints"), QJsonArray{QStringLiteral("MCP exposes this local-cache projection. Updates patch only name/description; completion uses done. Other Action protobuf fields cannot be edited through MCP.")});
            document.insert(QStringLiteral("related"), QJsonArray{reference(QStringLiteral("concept"), QStringLiteral("action_status")), reference(QStringLiteral("concept"), QStringLiteral("mutation")), reference(QStringLiteral("concept"), QStringLiteral("timestamps"))});
        } else if (name == QStringLiteral("node")) {
            fields = {{QStringLiteral("nodeId"), field(QStringLiteral("string"), QStringLiteral("Node UUID; protobuf uuid."))},
                {QStringLiteral("parentId"), field(QStringLiteral("string"), QStringLiteral("Parent UUID, empty for top-level; protobuf parent."))},
                {QStringLiteral("name"), field(QStringLiteral("string"), QStringLiteral("Display name."))},
                {QStringLiteral("kind"), field(QStringLiteral("string"), QStringLiteral("Lowercase protobuf Node.Kind name; see concept/node_kind."))},
                {QStringLiteral("inbox"), field(QStringLiteral("boolean"), QStringLiteral("Present as true only for the Inbox node."))},
                {QStringLiteral("description"), field(QStringLiteral("string"), QStringLiteral("Full format only; protobuf descr."))},
                {QStringLiteral("categoryId"), field(QStringLiteral("string"), QStringLiteral("Full format only; category UUID or empty string; protobuf category."))},
                {QStringLiteral("active"), field(QStringLiteral("boolean"), QStringLiteral("Full format only; listed nodes are active."))},
                {QStringLiteral("version"), field(QStringLiteral("integer"), QStringLiteral("Full format only; use as baseVersion for update_node."))},
                {QStringLiteral("updatedAt"), field(QStringLiteral("integer"), QStringLiteral("Full format only; milliseconds since Unix epoch."))}};
            document.insert(QStringLiteral("related"), QJsonArray{reference(QStringLiteral("concept"), QStringLiteral("node_kind")), reference(QStringLiteral("concept"), QStringLiteral("node_relationships")), reference(QStringLiteral("concept"), QStringLiteral("mutation"))});
            document.insert(QStringLiteral("constraints"), QJsonArray{QStringLiteral("Read listings contain active nodes only. Existing nodes cannot be moved or deleted through MCP; update_node supports name, description, kind, active, categoryId.")});
        } else {
            fields = {{QStringLiteral("categoryId"), field(QStringLiteral("string"), QStringLiteral("Category UUID; protobuf id."))},
                {QStringLiteral("name"), field(QStringLiteral("string"), QStringLiteral("Category display name."))},
                {QStringLiteral("description"), field(QStringLiteral("string"), QStringLiteral("Protobuf descr."))},
                {QStringLiteral("color"), field(QStringLiteral("string"), QStringLiteral("Display color."))},
                {QStringLiteral("icon"), field(QStringLiteral("string"), QStringLiteral("Display icon."))},
                {QStringLiteral("version"), field(QStringLiteral("integer"), QStringLiteral("Server category revision."))}};
            document.insert(QStringLiteral("constraints"), QJsonArray{QStringLiteral("Category reads do not expose category mutations.")});
            document.insert(QStringLiteral("related"), QJsonArray{reference(QStringLiteral("schema"), QStringLiteral("node"))});
        }
        document.insert(QStringLiteral("summary"), QStringLiteral("MCP JSON projection returned by %1 reads; fields differ from protobuf JSON.").arg(name));
        document.insert(QStringLiteral("fields"), fields);
        return result(document);
    } else if (subject == QStringLiteral("concept") && conceptNames.contains(name)) {
        if (name == QStringLiteral("node_kind")) {
            document.insert(QStringLiteral("summary"), QStringLiteral("Classification of lists/nodes; names are lowercase protobuf Node.Kind names. Default on creation is folder."));
            document.insert(QStringLiteral("values"), enumValues<nextapp::pb::Node::Kind>());
            document.insert(QStringLiteral("allowed"), nodeKindNames());
        } else if (name == QStringLiteral("action_status")) {
            document.insert(QStringLiteral("summary"), QStringLiteral("Action read results use numeric protobuf ActionStatus values: ACTIVE is available to work on, DONE is completed, ONHOLD is paused, and DELETED is removed. complete_action accepts done=true/false; update_action does not accept status."));
            document.insert(QStringLiteral("values"), enumValues<nextapp::pb::ActionStatusGadget::ActionStatus>());
        } else if (name == QStringLiteral("action_priority")) {
            document.insert(QStringLiteral("summary"), QStringLiteral("Domain ActionPriority enum; preserved protobuf spellings. Priority is not currently exposed in MCP reads or mutations."));
            document.insert(QStringLiteral("values"), enumValues<nextapp::pb::ActionPriorityGadget::ActionPriority>());
        } else if (name == QStringLiteral("mutation")) {
            document.insert(QStringLiteral("summary"), QStringLiteral("Mutations are gated by user settings and tracked locally using stable idempotency keys and request IDs."));
            document.insert(QStringLiteral("constraints"), QJsonArray{QStringLiteral("Reuse the same idempotencyKey and identical arguments for retries. A different payload with the same key conflicts."),
                QStringLiteral("When state is PENDING, poll nextapp_get_request_status with requestId; never submit with a new key while awaiting approval."),
                QStringLiteral("baseVersion must be the current revision, a nonnegative integer no larger than 2147483647. Updates preserve omitted fields."),
                QStringLiteral("OUTCOME_UNKNOWN or manualReconciliationRequired means the backend may have applied the change. Inspect domain state before deciding what to do; never blindly repeat it with a new key."),
                QStringLiteral("Approval, sync/offline cancellation, and backend validation still apply. Help availability does not imply a mutation is enabled.")});
            document.insert(QStringLiteral("related"), QJsonArray{reference(QStringLiteral("tool"), QStringLiteral("nextapp_get_request_status"))});
        } else if (name == QStringLiteral("timestamps")) {
            document.insert(QStringLiteral("summary"), QStringLiteral("MCP updatedAt values are Unix milliseconds. Action completedAt is a local-cache date/time string (empty when unset); its protobuf source completedTime uses Unix seconds. Do not treat completedAt as an epoch or assume a timezone not present in its text."));
        } else if (name == QStringLiteral("node_relationships")) {
            document.insert(QStringLiteral("summary"), QStringLiteral("Nodes form parent/child lists, folders or projects. Obtain existing active IDs from node reads. Omit parentId for top-level creation; omit nodeId on create_action to resolve the real Inbox. Never invent IDs. Inbox cannot be updated through MCP."));
            document.insert(QStringLiteral("related"), QJsonArray{reference(QStringLiteral("schema"), QStringLiteral("node"))});
        } else {
            document.insert(QStringLiteral("summary"), QStringLiteral("Node/category lists and searches return nextCursor when more results exist; pass it with the same query/format. Ordered by UUID. Actions return recent results ordered by updated descending then ID, without continuation; list_actions cursor is currently ignored. pageSize is clamped to the configured limit, at most 50. Reads use the local cache, not a remote search."));
        }
        return result(document);
    }
    return result({{QStringLiteral("error"), QStringLiteral("unknown_help_name")},
        {QStringLiteral("subject"), subject}, {QStringLiteral("name"), name},
        {QStringLiteral("message"), QStringLiteral("Use an exact name from available for this subject")},
        {QStringLiteral("available"), available()}}, true);
}

std::optional<QJsonObject> validateMutationArguments(const QString& tool_name, const QJsonObject& arguments) {
    const auto input = toolDefinition(tool_name).value(QStringLiteral("inputSchema")).toObject();
    const auto properties = input.value(QStringLiteral("properties")).toObject();
    if (!properties.contains(QStringLiteral("idempotencyKey"))) return std::nullopt;
    for (const auto& required : input.value(QStringLiteral("required")).toArray()) {
        const auto key = required.toString();
        if (!arguments.contains(key))
            return validationError(tool_name, key, {}, QStringLiteral("Required field is missing"), properties.value(key).toObject());
    }
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        const auto constraint = properties.value(it.key()).toObject();
        // Older action tools ignored unknown properties; preserve that behavior.
        // Node tools have always rejected them.
        if (constraint.isEmpty()) {
            if (tool_name.endsWith(QStringLiteral("_node")))
                return validationError(tool_name, it.key(), it.value(), QStringLiteral("Unknown field"));
            continue;
        }
        const auto type = constraint.value(QStringLiteral("type")).toString();
        const auto value = it.value();
        bool valid = true;
        if (type == QStringLiteral("string")) {
            valid = value.isString();
            const auto text = value.toString();
            if (constraint.contains(QStringLiteral("minLength"))) valid &= text.size() >= constraint.value(QStringLiteral("minLength")).toInt();
            if (constraint.contains(QStringLiteral("maxLength"))) valid &= text.size() <= constraint.value(QStringLiteral("maxLength")).toInt();
            if (constraint.contains(QStringLiteral("enum"))) valid &= constraint.value(QStringLiteral("enum")).toArray().contains(value);
            const auto clear_category = tool_name == QStringLiteral("nextapp_update_node") && it.key() == QStringLiteral("categoryId") && text.isEmpty();
            if (constraint.value(QStringLiteral("format")) == QStringLiteral("uuid") || it.key() == QStringLiteral("categoryId"))
                valid &= clear_category || !QUuid{text}.isNull();
            const auto byte_limit = it.key() == QStringLiteral("description") ? 64 * 1024 : it.key() == QStringLiteral("reason") ? 2048 : -1;
            if (byte_limit >= 0 && text.toUtf8().size() > byte_limit) {
                auto limits = constraint;
                limits.insert(QStringLiteral("maxUtf8Bytes"), byte_limit);
                return validationError(tool_name, it.key(), value, QStringLiteral("Text exceeds the UTF-8 byte limit"), limits);
            }
        } else if (type == QStringLiteral("integer")) {
            const auto number = value.toDouble(-1);
            valid = value.isDouble() && std::isfinite(number) && std::floor(number) == number
                && number >= constraint.value(QStringLiteral("minimum")).toDouble()
                && number <= constraint.value(QStringLiteral("maximum")).toDouble(std::numeric_limits<int>::max());
        } else if (type == QStringLiteral("boolean")) valid = value.isBool();
        if (!valid) return validationError(tool_name, it.key(), value, QStringLiteral("Field does not satisfy its type or constraints"), constraint);
    }
    const auto alternatives = input.value(QStringLiteral("anyOf")).toArray();
    if (!alternatives.isEmpty()) {
        QJsonArray allowed;
        for (const auto& alternative : alternatives) {
            const auto key = alternative.toObject().value(QStringLiteral("required")).toArray().first().toString();
            if (arguments.contains(key)) return std::nullopt;
            allowed.append(key);
        }
        return QJsonObject{{QStringLiteral("error"), QStringLiteral("empty_patch")},
            {QStringLiteral("message"), QStringLiteral("Supply at least one patch field")},
            {QStringLiteral("allowed"), allowed}, {QStringLiteral("help"), reference(QStringLiteral("tool"), tool_name)}};
    }
    return std::nullopt;
}
} // namespace nextapp::mcp
