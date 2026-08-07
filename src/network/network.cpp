#include "network/network.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#endif

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

namespace ashgrove {

std::string GameMessage::serialize() const {
    nlohmann::json j;
    j["type"] = static_cast<int>(type);
    j["payload"] = payload;
    return j.dump();
}

GameMessage GameMessage::deserialize(const std::string& json_str) {
    GameMessage msg;
    try {
        auto j = nlohmann::json::parse(json_str);
        msg.type = static_cast<GameMessage::Type>(j.value("type", 0));
        if (j.contains("payload")) msg.payload = j["payload"];
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to parse game message: {}", e.what());
        msg.type = GameMessage::Type::Error;
        msg.payload = {{"error", "Malformed message"}};
    }
    return msg;
}

static std::string base64_encode(const unsigned char* input, size_t length) {
    BIO* bmem = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, input, static_cast<int>(length));
    BIO_flush(b64);
    
    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(b64, &bptr);
    
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

SocketTransport::~SocketTransport() {
    stop();
}

void SocketTransport::start(int port) {
    if (running_) return;
    port_ = port;
    running_ = true;
    
    listen_socket_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (listen_socket_ < 0) {
        spdlog::error("Failed to create socket");
        running_ = false;
        return;
    }
    
    int opt = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    
    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::error("Failed to bind to port {}", port);
        closesocket(listen_socket_);
        listen_socket_ = -1;
        running_ = false;
        return;
    }
    
    if (listen(listen_socket_, 10) < 0) {
        spdlog::error("Failed to listen on port {}", port);
        closesocket(listen_socket_);
        listen_socket_ = -1;
        running_ = false;
        return;
    }
    
    spdlog::info("Socket transport listening on port {}", port);
    accept_thread_ = std::thread([this] { accept_loop(); });
}

void SocketTransport::stop() {
    running_ = false;
    
    if (listen_socket_ != -1) {
        closesocket(listen_socket_);
        listen_socket_ = -1;
    }
    
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int sock : client_sockets_) {
        closesocket(sock);
    }
    client_sockets_.clear();
    for (auto& t : client_threads_) {
        if (t.joinable()) t.detach();
    }
    client_threads_.clear();
}

void SocketTransport::register_http_route(const std::string& method, const std::string& path, HTTPHandler handler) {
    http_routes_[method + " " + path] = handler;
}

void SocketTransport::register_websocket_handler(WebSocketHandler handler) {
    ws_handler_ = handler;
}

void SocketTransport::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int sock : websocket_sockets_) {
        send_websocket_frame(sock, message);
    }
}

void SocketTransport::send_to_client(const std::string& client_id, const std::string& message) {
    if (client_id.empty()) {
        // Broadcast to all (simplified for now)
        broadcast(message);
        return;
    }
    // TODO: Track client IDs per socket
    spdlog::warn("send_to_client with specific client_id not yet implemented");
}

bool SocketTransport::is_running() const {
    return running_;
}

int SocketTransport::get_port() const {
    return port_;
}

void SocketTransport::accept_loop() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        
        int client_socket = static_cast<int>(accept(listen_socket_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len));
        if (client_socket < 0) {
            if (!running_) break;
            continue;
        }
        
        spdlog::debug("New connection from {}:{}", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_sockets_.push_back(client_socket);
            client_threads_.emplace_back([this, client_socket] { handle_connection(client_socket); });
        }
    }
}

