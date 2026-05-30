#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <optional>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace oj {

struct RedisConfig {
  std::string host = "127.0.0.1";
  std::string port = "6379";
  std::string password;
  int database = 0;
};

inline RedisConfig parse_redis_url(const std::string& url) {
  if (url.empty()) {
    throw std::runtime_error("REDIS_URL is not configured");
  }

  RedisConfig config;
  std::string rest = url;
  constexpr std::string_view scheme = "redis://";
  if (rest.rfind(std::string(scheme), 0) == 0) {
    rest = rest.substr(scheme.size());
  }

  const auto slash = rest.find('/');
  if (slash != std::string::npos) {
    const std::string db = rest.substr(slash + 1);
    rest = rest.substr(0, slash);
    if (!db.empty()) {
      config.database = std::stoi(db);
    }
  }

  const auto at = rest.find('@');
  if (at != std::string::npos) {
    std::string auth = rest.substr(0, at);
    rest = rest.substr(at + 1);
    const auto colon = auth.find(':');
    config.password = colon == std::string::npos ? auth : auth.substr(colon + 1);
  }

  if (!rest.empty() && rest.front() == '[') {
    const auto close = rest.find(']');
    if (close == std::string::npos) {
      throw std::runtime_error("Invalid Redis IPv6 URL");
    }
    config.host = rest.substr(1, close - 1);
    if (close + 2 <= rest.size() && rest[close + 1] == ':') {
      config.port = rest.substr(close + 2);
    }
  } else {
    const auto colon = rest.rfind(':');
    if (colon != std::string::npos) {
      config.host = rest.substr(0, colon);
      config.port = rest.substr(colon + 1);
    } else if (!rest.empty()) {
      config.host = rest;
    }
  }

  if (config.host.empty() || config.port.empty()) {
    throw std::runtime_error("Invalid Redis URL");
  }
  return config;
}

struct RedisValue {
  enum class Type { SimpleString, Error, Integer, BulkString, Array, Nil };

  Type type = Type::Nil;
  std::string text;
  std::vector<RedisValue> items;
};

class RedisClient {
 public:
  explicit RedisClient(std::string url) : config_(parse_redis_url(url)) {}

  void lpush(const std::string& key, const std::string& value) {
    RedisValue reply = command({"LPUSH", key, value});
    if (reply.type != RedisValue::Type::Integer) {
      throw std::runtime_error("Unexpected Redis LPUSH reply");
    }
  }

  std::string ping() {
    RedisValue reply = command({"PING"});
    if (reply.type != RedisValue::Type::SimpleString && reply.type != RedisValue::Type::BulkString) {
      throw std::runtime_error("Unexpected Redis PING reply");
    }
    return reply.text;
  }

  std::optional<std::string> brpop(const std::string& key, int timeout_seconds) {
    RedisValue reply = command({"BRPOP", key, std::to_string(timeout_seconds)});
    if (reply.type == RedisValue::Type::Nil) {
      return std::nullopt;
    }
    if (reply.type != RedisValue::Type::Array || reply.items.size() != 2 ||
        reply.items[1].type != RedisValue::Type::BulkString) {
      throw std::runtime_error("Unexpected Redis BRPOP reply");
    }
    return reply.items[1].text;
  }

 private:
#ifdef _WIN32
  using SocketHandle = SOCKET;
  static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

  struct WinsockInit {
    WinsockInit() {
      WSADATA data{};
      if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("WSAStartup failed");
      }
    }
    ~WinsockInit() { WSACleanup(); }
  };

  static void close_socket(SocketHandle socket) {
    if (socket != kInvalidSocket) {
      closesocket(socket);
    }
  }
#else
  using SocketHandle = int;
  static constexpr SocketHandle kInvalidSocket = -1;

  static void close_socket(SocketHandle socket) {
    if (socket != kInvalidSocket) {
      close(socket);
    }
  }
