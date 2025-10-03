#include "mcp/jsonrpc.h"
#include <gtest/gtest.h>
#include <sstream>

using namespace mcp;

static JsonRpcDispatcher makeDispatcher() {
    JsonRpcDispatcher d;
    d.registerHandler("echo", [](const json& params) {
        return params;
    });
    d.registerHandler("add", [](const json& params) {
        if (!params.is_array() || params.size() != 2 || !params[0].is_number() || !params[1].is_number()) {
            throw std::invalid_argument("params must be [number, number]");
        }
        return json(params[0].get<int>() + params[1].get<int>());
    });
    return d;
}

static std::string makeFrame(const json& obj) {
    std::string payload = obj.dump();
    std::ostringstream os;
    os << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
    return os.str();
}

static json parseFirstFramePayload(const std::string& framed) {
    // 期望格式："Content-Length: <n>\r\n\r\n<payload>"
    const std::string header = "Content-Length:";
    auto header_pos = framed.find(header);
    if (header_pos == std::string::npos) return json();
    auto eol = framed.find("\r\n", header_pos);
    if (eol == std::string::npos) return json();
    auto sep = framed.find("\r\n\r\n", eol);
    if (sep == std::string::npos) return json();
    std::string len_str = framed.substr(header_pos + header.size(), eol - (header_pos + header.size()));
    // trim leading spaces
    size_t pos = len_str.find_first_not_of(' ');
    if (pos != std::string::npos) len_str = len_str.substr(pos);
    size_t content_length = static_cast<size_t>(std::stoul(len_str));
    std::string payload = framed.substr(sep + 4, content_length);
    return json::parse(payload);
}

TEST(StdioJsonRpcServerTest, BasicSuccess) {
    JsonRpcDispatcher d = makeDispatcher();
    std::stringstream in;
    std::stringstream out;

    json req = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "add"},
        {"params", json::array({1, 2})}
    };
    in << makeFrame(req);

    StdioJsonRpcServer server(std::move(d), in, out);

    server.run();
    auto out_str = out.str();
    auto resp_json = parseFirstFramePayload(out_str);
    ASSERT_TRUE(resp_json.contains("result"));
    ASSERT_EQ(resp_json["result"].get<int>(), 3);
}

TEST(StdioJsonRpcServerTest, MethodNotFound) {
    JsonRpcDispatcher d;
    std::stringstream in;
    std::stringstream out;

    json req = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "unknown"}
    };
    in << makeFrame(req);

    StdioJsonRpcServer server(std::move(d), in, out);
    server.run();
    auto out_str = out.str();
    auto resp_json = parseFirstFramePayload(out_str);
    ASSERT_TRUE(resp_json.contains("error"));
    ASSERT_EQ(resp_json["error"]["code"].get<int>(), jsonrpc_errc::MethodNotFound);
}

TEST(StdioJsonRpcServerTest, InvalidRequestAndParseError) {
    JsonRpcDispatcher d = makeDispatcher();
    std::stringstream in;
    std::stringstream out;

    // Invalid Request: missing method
    json bad = {
        {"jsonrpc", "2.0"},
        {"id", 1}
    };
    in << makeFrame(bad);

    StdioJsonRpcServer server(std::move(d), in, out);
    server.run();
    auto out_str1 = out.str();
    auto resp_json1 = parseFirstFramePayload(out_str1);
    ASSERT_TRUE(resp_json1.contains("error"));
    ASSERT_EQ(resp_json1["error"]["code"].get<int>(), jsonrpc_errc::InvalidRequest);

    // Parse Error: invalid json
    std::stringstream in2;
    std::stringstream out2;
    std::string invalid_json = "{"; // malformed
    std::ostringstream os;
    os << "Content-Length: " << invalid_json.size() << "\r\n\r\n" << invalid_json;
    in2 << os.str();

    StdioJsonRpcServer server2(JsonRpcDispatcher{}, in2, out2);
    server2.run();
    std::string written = out2.str();
    ASSERT_NE(std::string::npos, written.find("Content-Length:"));
    auto resp_json2 = parseFirstFramePayload(written);
    ASSERT_TRUE(resp_json2.contains("error"));
    ASSERT_EQ(resp_json2["error"]["code"].get<int>(), jsonrpc_errc::ParseError);
}

TEST(StdioJsonRpcServerTest, NotificationNoResponse) {
    JsonRpcDispatcher d = makeDispatcher();
    std::stringstream in;
    std::stringstream out;

    json notify = {
        {"jsonrpc", "2.0"},
        {"method", "echo"},
        {"params", json{{"k", 1}}}
    }; // no id
    in << makeFrame(notify);

    StdioJsonRpcServer server(std::move(d), in, out);
    server.run();
    ASSERT_TRUE(out.str().empty());
}