void SocketTransport::handle_connection(int client_socket) {
    std::string buffer;
    std::vector<char> buf(8192);
    
    while (running_) {
        ssize_t received = recv(client_socket, buf.data(), buf.size() - 1, 0);
        if (received <= 0) break;
        buffer.append(buf.data(), static_cast<size_t>(received));
        
        // Check if we have a complete HTTP request (headers terminated by \r\n\r\n)
        size_t header_end = buffer.find("\r\n\r\n");
        if (header_end == std::string::npos) continue;
        
        std::string header_block = buffer.substr(0, header_end + 4);
        
        // Check for WebSocket upgrade
        if (header_block.find("Upgrade: websocket") != std::string::npos ||
            header_block.find("upgrade: websocket") != std::string::npos) {
            if (perform_websocket_handshake(client_socket, header_block)) {
                spdlog::debug("WebSocket handshake completed");
                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    websocket_sockets_.insert(client_socket);
                }
                websocket_loop(client_socket, header_block);
            } else {
                spdlog::warn("WebSocket handshake failed");
            }
            break;
        }
        
        // Regular HTTP request
        HTTPRequest req = parse_http_request(header_block);
        
        // Read body if Content-Length specified
        auto content_length_it = req.headers.find("Content-Length");
        if (content_length_it != req.headers.end()) {
            size_t content_length = std::stoul(content_length_it->second);
            size_t body_start = header_end + 4;
            while (buffer.size() < body_start + content_length) {
                ssize_t more = recv(client_socket, buf.data(), buf.size() - 1, 0);
                if (more <= 0) break;
                buffer.append(buf.data(), static_cast<size_t>(more));
            }
            if (buffer.size() >= body_start + content_length) {
                req.body = buffer.substr(body_start, content_length);
            }
        }
        
        HTTPResponse resp = dispatch_http(req);
        
        std::string response = "HTTP/1.1 " + std::to_string(resp.status_code) + " " + 
            (resp.status_code == 200 ? "OK" : resp.status_code == 204 ? "No Content" : resp.status_code == 404 ? "Not Found" : "Error") + "\r\n" +
            "Content-Type: " + resp.content_type + "\r\n" +
            "Content-Length: " + std::to_string(resp.body.size()) + "\r\n" +
            "Access-Control-Allow-Origin: *\r\n";
        for (const auto& [k, v] : resp.extra_headers) {
            response += k + ": " + v + "\r\n";
        }
        response += "Connection: close\r\n\r\n" + resp.body;
        
        send(client_socket, response.data(), response.size(), 0);
        break; // Close connection after response (keep-alive not implemented)
    }
    
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_sockets_.erase(std::remove(client_sockets_.begin(), client_sockets_.end(), client_socket), client_sockets_.end());
        websocket_sockets_.erase(client_socket);
    }
    closesocket(client_socket);
}

HTTPRequest SocketTransport::parse_http_request(const std::string& header_block) {
    HTTPRequest req;
    std::istringstream iss(header_block);
    std::string line;
    
    // Request line: METHOD PATH HTTP/1.1
    if (std::getline(iss, line)) {
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        std::istringstream line_stream(line);
        line_stream >> req.method >> req.path;
        
        // Remove leading "api/" normalization concerns; split query string
        size_t query_pos = req.path.find('?');
        if (query_pos != std::string::npos) {
            std::string query_str = req.path.substr(query_pos + 1);
            req.path = req.path.substr(0, query_pos);
            // Parse query params (simple key=value&...)
            std::istringstream qs(query_str);
            std::string param;
            while (std::getline(qs, param, '&')) {
                size_t eq = param.find('=');
                if (eq != std::string::npos) {
                    req.query_params[param.substr(0, eq)] = param.substr(eq + 1);
                }
            }
        }
    }
    
    // Headers
    while (std::getline(iss, line)) {
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        if (line.empty()) break;
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            // Trim leading spaces
            size_t start = value.find_first_not_of(" \t");
            if (start != std::string::npos) value = value.substr(start);
            req.headers[key] = value;
        }
    }
    return req;
}

