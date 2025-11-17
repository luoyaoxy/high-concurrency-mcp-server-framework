/**
 * @file types.cpp
 * @brief MCP 协议核心类型实现
 */

#include "types.h"

namespace mcp {

json ToolInputSchema::to_json() const {
    json j = {
        {"type", type},
        {"properties", properties}
    };

    if (!required.empty()) {
        j["required"] = required;
    }

    return j;
}

ToolInputSchema ToolInputSchema::from_json(const json& j) {
    ToolInputSchema schema;
    schema.type = j.value("type", "object");
    schema.properties = j.value("properties", json::object());

    if (j.contains("required")) {
        schema.required = j["required"].get<std::vector<std::string>>();
    }

    return schema;
}

json Tool::to_json() const {
    return {
        {"name", name},
        {"description", description},
        {"inputSchema", input_schema.to_json()}
    };
}

Tool Tool::from_json(const json& j) {
    Tool tool;
    tool.name = j.at("name").get<std::string>();
    tool.description = j.value("description", "");
    tool.input_schema = ToolInputSchema::from_json(j.at("inputSchema"));
    return tool;
}

json ContentItem::to_json() const {
    json j = {{"type", type}};

    if (text.has_value()) {
        j["text"] = *text;
    }
    if (data.has_value()) {
        j["data"] = *data;
    }
    if (mime_type.has_value()) {
        j["mimeType"] = *mime_type;
    }
    if (uri.has_value()) {
        j["uri"] = *uri;
    }

    return j;
}

ContentItem ContentItem::from_json(const json& j) {
    ContentItem item;
    item.type = j.at("type").get<std::string>();

    if (j.contains("text")) {
        item.text = j["text"].get<std::string>();
    }
    if (j.contains("data")) {
        item.data = j["data"].get<std::string>();
    }
    if (j.contains("mimeType")) {
        item.mime_type = j["mimeType"].get<std::string>();
    }
    if (j.contains("uri")) {
        item.uri = j["uri"].get<std::string>();
    }

    return item;
}

json ToolResult::to_json() const {
    json content_arr = json::array();
    for (const auto& item : content) {
        content_arr.push_back(item.to_json());
    }

    json j = {{"content", content_arr}};

    if (is_error) {
        j["isError"] = is_error;
    }

    return j;
}

ToolResult ToolResult::from_json(const json& j) {
    ToolResult result;

    if (j.contains("content")) {
        for (const auto& item_json : j["content"]) {
            result.content.push_back(ContentItem::from_json(item_json));
        }
    }

    result.is_error = j.value("isError", false);

    return result;
}

json Resource::to_json() const {
    json j = {
        {"uri", uri},
        {"name", name}
    };

    if (description.has_value()) {
        j["description"] = *description;
    }
    if (mime_type.has_value()) {
        j["mimeType"] = *mime_type;
    }

    return j;
}

Resource Resource::from_json(const json& j) {
    Resource res;
    res.uri = j.at("uri").get<std::string>();
    res.name = j.at("name").get<std::string>();

    if (j.contains("description")) {
        res.description = j["description"].get<std::string>();
    }
    if (j.contains("mimeType")) {
        res.mime_type = j["mimeType"].get<std::string>();
    }

    return res;
}

json ResourceContent::to_json() const {
    json j = {
        {"uri", uri},
        {"text", text}
    };

    if (mime_type.has_value()) {
        j["mimeType"] = *mime_type;
    }
    if (blob.has_value()) {
        j["blob"] = *blob;
    }

    return j;
}

ResourceContent ResourceContent::from_json(const json& j) {
    ResourceContent content;
    content.uri = j.at("uri").get<std::string>();
    content.text = j.value("text", "");

    if (j.contains("mimeType")) {
        content.mime_type = j["mimeType"].get<std::string>();
    }
    if (j.contains("blob")) {
        content.blob = j["blob"].get<std::string>();
    }

    return content;
}

json PromptArgument::to_json() const {
    json j = {
        {"name", name},
        {"required", required}
    };

    if (description.has_value()) {
        j["description"] = *description;
    }

    return j;
}

PromptArgument PromptArgument::from_json(const json& j) {
    PromptArgument arg;
    arg.name = j.at("name").get<std::string>();
    arg.required = j.value("required", false);

    if (j.contains("description")) {
        arg.description = j["description"].get<std::string>();
    }

    return arg;
}

json Prompt::to_json() const {
    json j = {{"name", name}};

    if (description.has_value()) {
        j["description"] = *description;
    }

    if (!arguments.empty()) {
        json args_arr = json::array();
        for (const auto& arg : arguments) {
            args_arr.push_back(arg.to_json());
        }
        j["arguments"] = args_arr;
    }

    return j;
}

Prompt Prompt::from_json(const json& j) {
    Prompt prompt;
    prompt.name = j.at("name").get<std::string>();

    if (j.contains("description")) {
        prompt.description = j["description"].get<std::string>();
    }

    if (j.contains("arguments")) {
        for (const auto& arg_json : j["arguments"]) {
            prompt.arguments.push_back(PromptArgument::from_json(arg_json));
        }
    }

    return prompt;
}

json PromptMessage::to_json() const {
    return {
        {"role", role == Role::User ? "user" : "assistant"},
        {"content", content}
    };
}

PromptMessage PromptMessage::from_json(const json& j) {
    PromptMessage msg;

    std::string role_str = j.at("role").get<std::string>();
    msg.role = (role_str == "user") ? Role::User : Role::Assistant;
    msg.content = j.at("content");

    return msg;
}

json ServerCapabilities::ToolsCapability::to_json() const {
    return {{"listChanged", list_changed}};
}

ServerCapabilities::ToolsCapability ServerCapabilities::ToolsCapability::from_json(const json& j) {
    ToolsCapability cap;
    cap.list_changed = j.value("listChanged", false);
    return cap;
}

json ServerCapabilities::ResourcesCapability::to_json() const {
    return {
        {"subscribe", subscribe},
        {"listChanged", list_changed}
    };
}

ServerCapabilities::ResourcesCapability ServerCapabilities::ResourcesCapability::from_json(const json& j) {
    ResourcesCapability cap;
    cap.subscribe = j.value("subscribe", false);
    cap.list_changed = j.value("listChanged", false);
    return cap;
}

json ServerCapabilities::PromptsCapability::to_json() const {
    return {{"listChanged", list_changed}};
}

ServerCapabilities::PromptsCapability ServerCapabilities::PromptsCapability::from_json(const json& j) {
    PromptsCapability cap;
    cap.list_changed = j.value("listChanged", false);
    return cap;
}

json ServerCapabilities::to_json() const {
    json j = json::object();

    if (tools.has_value()) {
        j["tools"] = tools->to_json();
    }
    if (resources.has_value()) {
        j["resources"] = resources->to_json();
    }
    if (prompts.has_value()) {
        j["prompts"] = prompts->to_json();
    }
    if (logging.has_value()) {
        j["logging"] = *logging;
    }

    return j;
}

ServerCapabilities ServerCapabilities::from_json(const json& j) {
    ServerCapabilities cap;

    if (j.contains("tools")) {
        cap.tools = ToolsCapability::from_json(j["tools"]);
    }
    if (j.contains("resources")) {
        cap.resources = ResourcesCapability::from_json(j["resources"]);
    }
    if (j.contains("prompts")) {
        cap.prompts = PromptsCapability::from_json(j["prompts"]);
    }
    if (j.contains("logging")) {
        cap.logging = j["logging"];
    }

    return cap;
}

json ServerInfo::to_json() const {
    return {
        {"name", name},
        {"version", version}
    };
}

ServerInfo ServerInfo::from_json(const json& j) {
    ServerInfo info;
    info.name = j.at("name").get<std::string>();
    info.version = j.at("version").get<std::string>();
    return info;
}

json InitializeResult::to_json() const {
    return {
        {"protocolVersion", protocol_version},
        {"capabilities", capabilities.to_json()},
        {"serverInfo", server_info.to_json()}
    };
}

InitializeResult InitializeResult::from_json(const json& j) {
    InitializeResult result;
    result.protocol_version = j.at("protocolVersion").get<std::string>();
    result.capabilities = ServerCapabilities::from_json(j.at("capabilities"));
    result.server_info = ServerInfo::from_json(j.at("serverInfo"));
    return result;
}

} // namespace mcp
