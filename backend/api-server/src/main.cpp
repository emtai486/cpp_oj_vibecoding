#include <httplib.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::atomic<unsigned long long> request_counter{1};

std::string env_or(const char* key, const std::string& fallback) {
  const char* value = std::getenv(key);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  return value;
}

int env_port_or(const char* key, int fallback) {
  const char* value = std::getenv(key);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }

  try {
    const int port = std::stoi(value);
    if (port <= 0 || port > 65535) {
      throw std::out_of_range("port");
    }
    return port;
  } catch (const std::exception&) {
    std::cerr << "Invalid " << key << " value '" << value << "', using " << fallback << "\n";
    return fallback;
  }
}

std::string json_escape(const std::string& input) {
  std::ostringstream out;
  for (const unsigned char ch : input) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (ch < 0x20) {
          out << "\\u";
          constexpr char hex[] = "0123456789abcdef";
          out << "00" << hex[(ch >> 4) & 0x0F] << hex[ch & 0x0F];
        } else {
          out << ch;
        }
    }
  }
  return out.str();
}

std::string request_id_from(const httplib::Request& req) {
  const auto header = req.get_header_value("X-Request-Id");
  if (!header.empty()) {
    return header;
  }
  return "req_" + std::to_string(request_counter.fetch_add(1));
}

void apply_common_headers(httplib::Response& res, const std::string& request_id) {
  res.set_header("Access-Control-Allow-Origin", "*");
  res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type, X-Request-Id");
  res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  res.set_header("Access-Control-Expose-Headers", "X-Request-Id");
  res.set_header("Cache-Control", "no-store");
  res.set_header("X-Request-Id", request_id);
}

std::string api_response(const std::string& code,
                         const std::string& message,
                         const std::string& data_json,
                         const std::string& request_id) {
  std::ostringstream body;
  body << "{";
  body << "\"code\":\"" << json_escape(code) << "\",";
  body << "\"message\":\"" << json_escape(message) << "\",";
  body << "\"data\":" << data_json << ",";
  body << "\"request_id\":\"" << json_escape(request_id) << "\"";
  body << "}";
  return body.str();
}

void set_json(httplib::Response& res, int status, const std::string& body) {
  res.status = status;
  res.set_content(body, "application/json; charset=utf-8");
}

std::optional<std::string> read_file(const std::string& path) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file) {
    return std::nullopt;
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string health_payload() {
  const bool database_configured = !env_or("DATABASE_URL", "").empty();
  const bool redis_configured = !env_or("REDIS_URL", "").empty();

  std::ostringstream data;
  data << "{";
  data << "\"service\":\"api-server\",";
  data << "\"version\":\"" << json_escape(API_SERVER_VERSION) << "\",";
  data << "\"status\":\"ok\",";
  data << "\"database_configured\":" << (database_configured ? "true" : "false") << ",";
  data << "\"redis_configured\":" << (redis_configured ? "true" : "false");
  data << "}";
  return data.str();
}

void handle_health(const httplib::Request& req, httplib::Response& res) {
  const auto request_id = request_id_from(req);
  apply_common_headers(res, request_id);
  set_json(res, 200, api_response("OK", "success", health_payload(), request_id));
}

void handle_not_implemented(const httplib::Request& req, httplib::Response& res) {
  const auto request_id = request_id_from(req);
  apply_common_headers(res, request_id);
  set_json(res,
           501,
           api_response("NOT_IMPLEMENTED",
                        "Endpoint will be implemented in a later phase",
                        "{}",
                        request_id));
}

}  // namespace

int main() {
  const std::string host = env_or("API_HOST", "0.0.0.0");
  const int port = env_port_or("API_PORT", 8080);
  const std::string openapi_path = env_or("OPENAPI_PATH", "backend/openapi/openapi.json");

  httplib::Server server;

  server.Options(R"(/.*)", [](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    res.status = 204;
  });

  server.Get("/health", handle_health);
  server.Get("/api/health", handle_health);

  server.Get("/api/openapi.json", [openapi_path](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);

    const auto content = read_file(openapi_path);
    if (!content.has_value()) {
      set_json(res,
               500,
               api_response("OPENAPI_NOT_FOUND",
                            "OpenAPI document is not available",
                            "{}",
                            request_id));
      return;
    }

    res.status = 200;
    res.set_content(*content, "application/json; charset=utf-8");
  });

  server.Get("/api/v1", [](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    set_json(res,
             200,
             api_response("OK",
                          "success",
                          "{\"name\":\"AI Native Online Judge API\",\"phase\":\"phase1\"}",
                          request_id));
  });

  server.Post("/api/auth/register", handle_not_implemented);
  server.Post("/api/auth/login", handle_not_implemented);
  server.Get("/api/v1/problems", handle_not_implemented);
  server.Get(R"(/api/v1/problems/[0-9]+)", handle_not_implemented);
  server.Post("/api/submissions", handle_not_implemented);
  server.Get(R"(/api/submissions/[0-9]+)", handle_not_implemented);
  server.Get("/api/me/submissions", handle_not_implemented);
  server.Post("/api/run", handle_not_implemented);
  server.Post("/api/ai/hint", handle_not_implemented);
  server.Get("/api/admin/problems", handle_not_implemented);
  server.Post("/api/admin/problems", handle_not_implemented);
  server.Put(R"(/api/admin/problems/[0-9]+)", handle_not_implemented);
  server.Post(R"(/api/admin/problems/[0-9]+/testcases)", handle_not_implemented);
  server.Get("/api/admin/submissions", handle_not_implemented);
  server.Get("/api/admin/workers", handle_not_implemented);
  server.Get("/api/admin/ai-logs", handle_not_implemented);

  server.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    set_json(res,
             res.status == 0 ? 404 : res.status,
             api_response("NOT_FOUND", "Route not found", "{}", request_id));
  });

  server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
    std::cout << req.method << " " << req.path << " " << res.status;
    const auto request_id = res.get_header_value("X-Request-Id");
    if (!request_id.empty()) {
      std::cout << " request_id=" << request_id;
    }
    std::cout << "\n";
  });

  std::cout << "api-server listening on " << host << ":" << port << "\n";
  if (!server.listen(host, port)) {
    std::cerr << "api-server failed to listen on " << host << ":" << port << "\n";
    return 1;
  }

  return 0;
}
