#include "mcp/json_rpc.h"
#include <gtest/gtest.h>

using namespace mcp;

class TestHandler : public JsonRpcHandler {
public:
    JsonRpcResponse HandleRequest(const JsonRpcRequest& request) override {
        if (request.method == "echo") {
            return CreateSuccessResponse(request.id, request.params);
        } else if (request.method == "error") {
            return CreateErrorResponse(request.id, -1, "Test error");
        }
        
        return CreateErrorResponse(request.id, error_codes::METHOD_NOT_FOUND, "Method not found");
    }
};

TEST(JsonRpcTest, RequestSerialization) {
    JsonRpcRequest request;
    request.method = "test_method";
    request.params = json{{"key", "value"}};
    request.id = "123";
    
    json j = request.ToJson();
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["method"], "test_method");
    EXPECT_EQ(j["params"]["key"], "value");
    EXPECT_EQ(j["id"], "123");
    
    JsonRpcRequest parsed = JsonRpcRequest::FromJson(j);
    EXPECT_EQ(parsed.method, request.method);
    EXPECT_EQ(parsed.params, request.params);
    EXPECT_EQ(parsed.id, request.id);
}

TEST(JsonRpcTest, ResponseSerialization) {
    JsonRpcResponse response;
    response.result = json{{"result", "success"}};
    response.id = "123";
    
    json j = response.ToJson();
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["result"]["result"], "success");
    EXPECT_EQ(j["id"], "123");
    EXPECT_FALSE(j.contains("error"));
    
    JsonRpcResponse parsed = JsonRpcResponse::FromJson(j);
    EXPECT_EQ(parsed.result, response.result);
    EXPECT_EQ(parsed.id, response.id);
    EXPECT_TRUE(parsed.IsSuccess());
    EXPECT_FALSE(parsed.IsError());
    
    // Test error response
    JsonRpcResponse error_response;
    error_response.error = json{{"code", -1}, {"message", "Test error"}};
    error_response.id = "456";
    
    json error_j = error_response.ToJson();
    EXPECT_EQ(error_j["error"]["code"], -1);
    EXPECT_EQ(error_j["error"]["message"], "Test error");
    
    JsonRpcResponse error_parsed = JsonRpcResponse::FromJson(error_j);
    EXPECT_TRUE(error_parsed.IsError());
    EXPECT_FALSE(error_parsed.IsSuccess());
}

TEST(JsonRpcTest, HandlerProcessing) {
    TestHandler handler;
    
    // Test successful request
    std::string request_msg = R"({"jsonrpc":"2.0","method":"echo","params":{"test":"value"},"id":"1"})";
    std::string response_msg = handler.ProcessMessage(request_msg);
    
    json response_json = json::parse(response_msg);
    EXPECT_EQ(response_json["jsonrpc"], "2.0");
    EXPECT_EQ(response_json["result"]["test"], "value");
    EXPECT_EQ(response_json["id"], "1");
    
    // Test error request
    std::string error_request = R"({"jsonrpc":"2.0","method":"error","id":"2"})";
    std::string error_response = handler.ProcessMessage(error_request);
    
    json error_json = json::parse(error_response);
    EXPECT_EQ(error_json["error"]["code"], -1);
    EXPECT_EQ(error_json["error"]["message"], "Test error");
    
    // Test method not found
    std::string not_found_request = R"({"jsonrpc":"2.0","method":"unknown","id":"3"})";
    std::string not_found_response = handler.ProcessMessage(not_found_request);
    
    json not_found_json = json::parse(not_found_response);
    EXPECT_EQ(not_found_json["error"]["code"], error_codes::METHOD_NOT_FOUND);
    
    // Test parse error
    std::string invalid_json = "invalid json";
    std::string parse_error_response = handler.ProcessMessage(invalid_json);
    
    json parse_error_json = json::parse(parse_error_response);
    EXPECT_EQ(parse_error_json["error"]["code"], error_codes::PARSE_ERROR);
}