HTTPResponse SocketTransport::dispatch_http(const HTTPRequest& req) {
    // CORS preflight: respond 204 with the allowed origin/methods/headers so
    // browsers allow cross-origin POSTs carrying a JSON body.
    if (req.method == "OPTIONS") {
        return HTTPResponse{204, "application/json", "", {
            {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
            {"Access-Control-Allow-Headers", "Content-Type, Accept"},
            {"Access-Control-Allow-Private-Network", "true"},
            {"Access-Control-Max-Age", "86400"},
        }};
    }

    std::string key = req.method + " " + req.path;
    
    auto it = http_routes_.find(key);
    if (it == http_routes_.end()) {
        spdlog::warn("No route for {} {}", req.method, req.path);
        return HTTPResponse{404, "application/json", nlohmann::json{{"error", "Not found"}}.dump()};
    }
    
    try {
        return it->second(req);
    } catch (const std::exception& e) {
        spdlog::error("HTTP handler error: {}", e.what());
        return HTTPResponse{500, "application/json", nlohmann::json{{"error", e.what()}}.dump()};
    }
}

bool SocketTransport::perform_websocket_handshake(int socket, const std::string& request) {
    // Extract Sec-WebSocket-Key
    std::string key;
    size_t key_pos = request.find("Sec-WebSocket-Key:");
    if (key_pos != std::string::npos) {
        size_t start = key_pos + strlen("Sec-WebSocket-Key:");
        while (start < request.size() && (request[start] == ' ' || request[start] == '\t')) start++;
        size_t end = request.find("\r\n", start);
        if (end != std::string::npos) {
            key = request.substr(start, end - start);
            // Trim trailing spaces
            while (!key.empty() && (key.back() == ' ' || key.back() == '\r')) key.pop_back();
        }
    }
    
    if (key.empty()) {
        spdlog::warn("No Sec-WebSocket-Key in handshake");
        return false;
    }
    
    // Compute Sec-WebSocket-Accept: SHA1(key + GUID), base64 encoded
    std::string combined = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    
    unsigned char hash[20];
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
    EVP_DigestUpdate(ctx, combined.data(), combined.size());
    EVP_DigestFinal_ex(ctx, hash, nullptr);
    EVP_MD_CTX_free(ctx);
    
    std::string accept_key = base64_encode(hash, 20);
    
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n"
        "\r\n";
    
    send(socket, response.data(), response.size(), 0);
    return true;
}

void SocketTransport::websocket_loop(int socket, const std::string&) {
    std::vector<char> buffer(8192);
    
    while (running_) {
        ssize_t received = recv(socket, buffer.data(), buffer.size(), 0);
        if (received <= 0) break;
        
        // Parse WebSocket frame (client -> server, unmasked frames expected per spec client frames ARE masked)
        // Minimal frame parser: FIN+opcode byte, mask byte + length
        unsigned char* data = reinterpret_cast<unsigned char*>(buffer.data());
        if (received < 2) continue;
        
        unsigned char opcode = data[0] & 0x0F;
        unsigned char mask_flag = data[1] & 0x80;
        uint64_t payload_len = data[1] & 0x7F;
        
        size_t offset = 2;
        if (payload_len == 126) {
            if (received < 4) continue;
            payload_len = (static_cast<uint64_t>(data[2]) << 8) | data[3];
            offset = 4;
        } else if (payload_len == 127) {
            if (received < 10) continue;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | data[2 + i];
            }
            offset = 10;
        }
        
        if (opcode == 0x8) { // Close frame
            spdlog::debug("WebSocket close frame received");
            break;
        }
        
        if (opcode == 0x9) { // Ping -> pong
            unsigned char pong[2] = {0x8A, 0x00};
            send(socket, reinterpret_cast<char*>(pong), 2, 0);
            continue;
        }
        
        // Client frames MUST be masked
        if (mask_flag) {
            unsigned char mask_key[4];
            if (static_cast<size_t>(received) < offset + 4) continue;
            memcpy(mask_key, data + offset, 4);
            offset += 4;
            
            if (static_cast<size_t>(received) < offset + payload_len) continue;
            
            std::string message;
            message.reserve(payload_len);
            for (uint64_t i = 0; i < payload_len; ++i) {
                message.push_back(static_cast<char>(data[offset + i] ^ mask_key[i % 4]));
            }
            
            if (opcode == 0x1 || opcode == 0x2) { // Text or binary
                if (ws_handler_) {
                    try {
                        ws_handler_(message);
                    } catch (const std::exception& e) {
                        spdlog::error("WebSocket handler error: {}", e.what());
                    }
                }
            }
        } else {
            // Unmasked data (shouldn't happen from client, but be tolerant)
            if (static_cast<size_t>(received) < offset + payload_len) continue;
            std::string message(reinterpret_cast<char*>(data + offset), static_cast<size_t>(payload_len));
            if (ws_handler_) {
                try {
                    ws_handler_(message);
                } catch (const std::exception& e) {
                    spdlog::error("WebSocket handler error: {}", e.what());
                }
            }
        }
    }
}

