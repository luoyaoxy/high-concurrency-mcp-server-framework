#include "mcp/http_transport.h"
#include "mcp/config.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

namespace mcp {

// HttpClient implementation
HttpClient::HttpClient() : curl_(nullptr), headers_(nullptr), 
    timeout_(MCP_CONFIG.GetClientDefaultTimeoutSeconds()), 
    connect_timeout_(MCP_CONFIG.GetClientDefaultConnectTimeoutSeconds()) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_ = curl_easy_init();
    
    if (curl_) {
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, timeout_);
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, connect_timeout_);
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);
    }
}

HttpClient::~HttpClient() {
    if (headers_) {
        curl_slist_free_all(headers_);
    }
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
    curl_global_cleanup();
}

HttpResponse HttpClient::post(const std::string& url, const std::string& data, 
                             const std::string& content_type) {
    HttpResponse response;
    
    if (!curl_) {
        response.error_message = "CURL not initialized";
        return response;
    }
    
    std::string response_body;
    
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_body);
    
    struct curl_slist* temp_headers = nullptr;
    std::string content_type_header = "Content-Type: " + content_type;
    temp_headers = curl_slist_append(temp_headers, content_type_header.c_str());
    
    if (headers_) {
        struct curl_slist* current = headers_;
        while (current) {
            temp_headers = curl_slist_append(temp_headers, current->data);
            current = current->next;
        }
    }
    
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, temp_headers);
    
    CURLcode res = curl_easy_perform(curl_);
    
    curl_slist_free_all(temp_headers);
    
    if (res != CURLE_OK) {
        response.error_message = curl_easy_strerror(res);
    } else {
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.status_code);
        response.body = response_body;
        
        char* ct = nullptr;
        curl_easy_getinfo(curl_, CURLINFO_CONTENT_TYPE, &ct);
        if (ct) {
            response.content_type = ct;
        }
    }
    
    return response;
}

HttpResponse HttpClient::get(const std::string& url) {
    HttpResponse response;
    
    if (!curl_) {
        response.error_message = "CURL not initialized";
        return response;
    }
    
    std::string response_body;
    
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_body);
    
    if (headers_) {
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
    }
    
    CURLcode res = curl_easy_perform(curl_);
    
    if (res != CURLE_OK) {
        response.error_message = curl_easy_strerror(res);
    } else {
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.status_code);
        response.body = response_body;
        
        char* ct = nullptr;
        curl_easy_getinfo(curl_, CURLINFO_CONTENT_TYPE, &ct);
        if (ct) {
            response.content_type = ct;
        }
    }
    
    return response;
}

void HttpClient::SetTimeout(long timeout_seconds) {
    timeout_ = timeout_seconds;
    if (curl_) {
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, timeout_);
    }
}

void HttpClient::SetConnectTimeout(long timeout_seconds) {
    connect_timeout_ = timeout_seconds;
    if (curl_) {
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, connect_timeout_);
    }
}

void HttpClient::AddHeader(const std::string& header) {
    headers_ = curl_slist_append(headers_, header.c_str());
}

size_t HttpClient::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// SseClient implementation
SseClient::SseClient(const std::string& url) 
    : url_(url), running_(false), connected_(false) {}

    SseClient::~SseClient() {
    Stop();
}

void SseClient::SetMessageCallback(MessageCallback callback) {
    message_callback_ = callback;
}

void SseClient::SetErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

bool SseClient::Start() {
    if (running_.load()) {
        return false;
    }
    
    running_ = true;
    worker_thread_ = std::make_unique<std::thread>(&SseClient::WorkerLoop, this);
    return true;
}

void SseClient::Stop() {
    running_ = false;
    connected_ = false;
    
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
}

bool SseClient::IsConnected() const {
    return connected_.load();
}

void SseClient::WorkerLoop() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (error_callback_) {
            error_callback_("Failed to initialize CURL");
        }
        return;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SseWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    headers = curl_slist_append(headers, "Cache-Control: no-cache");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    while (running_.load()) {
        connected_ = true;
        CURLcode res = curl_easy_perform(curl);
        connected_ = false;
        
        if (res != CURLE_OK && error_callback_) {
            error_callback_("SSE connection error: " + std::string(curl_easy_strerror(res)));
        }
        
        if (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(MCP_CONFIG.GetHttpSseReconnectDelaySeconds()));
        }
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

void SseClient::ParseSseMessage(const std::string& raw_message) {
    std::istringstream iss(raw_message);
    std::string line;
    std::string event_type = "message";
    std::string data;
    
    while (std::getline(iss, line)) {
        if (line.empty() || line == "\r") {
            if (!data.empty() && message_callback_) {
                message_callback_(data, event_type);
                data.clear();
                event_type = "message";
            }
            continue;
        }
        
        if (line.substr(0, 6) == "event:") {
            event_type = line.substr(6);
            if (!event_type.empty() && event_type[0] == ' ') {
                event_type = event_type.substr(1);
            }
        } else if (line.substr(0, 5) == "data:") {
            std::string line_data = line.substr(5);
            if (!line_data.empty() && line_data[0] == ' ') {
                line_data = line_data.substr(1);
            }
            if (!data.empty()) {
                data += "\n";
            }
            data += line_data;
        }
    }
}