#endif

  class Connection {
   public:
    explicit Connection(const RedisConfig& config) {
#ifdef _WIN32
      static WinsockInit winsock;
#endif
      addrinfo hints{};
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_family = AF_UNSPEC;

      addrinfo* result = nullptr;
      const int rc = getaddrinfo(config.host.c_str(), config.port.c_str(), &hints, &result);
      if (rc != 0) {
        throw std::runtime_error("Unable to resolve Redis host");
      }

      for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        socket_ = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (socket_ == kInvalidSocket) {
          continue;
        }
        if (connect(socket_, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0) {
          break;
        }
        close_socket(socket_);
        socket_ = kInvalidSocket;
      }

      freeaddrinfo(result);
      if (socket_ == kInvalidSocket) {
        throw std::runtime_error("Unable to connect to Redis");
      }
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    ~Connection() { close_socket(socket_); }

    void write_all(const std::string& data) {
      size_t sent = 0;
      while (sent < data.size()) {
#ifdef _WIN32
        const int rc = send(socket_, data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
        const ssize_t rc = send(socket_, data.data() + sent, data.size() - sent, 0);
#endif
        if (rc <= 0) {
          throw std::runtime_error("Redis socket write failed");
        }
        sent += static_cast<size_t>(rc);
      }
    }

    char read_char() {
      char ch = '\0';
#ifdef _WIN32
      const int rc = recv(socket_, &ch, 1, 0);
#else
      const ssize_t rc = recv(socket_, &ch, 1, 0);
#endif
      if (rc != 1) {
        throw std::runtime_error("Redis socket read failed");
      }
      return ch;
    }

   private:
    SocketHandle socket_ = kInvalidSocket;
  };

  RedisValue command(const std::vector<std::string>& args) {
    Connection connection(config_);
    if (!config_.password.empty()) {
      connection.write_all(encode({"AUTH", config_.password}));
      check_error(read_value(connection));
    }
    if (config_.database != 0) {
      connection.write_all(encode({"SELECT", std::to_string(config_.database)}));
      check_error(read_value(connection));
    }

    connection.write_all(encode(args));
    RedisValue reply = read_value(connection);
    check_error(reply);
    return reply;
  }

  static std::string encode(const std::vector<std::string>& args) {
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (const std::string& arg : args) {
      out += "$" + std::to_string(arg.size()) + "\r\n";
      out += arg;
      out += "\r\n";
    }
    return out;
  }

  static std::string read_line(Connection& connection) {
    std::string line;
    while (true) {
      const char ch = connection.read_char();
      if (ch == '\r') {
        if (connection.read_char() != '\n') {
          throw std::runtime_error("Invalid Redis line ending");
        }
        return line;
      }
      line.push_back(ch);
    }
  }

  static RedisValue read_value(Connection& connection) {
    const char type = connection.read_char();
    if (type == '+') {
      return {RedisValue::Type::SimpleString, read_line(connection), {}};
    }
    if (type == '-') {
      return {RedisValue::Type::Error, read_line(connection), {}};
    }
    if (type == ':') {
      return {RedisValue::Type::Integer, read_line(connection), {}};
    }
    if (type == '$') {
      const int length = std::stoi(read_line(connection));
      if (length < 0) {
        return {RedisValue::Type::Nil, "", {}};
      }
      RedisValue value;
      value.type = RedisValue::Type::BulkString;
      value.text.reserve(static_cast<size_t>(length));
      for (int i = 0; i < length; ++i) {
        value.text.push_back(connection.read_char());
      }
      if (connection.read_char() != '\r' || connection.read_char() != '\n') {
        throw std::runtime_error("Invalid Redis bulk string ending");
      }
      return value;
    }
    if (type == '*') {
      const int length = std::stoi(read_line(connection));
      if (length < 0) {
        return {RedisValue::Type::Nil, "", {}};
      }
      RedisValue value;
      value.type = RedisValue::Type::Array;
      value.items.reserve(static_cast<size_t>(length));
      for (int i = 0; i < length; ++i) {
        value.items.push_back(read_value(connection));
      }
      return value;
    }
    throw std::runtime_error("Unknown Redis reply type");
  }

  static void check_error(const RedisValue& reply) {
    if (reply.type == RedisValue::Type::Error) {
      throw std::runtime_error("Redis error: " + reply.text);
    }
  }

  RedisConfig config_;
};

}  // namespace oj
