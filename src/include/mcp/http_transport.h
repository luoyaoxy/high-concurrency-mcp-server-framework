#pragma once

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <curl/curl.h>

namespace mcp {

struct HttpResponse {
    long status_code = 0;
    std::string body;
    std::string content_type;
    std::string error_message;
    
    bool IsSuccess() const { return status_code >= 200 && status_code < 300; }
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    HttpResponse post(const std::string& url, const std::string& data, 
                     const std::string& content_type = "application/json");
    
    HttpResponse get(const std::string& url);
    
    void SetTimeout(long timeout_seconds);
    void SetConnectTimeout(long timeout_seconds);
    void AddHeader(const std::string& header);
    
private:
    CURL* curl_;
    struct curl_slist* headers_;
    long timeout_;
    long connect_timeout_;
    
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
};

class SseClient {
public:
    using MessageCallback = std::function<void(const std::string& data, const std::string& event_type)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    SseClient(const std::string& url);
    ~SseClient();
    
    void SetMessageCallback(MessageCallback callback);
    void SetErrorCallback(ErrorCallback callback);
    
    bool Start();
    void Stop();
    bool IsConnected() const;
    
private:
    std::string url_;
    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    std::unique_ptr<std::thread> worker_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    
    void WorkerLoop();
    void ParseSseMessage(const std::string& raw_message);
    
    static size_t SseWriteCallback(void* contents, size_t size, size_t nmemb, SseClient* client);
};

class HttpServer {
public:
    using RequestHandler = std::function<std::string(const std::string& path, 
                                                   const std::string& method,
                                                   const std::string& body,
                                                   const std::string& content_type)>;
    
    HttpServer(int port = 8080);
    ~HttpServer();
    
    void SetRequestHandler(RequestHandler handler);
    bool Start();
    void Stop();
    bool IsRunning() const;
    
    void EnableCors(bool enable = true);
    void SetStaticPath(const std::string& path);
    
private:
    int port_;
    RequestHandler request_handler_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> server_thread_;
    bool cors_enabled_;
    std::string static_path_;
    
    void ServerLoop();
    std::string HandleRequest(const std::string& request);
    std::string CreateResponse(int status_code, const std::string& body, 
                              const std::string& content_type = "application/json");
};

} // namespace mcp