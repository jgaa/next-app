#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <optional>

namespace nextapp::mcp {

// Protobuf-derived MCP spelling; also used by input schemas and validation.
QJsonArray nodeKindNames();
QString nodeKindName(int value);
std::optional<int> nodeKindValue(const QString& name);

// Returns the existing MCP CallToolResult envelope, including actionable errors.
QJsonObject mcpHelp(const QJsonObject& arguments);

// Validates mutation fields before reservation/approval. Domain/cache validation
// remains authoritative in McpGateway and on the backend.
std::optional<QJsonObject> validateMutationArguments(const QString& tool_name,
                                                    const QJsonObject& arguments);
} // namespace nextapp::mcp
