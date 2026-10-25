# This project 

This is NextApp, a project with multiple binaries using CMake and C++20.

The UI app in `src/NextAppUi/` uses Qt 6.10 or later with QML and QtGrpc, and targets Linux, macOS, Windows, and Android.

The backends, `nextappd` and `signupd`, are built only for Linux and do not use Qt.

All sub-projects are built using the root `./CMakeLists.txt`.

# Rules and guidelines

Cache the files locally (not checked into git) and either refresh or check the timestamp on the HTTP server when you refresh your Agents instructions, or checkout the agents repository https://github.com/jgaa/agents.git outside the project and keep it up to date before you re-read the files.

https://raw.githubusercontent.com/jgaa/agents/refs/heads/main/rules/tlv-agents.md
https://raw.githubusercontent.com/jgaa/agents/refs/heads/main/rules/tlv-agents-cpp.md


# Rules for this repository:

* Keep changes for newer versions of Boost, gRPC, Protobuf, and other dependencies backwards compatible with older supported versions.
* Default to desktop builds and desktop test binaries for local development and verification.

## Code
* Use C++20.

## Release GA workflow
* The release workflow must never rebuild Flatpak, macOS, or other package artifacts. Package builds are expensive, slow, and can fail for reasons unrelated to the release logic.
* The release workflow is expected to reuse already-built successful artifacts by versioned artifact filename only. Do not bind release artifact selection to a specific commit or require a full rebuild because one package had to be rebuilt later for the same version.
* The only required sanity check for release artifact selection is that the artifact filenames match the version being tagged and released.
* The release workflow may create and push the release tag, but only after it has verified that the required version-matched artifacts already exist.

General engineering expectations are covered by the imported rules above. The following release requirements are specific to this project.

## MCP maintenance

* MCP tool definitions, input schemas, help metadata, examples, and constraints must be reviewed and updated whenever relevant protobuf definitions, domain models, validation rules, enums, field semantics, or other constraints change. Changing an existing protobuf/API constraint requires checking all affected MCP metadata.
* MCP documentation drifting away from the running implementation is a bug. Reuse generated protobuf metadata and shared validation/schema definitions where practical; keep detailed documentation in local MCP help rather than duplicating it in tool descriptions.
* Normal feature work must include an MCP-impact review. Every new user-visible/domain feature must be evaluated to determine whether it should also be exposed through MCP.
* Evaluation of MCP exposure is mandatory, but implementation is not automatic. Adding a new MCP tool or substantially extending the MCP API requires explicit project-owner approval first. Maintenance fixes that keep already-approved MCP tools, schemas, help, and validation synchronized with the implementation do not require new approval.
