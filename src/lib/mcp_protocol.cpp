#include "mcp/mcp_protocol.h"

namespace mcp {

json Tool::ToJson() const {
    json j;
    j["name"] = name;
    j["description"] = description;
    
    json params_schema;
    params_schema["type"] = "object";
    params_schema["properties"] = json::object();
    json required_params = json::array();
    
    for (const auto& [param_name, param] : parameters) {
        json param_json;
        param_json["type"] = param.type;
        param_json["description"] = param.description;
        
        if (param.default_value.has_value()) {
            param_json["default"] = param.default_value.value();
        }
        
        if (param.enum_values.has_value()) {
            param_json["enum"] = param.enum_values.value();
        }
        
        params_schema["properties"][param_name] = param_json;
        
        if (param.required) {
            required_params.push_back(param_name);
        }
    }
    
    if (!required_params.empty()) {
        params_schema["required"] = required_params;
    }
    
    j["inputSchema"] = params_schema;
    return j;
}

Tool Tool::FromJson(const json& j) {
    Tool tool;
    tool.name = j.at("name").get<std::string>();
    tool.description = j.at("description").get<std::string>();
    
    if (j.contains("inputSchema") && j["inputSchema"].contains("properties")) {
        const auto& props = j["inputSchema"]["properties"];
        json required_params;
        
        if (j["inputSchema"].contains("required")) {
            required_params = j["inputSchema"]["required"];
        }
        
        for (const auto& [param_name, param_json] : props.items()) {
            ToolParameter param;
            param.type = param_json.value("type", "string");
            param.description = param_json.value("description", "");
            
            if (param_json.contains("default")) {
                param.default_value = param_json["default"];
            }
            
            if (param_json.contains("enum")) {
                param.enum_values = param_json["enum"];
            }
            
            param.required = required_params.is_array() && 
                           std::find(required_params.begin(), required_params.end(), param_name) != required_params.end();
            
            tool.parameters[param_name] = param;
        }
    }
    
    return tool;
}

json Resource::ToJson() const {
    json j;
    j["uri"] = uri;
    j["name"] = name;
    
    if (description.has_value()) {
        j["description"] = description.value();
    }
    
    if (mime_type.has_value()) {
        j["mimeType"] = mime_type.value();
    }
    
    return j;
}

Resource Resource::FromJson(const json& j) {
    Resource resource;
    resource.uri = j.at("uri").get<std::string>();
    resource.name = j.at("name").get<std::string>();
    
    if (j.contains("description")) {
        resource.description = j["description"].get<std::string>();
    }
    
    if (j.contains("mimeType")) {
        resource.mime_type = j["mimeType"].get<std::string>();
    }
    
    return resource;
}

json ToolCall::ToJson() const {
    json j;
    j["name"] = name;
    j["arguments"] = arguments;
    return j;
}

ToolCall ToolCall::FromJson(const json& j) {
    ToolCall call;
    call.name = j.at("name").get<std::string>();
    call.arguments = j.value("arguments", json::object());
    return call;
}

json ToolResult::ToJson() const {
    json j;
    j["type"] = type;
    j["content"] = content;
    
    if (is_error.has_value()) {
        j["isError"] = is_error.value();
    }
    
    return j;
}

ToolResult ToolResult::FromJson(const json& j) {
    ToolResult result;
    result.type = j.at("type").get<std::string>();
    result.content = j.at("content");
    
    if (j.contains("isError")) {
        result.is_error = j["isError"].get<bool>();
    }
    
    return result;
}

void McpServer::AddTool(const Tool& tool) {
    tools_.push_back(tool);
}

void McpServer::AddResource(const Resource& resource) {
    resources_.push_back(resource);
}

JsonRpcResponse McpServer::HandleRequest(const JsonRpcRequest& request) {
    if (request.method == mcp_methods::INITIALIZE) {
        return HandleInitialize(request.params);
    }
    
    if (!initialized_ && request.method != mcp_methods::INITIALIZE) {
        return CreateErrorResponse(request.id, error_codes::INVALID_REQUEST, 
                                   "Server not initialized");
    }
    
    if (request.method == mcp_methods::TOOLS_LIST) {
        return HandleToolsList(request.params);
    } else if (request.method == mcp_methods::TOOLS_CALL) {
        return HandleToolsCall(request.params);
    } else if (request.method == mcp_methods::RESOURCES_LIST) {
        return HandleResourcesList(request.params);
    } else if (request.method == mcp_methods::RESOURCES_READ) {
        return HandleResourcesRead(request.params);
    }
    
    return CreateErrorResponse(request.id, error_codes::METHOD_NOT_FOUND, 
                               "Method not found: " + request.method);
}

JsonRpcResponse McpServer::HandleInitialize(const json& params) {
    initialized_ = true;
    
    json result;
    result["protocolVersion"] = "2024-11-05";
    result["capabilities"] = {
        {"tools", json::object()},
        {"resources", json::object()}
    };
    result["serverInfo"] = {
        {"name", "MCP Weather Server"},
        {"version", "1.0.0"}
    };
    
    return CreateSuccessResponse(std::nullopt, result);
}

JsonRpcResponse McpServer::HandleToolsList(const json& params) {
    json result;
    result["tools"] = json::array();
    
    for (const auto& tool : tools_) {
        result["tools"].push_back(tool.ToJson());
    }
    
    return CreateSuccessResponse(std::nullopt, result);
}

JsonRpcResponse McpServer::HandleToolsCall(const json& params) {
    if (!params.contains("name")) {
        return CreateErrorResponse(std::nullopt, error_codes::INVALID_PARAMS, 
                                   "Missing tool name");
    }
    
    std::string tool_name = params["name"];
    json arguments = params.value("arguments", json::object());
    
    try {
        auto result = ExecuteTool(tool_name, arguments);
        json response_content;
        response_content["content"] = json::array();
        response_content["content"].push_back(result.ToJson());
        
        return CreateSuccessResponse(std::nullopt, response_content);
    } catch (const std::exception& e) {
        return CreateErrorResponse(std::nullopt, error_codes::INTERNAL_ERROR, 
                                   "Tool execution failed: " + std::string(e.what()));
    }
}

JsonRpcResponse McpServer::HandleResourcesList(const json& params) {
    json result;
    result["resources"] = json::array();
    
    for (const auto& resource : resources_) {
        result["resources"].push_back(resource.ToJson());
    }
    
    return CreateSuccessResponse(std::nullopt, result);
}

JsonRpcResponse McpServer::HandleResourcesRead(const json& params) {
    if (!params.contains("uri")) {
        return CreateErrorResponse(std::nullopt, error_codes::INVALID_PARAMS, 
                                   "Missing resource URI");
    }
    
    std::string uri = params["uri"];
    
    try {
        auto content = ReadResource(uri);
        json result;
        result["contents"] = json::array();
        result["contents"].push_back({
            {"uri", uri},
            {"mimeType", "application/json"},
            {"text", content.dump()}
        });
        
        return CreateSuccessResponse(std::nullopt, result);
    } catch (const std::exception& e) {
        return CreateErrorResponse(std::nullopt, error_codes::INTERNAL_ERROR, 
                                   "Resource read failed: " + std::string(e.what()));
    }
}

} // namespace mcp