size_t SseClient::SseWriteCallback(void* contents, size_t size, size_t nmemb, SseClient* client) {
    size_t total_size = size * nmemb;
    std::string data(static_cast<char*>(contents), total_size);
    client->ParseSseMessage(data);
    return total_size;
}

// HttpServer implementation
HttpServer::HttpServer(int port) 
    : port_(port), running_(false), cors_enabled_(false) {}

HttpServer::~HttpServer() {
    Stop();
}

void HttpServer::SetRequestHandler(RequestHandler handler) {
    request_handler_ = handler;
}

bool HttpServer::Start() {
    if (running_.load()) {
        return false;
    }
    
    running_ = true;
    server_thread_ = std::make_unique<std::thread>(&HttpServer::ServerLoop, this);
    return true;
}

void HttpServer::Stop() {
    running_ = false;
    
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
}

bool HttpServer::IsRunning() const {
    return running_.load();
}

void HttpServer::EnableCors(bool enable) {
    cors_enabled_ = enable;
}

void HttpServer::SetStaticPath(const std::string& path) {
    static_path_ = path;
}

void HttpServer::ServerLoop() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return;
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        std::cerr << "Setsockopt failed" << std::endl;
        close(server_fd);
        return;
    }
    
    // 设置非阻塞模式，以便能够响应关闭信号
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, MCP_CONFIG.GetServerListenBacklog()) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        return;
    }
    
    std::cout << "Server listening on port " << port_ << std::endl;
    
    while (running_.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞模式下没有连接，短暂等待后继续
                std::this_thread::sleep_for(std::chrono::milliseconds(MCP_CONFIG.GetServerSocketTimeoutMs()));
                continue;
            } else if (running_.load()) {
                std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            }
            continue;
        }
        
        int buffer_size = MCP_CONFIG.GetServerRequestBufferSize();
        std::vector<char> buffer(buffer_size, 0);
        ssize_t bytes_read = read(client_socket, buffer.data(), buffer.size() - 1);
        
        if (bytes_read > 0) {
            std::string request(buffer.data(), bytes_read);
            std::string response = HandleRequest(request);
            send(client_socket, response.c_str(), response.length(), 0);
        }
        
        close(client_socket);
    }
    
    close(server_fd);
}

std::string HttpServer::HandleRequest(const std::string& request) {
    std::istringstream iss(request);
    std::string line;
    
    if (!std::getline(iss, line)) {
        return CreateResponse(400, "Bad Request");
    }
    
    std::istringstream request_line(line);
    std::string method, path, version;
    request_line >> method >> path >> version;
    
    std::string content_type;
    std::string body;
    size_t content_length = 0;
    
    while (std::getline(iss, line) && line != "\r") {
        if (line.substr(0, 14) == "Content-Type: ") {
            content_type = line.substr(14);
            if (!content_type.empty() && content_type.back() == '\r') {
                content_type.pop_back();
            }
        } else if (line.substr(0, 16) == "Content-Length: ") {
            content_length = std::stoul(line.substr(16));
        }
    }
    
    if (content_length > 0) {
        body.resize(content_length);
        iss.read(&body[0], content_length);
    }
    
    if (request_handler_) {
        std::string response_body = request_handler_(path, method, body, content_type);
        return CreateResponse(200, response_body);
    }
    
    return CreateResponse(404, "Not Found");
}

std::string HttpServer::CreateResponse(int status_code, const std::string& body, 
                                       const std::string& content_type) {
    std::ostringstream response;
    
    response << "HTTP/1.1 " << status_code;
    
    switch (status_code) {
        case 200: response << " OK"; break;
        case 400: response << " Bad Request"; break;
        case 404: response << " Not Found"; break;
        case 500: response << " Internal Server Error"; break;
        default: response << " Unknown"; break;
    }
    
    response << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    
    if (cors_enabled_) {
        response << "Access-Control-Allow-Origin: " << MCP_CONFIG.GetHttpCorsHeader("allow_origin") << "\r\n";
        response << "Access-Control-Allow-Methods: " << MCP_CONFIG.GetHttpCorsHeader("allow_methods") << "\r\n";
        response << "Access-Control-Allow-Headers: " << MCP_CONFIG.GetHttpCorsHeader("allow_headers") << "\r\n";
    }
    
    response << "\r\n";
    response << body;
    
    return response.str();
}

} // namespace mcp