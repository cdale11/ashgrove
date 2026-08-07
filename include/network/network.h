#pragma once

#include "common/types.h"
#include <string>
#include <functional>
#include <memory>
#include <map>
#include <thread>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>

namespace ashgrove {

struct HTTPRequest {
    std::string method;   // GET, POST, PUT, DELETE
    std::string path;     // /api/world/state
    std::map<std::string, std::string> headers;
    std::string body;
    std::map<std::string, std::string> query_params;
};

struct HTTPResponse {
    int status_code = 200;
    std::string content_type = "application/json";
    std::string body;
};

using HTTPHandler = std::function<HTTPResponse(const HTTPRequest&)>;
using WebSocketHandler = std::function<void(const std::string& message)>;

// Game protocol messages (sent over WebSocket)
struct GameMessage {
    enum class Type : uint8_t {
        // Client -> Server
        PlayerAction,
        RequestState,
        DialogueChoice,
        Interact,
        Move,
        UseItem,
        SaveGame,
        LoadGame,
        // Server -> Client
        WorldState,
        DialogueResponse,
        EventNotification,
        SaveResult,
        Error,
        TimeSync
    };
    
    Type type;
    nlohmann::json payload;
    std::string serialize() const;
    static GameMessage deserialize(const std::string& json_str);
};

// Abstract transport interface - allows swapping between REST and WebSocket
class ITransport {
public:
    virtual ~ITransport() = default;
    
    virtual void start(int port) = 0;
    virtual void stop() = 0;
    
    virtual void register_http_route(const std::string& method, const std::string& path, HTTPHandler handler) = 0;
    virtual void register_websocket_handler(WebSocketHandler handler) = 0;
    virtual void broadcast(const std::string& message) = 0;
    virtual void send_to_client(const std::string& client_id, const std::string& message) = 0;
    
    virtual bool is_running() const = 0;
    virtual int get_port() const = 0;
};

class NetworkServer {
public:
    NetworkServer(std::shared_ptr<ITransport> transport);
    ~NetworkServer() = default;
    
    void start(int port);
    void stop();
    
    // Register game endpoints
    void register_game_endpoints();
    
    // Handle incoming WebSocket messages
    void handle_message(const std::string& message);
    
    // Game state access (injected by server)
    using StateProvider = std::function<nlohmann::json()>;
    using ActionHandler = std::function<nlohmann::json(const nlohmann::json&)>;
    
    void set_state_provider(StateProvider provider) { state_provider_ = provider; }
    void set_action_handler(ActionHandler handler) { action_handler_ = handler; }
    
private:
    std::shared_ptr<ITransport> transport_;
    StateProvider state_provider_;
    ActionHandler action_handler_;
};

// Socket-based transport using POSIX sockets (cross-platform via #ifdef)
// For the vertical slice this provides a basic TCP listener; full HTTP parsing
// and WebSocket upgrades are added incrementally.
class SocketTransport : public ITransport {
public:
    SocketTransport() = default;
    ~SocketTransport() override;

    void start(int port) override;
    void stop() override;

    void register_http_route(const std::string& method, const std::string& path, HTTPHandler handler) override;
    void register_websocket_handler(WebSocketHandler handler) override;

    void broadcast(const std::string& message) override;
    void send_to_client(const std::string& client_id, const std::string& message) override;

    bool is_running() const override;
    int get_port() const override;

private:
    int port_ = 8000;
    bool running_ = false;
    std::map<std::string, HTTPHandler> http_routes_;
    WebSocketHandler ws_handler_;
    std::thread accept_thread_;
    int listen_socket_ = -1;
    std::vector<std::thread> client_threads_;
    std::vector<int> client_sockets_;
    std::mutex clients_mutex_;

    void accept_loop();
    void handle_connection(int client_socket);
    HTTPRequest parse_http_request(const std::string& header_block);
    HTTPResponse dispatch_http(const HTTPRequest& req);
    void websocket_loop(int socket, const std::string& header_block);
    bool perform_websocket_handshake(int socket, const std::string& request);
    void send_websocket_frame(int socket, const std::string& payload);
};

} // namespace ashgrove