void SocketTransport::send_websocket_frame(int socket, const std::string& payload) {
    std::vector<char> frame;
    frame.push_back(static_cast<char>(0x81)); // FIN + Text opcode
    
    size_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
    } else if (len <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
        }
    }
    
    frame.insert(frame.end(), payload.begin(), payload.end());
    send(socket, frame.data(), frame.size(), 0);
}

NetworkServer::NetworkServer(std::shared_ptr<ITransport> transport)
    : transport_(transport) {}

void NetworkServer::start(int port) {
    transport_->start(port);
    register_game_endpoints();
}

void NetworkServer::stop() {
    transport_->stop();
}

void NetworkServer::register_game_endpoints() {
    // Health check
    transport_->register_http_route("GET", "/api/health", [](const HTTPRequest&) {
        return HTTPResponse{200, "application/json", nlohmann::json{{"status", "ok"}, {"game", "ashgrove"}, {"version", "0.1.0"}}.dump()};
    });
    
    // World state
    transport_->register_http_route("GET", "/api/world/state", [this](const HTTPRequest&) {
        if (state_provider_) {
            return HTTPResponse{200, "application/json", state_provider_().dump()};
        }
        return HTTPResponse{500, "application/json", nlohmann::json{{"error", "State provider not set"}}.dump()};
    });
    
    // Player action via REST
    transport_->register_http_route("POST", "/api/action", [this](const HTTPRequest& req) {
        if (!action_handler_) return HTTPResponse{500, "application/json", nlohmann::json{{"error", "Action handler not set"}}.dump()};
        try {
            auto request_json = nlohmann::json::parse(req.body);
            auto result = action_handler_(request_json);
            return HTTPResponse{200, "application/json", result.dump()};
        } catch (const nlohmann::json::exception& e) {
            return HTTPResponse{400, "application/json", nlohmann::json{{"error", e.what()}}.dump()};
        }
    });
    
    // WebSocket handler for real-time communication
    transport_->register_websocket_handler([this](const std::string& message) {
        handle_message(message);
    });
    
    spdlog::info("Game endpoints registered");
}

void NetworkServer::handle_message(const std::string& message) {
    auto game_msg = GameMessage::deserialize(message);
    
    switch (game_msg.type) {
        case GameMessage::Type::RequestState:
            if (state_provider_) {
                GameMessage response;
                response.type = GameMessage::Type::WorldState;
                response.payload = state_provider_();
                transport_->send_to_client("", response.serialize());
            }
            break;
            
        case GameMessage::Type::PlayerAction: {
            if (action_handler_) {
                GameMessage response;
                response.type = GameMessage::Type::WorldState;
                response.payload = action_handler_(game_msg.payload);
                transport_->send_to_client("", response.serialize());
            }
            break;
        }
        
        case GameMessage::Type::SaveGame: {
            if (action_handler_) {
                GameMessage response;
                response.type = GameMessage::Type::SaveResult;
                response.payload = action_handler_({{"type", "save"}});
                transport_->send_to_client("", response.serialize());
            }
            break;
        }
        
        case GameMessage::Type::LoadGame: {
            if (action_handler_) {
                GameMessage response;
                response.type = GameMessage::Type::WorldState;
                response.payload = action_handler_({{"type", "load"}});
                transport_->send_to_client("", response.serialize());
            }
            break;
        }
        
        default:
            break;
    }
}

} // namespace ashgrove