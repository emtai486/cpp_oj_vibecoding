#include <httplib.h>
#include "redis_client.hpp"
#include <libpq-fe.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef API_SERVER_VERSION
#define API_SERVER_VERSION "dev"
#endif

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

long long unix_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string trim(const std::string& input) {
  auto begin = input.begin();
  while (begin != input.end() && std::isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }
  auto end = input.end();
  while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }
  return std::string(begin, end);
}

std::string lower_copy(std::string input) {
  std::transform(input.begin(), input.end(), input.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return input;
}

bool one_of(const std::string& value, const std::vector<std::string>& allowed) {
  return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
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
          constexpr char hex[] = "0123456789abcdef";
          out << "\\u00" << hex[(ch >> 4) & 0x0F] << hex[ch & 0x0F];
        } else {
          out << ch;
        }
    }
  }
  return out.str();
}

std::string json_string(const std::string& input) {
  return "\"" + json_escape(input) + "\"";
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

void send_ok(httplib::Response& res, const std::string& request_id, const std::string& data_json) {
  set_json(res, 200, api_response("OK", "success", data_json, request_id));
}

void send_created(httplib::Response& res, const std::string& request_id, const std::string& data_json) {
  set_json(res, 201, api_response("OK", "success", data_json, request_id));
}

void send_error(httplib::Response& res,
                int status,
                const std::string& code,
                const std::string& message,
                const std::string& request_id) {
  set_json(res, status, api_response(code, message, "{}", request_id));
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

void write_file_checked(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error("Unable to open test data file for writing");
  }
  file << content;
  if (!file) {
    throw std::runtime_error("Unable to write test data file");
  }
}

struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool bool_value = false;
  double number_value = 0;
  std::string string_value;
  std::vector<JsonValue> array_value;
  std::map<std::string, JsonValue> object_value;

  bool is_object() const { return type == Type::Object; }
  bool is_array() const { return type == Type::Array; }
  bool is_string() const { return type == Type::String; }
  bool is_number() const { return type == Type::Number; }
  bool is_bool() const { return type == Type::Bool; }
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  JsonValue parse() {
    skip_ws();
    JsonValue value = parse_value();
    skip_ws();
    if (pos_ != input_.size()) {
      throw std::runtime_error("Unexpected trailing JSON content");
    }
    return value;
  }

 private:
  std::string_view input_;
  size_t pos_ = 0;

  void skip_ws() {
    while (pos_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[pos_]))) {
      ++pos_;
    }
  }

  char peek() const {
    if (pos_ >= input_.size()) {
      return '\0';
    }
    return input_[pos_];
  }

  char take() {
    if (pos_ >= input_.size()) {
      throw std::runtime_error("Unexpected end of JSON");
    }
    return input_[pos_++];
  }

  bool consume(std::string_view token) {
    if (input_.substr(pos_, token.size()) == token) {
      pos_ += token.size();
      return true;
    }
    return false;
  }

  JsonValue parse_value() {
    skip_ws();
    const char ch = peek();
    if (ch == '"') {
      JsonValue value;
      value.type = JsonValue::Type::String;
      value.string_value = parse_string();
      return value;
    }
    if (ch == '{') {
      return parse_object();
    }
    if (ch == '[') {
      return parse_array();
    }
    if (consume("true")) {
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.bool_value = true;
      return value;
    }
    if (consume("false")) {
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.bool_value = false;
      return value;
    }
    if (consume("null")) {
      return JsonValue{};
    }
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
      return parse_number();
    }
    throw std::runtime_error("Invalid JSON value");
  }

  static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
      return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
      return ch - 'A' + 10;
    }
    return -1;
  }

  static void append_utf8(std::string& out, unsigned int codepoint) {
    if (codepoint <= 0x7F) {
      out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
      out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
  }

  unsigned int parse_hex4() {
    unsigned int value = 0;
    for (int i = 0; i < 4; ++i) {
      const int digit = hex_value(take());
      if (digit < 0) {
        throw std::runtime_error("Invalid JSON unicode escape");
      }
      value = (value << 4) | static_cast<unsigned int>(digit);
    }
    return value;
  }

  std::string parse_string() {
    if (take() != '"') {
      throw std::runtime_error("Expected JSON string");
    }

    std::string out;
    while (true) {
      const char ch = take();
      if (ch == '"') {
        return out;
      }
      if (static_cast<unsigned char>(ch) < 0x20) {
        throw std::runtime_error("Invalid control character in JSON string");
      }
      if (ch != '\\') {
        out.push_back(ch);
        continue;
      }

      const char esc = take();
      switch (esc) {
        case '"':
        case '\\':
        case '/':
          out.push_back(esc);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u':
          append_utf8(out, parse_hex4());
          break;
        default:
          throw std::runtime_error("Invalid JSON escape");
      }
    }
  }

  JsonValue parse_number() {
    const size_t start = pos_;
    if (peek() == '-') {
      ++pos_;
    }
    if (!std::isdigit(static_cast<unsigned char>(peek()))) {
      throw std::runtime_error("Invalid JSON number");
    }
    if (peek() == '0') {
      ++pos_;
    } else {
      while (std::isdigit(static_cast<unsigned char>(peek()))) {
        ++pos_;
      }
    }
    if (peek() == '.') {
      ++pos_;
      if (!std::isdigit(static_cast<unsigned char>(peek()))) {
        throw std::runtime_error("Invalid JSON number fraction");
      }
      while (std::isdigit(static_cast<unsigned char>(peek()))) {
        ++pos_;
      }
    }
    if (peek() == 'e' || peek() == 'E') {
      ++pos_;
      if (peek() == '+' || peek() == '-') {
        ++pos_;
      }
      if (!std::isdigit(static_cast<unsigned char>(peek()))) {
        throw std::runtime_error("Invalid JSON exponent");
      }
      while (std::isdigit(static_cast<unsigned char>(peek()))) {
        ++pos_;
      }
    }

    JsonValue value;
    value.type = JsonValue::Type::Number;
    value.number_value = std::stod(std::string(input_.substr(start, pos_ - start)));
    return value;
  }

  JsonValue parse_array() {
    if (take() != '[') {
      throw std::runtime_error("Expected JSON array");
    }
    JsonValue value;
    value.type = JsonValue::Type::Array;
    skip_ws();
    if (peek() == ']') {
      take();
      return value;
    }
    while (true) {
      value.array_value.push_back(parse_value());
      skip_ws();
      const char ch = take();
      if (ch == ']') {
        return value;
      }
      if (ch != ',') {
        throw std::runtime_error("Expected comma in JSON array");
      }
    }
  }

  JsonValue parse_object() {
    if (take() != '{') {
      throw std::runtime_error("Expected JSON object");
    }
    JsonValue value;
    value.type = JsonValue::Type::Object;
    skip_ws();
    if (peek() == '}') {
      take();
      return value;
    }
    while (true) {
      skip_ws();
      const std::string key = parse_string();
      skip_ws();
      if (take() != ':') {
        throw std::runtime_error("Expected colon in JSON object");
      }
      value.object_value[key] = parse_value();
      skip_ws();
      const char ch = take();
      if (ch == '}') {
        return value;
      }
      if (ch != ',') {
        throw std::runtime_error("Expected comma in JSON object");
      }
    }
  }
};

JsonValue parse_json_body(const httplib::Request& req) {
  if (req.body.size() > 2 * 1024 * 1024) {
    throw std::runtime_error("Request body is too large");
  }
  JsonValue parsed = JsonParser(req.body).parse();
  if (!parsed.is_object()) {
    throw std::runtime_error("Request body must be a JSON object");
  }
  return parsed;
}

const JsonValue* object_get(const JsonValue& object, const std::string& key) {
  if (!object.is_object()) {
    return nullptr;
  }
  const auto it = object.object_value.find(key);
  if (it == object.object_value.end()) {
    return nullptr;
  }
  return &it->second;
}

std::string json_string_field(const JsonValue& object,
                              const std::string& key,
                              const std::string& fallback = "") {
  const JsonValue* value = object_get(object, key);
  if (value == nullptr || value->type == JsonValue::Type::Null) {
    return fallback;
  }
  if (!value->is_string()) {
    throw std::runtime_error("Field '" + key + "' must be a string");
  }
  return value->string_value;
}

int json_int_field(const JsonValue& object,
                   const std::string& key,
                   int fallback,
                   int min_value,
                   int max_value) {
  const JsonValue* value = object_get(object, key);
  if (value == nullptr || value->type == JsonValue::Type::Null) {
    return fallback;
  }
  if (!value->is_number()) {
    throw std::runtime_error("Field '" + key + "' must be a number");
  }
  const int number = static_cast<int>(value->number_value);
  if (number < min_value || number > max_value) {
    throw std::runtime_error("Field '" + key + "' is out of range");
  }
  return number;
}

bool json_bool_field(const JsonValue& object, const std::string& key, bool fallback) {
  const JsonValue* value = object_get(object, key);
  if (value == nullptr || value->type == JsonValue::Type::Null) {
    return fallback;
  }
  if (!value->is_bool()) {
    throw std::runtime_error("Field '" + key + "' must be a boolean");
  }
  return value->bool_value;
}

std::vector<std::string> json_string_array_field(const JsonValue& object,
                                                 const std::string& key,
                                                 size_t max_items = 16) {
  const JsonValue* value = object_get(object, key);
  if (value == nullptr || value->type == JsonValue::Type::Null) {
    return {};
  }
  if (!value->is_array()) {
    throw std::runtime_error("Field '" + key + "' must be an array");
  }
  if (value->array_value.size() > max_items) {
    throw std::runtime_error("Field '" + key + "' has too many items");
  }

  std::vector<std::string> items;
  for (const JsonValue& item : value->array_value) {
    if (!item.is_string()) {
      throw std::runtime_error("Field '" + key + "' must contain only strings");
    }
    const std::string tag = trim(item.string_value);
    if (!tag.empty()) {
      items.push_back(tag);
    }
  }
  return items;
}

std::string db_array_literal(const std::vector<std::string>& items) {
  std::ostringstream out;
  out << "{";
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << "\"";
    for (const char ch : items[i]) {
      if (ch == '"' || ch == '\\') {
        out << "\\";
      }
      out << ch;
    }
    out << "\"";
  }
  out << "}";
  return out.str();
}

std::string first_line_summary(const std::string& content) {
  std::string summary = trim(content.substr(0, std::min<size_t>(content.size(), 160)));
  for (char& ch : summary) {
    if (ch == '\r' || ch == '\n' || ch == '\t') {
      ch = ' ';
    }
  }
  if (content.size() > 160) {
    summary += "...";
  }
  return summary;
}

class DbException : public std::runtime_error {
 public:
  DbException(std::string message, std::string sqlstate)
      : std::runtime_error(std::move(message)), sqlstate_(std::move(sqlstate)) {}

  const std::string& sqlstate() const { return sqlstate_; }

 private:
  std::string sqlstate_;
};

class PgResult {
 public:
  explicit PgResult(PGresult* result = nullptr) : result_(result) {}
  PgResult(const PgResult&) = delete;
  PgResult& operator=(const PgResult&) = delete;

  PgResult(PgResult&& other) noexcept : result_(other.result_) {
    other.result_ = nullptr;
  }

  PgResult& operator=(PgResult&& other) noexcept {
    if (this != &other) {
      clear();
      result_ = other.result_;
      other.result_ = nullptr;
    }
    return *this;
  }

  ~PgResult() { clear(); }

  int rows() const { return result_ == nullptr ? 0 : PQntuples(result_); }

  bool is_null(int row, int column) const {
    return PQgetisnull(result_, row, column) == 1;
  }

  std::string get(int row, int column) const {
    if (is_null(row, column)) {
      return "";
    }
    return PQgetvalue(result_, row, column);
  }

 private:
  PGresult* result_ = nullptr;

  void clear() {
    if (result_ != nullptr) {
      PQclear(result_);
      result_ = nullptr;
    }
  }
};

class Database {
 public:
  explicit Database(std::string url) : url_(std::move(url)) {}

  ~Database() {
    if (conn_ != nullptr) {
      PQfinish(conn_);
    }
  }

  bool configured() const { return !url_.empty(); }

  bool healthy() {
    if (!configured()) {
      return false;
    }
    try {
      exec("SELECT 1");
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }

  PgResult exec(const std::string& sql) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_connection_locked();
    PGresult* result = PQexec(conn_, sql.c_str());
    return checked_result(result);
  }

  PgResult exec_params(const std::string& sql, const std::vector<std::string>& params = {}) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_connection_locked();

    std::vector<const char*> values;
    values.reserve(params.size());
    for (const auto& param : params) {
      values.push_back(param.c_str());
    }

    PGresult* result = PQexecParams(conn_,
                                    sql.c_str(),
                                    static_cast<int>(values.size()),
                                    nullptr,
                                    values.data(),
                                    nullptr,
                                    nullptr,
                                    0);
    return checked_result(result);
  }

 private:
  std::string url_;
  PGconn* conn_ = nullptr;
  std::mutex mutex_;

  void ensure_connection_locked() {
    if (!configured()) {
      throw DbException("DATABASE_URL is not configured", "");
    }
    if (conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK) {
      return;
    }
    if (conn_ != nullptr) {
      PQfinish(conn_);
      conn_ = nullptr;
    }
    conn_ = PQconnectdb(url_.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
      const std::string message = conn_ == nullptr ? "Unable to connect to PostgreSQL" : PQerrorMessage(conn_);
      if (conn_ != nullptr) {
        PQfinish(conn_);
        conn_ = nullptr;
      }
      throw DbException(message, "");
    }
  }

  PgResult checked_result(PGresult* result) {
    if (result == nullptr) {
      throw DbException(PQerrorMessage(conn_), "");
    }
    const ExecStatusType status = PQresultStatus(result);
    if (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK) {
      return PgResult(result);
    }

    const char* sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);
    std::string message = PQresultErrorMessage(result);
    std::string state = sqlstate == nullptr ? "" : sqlstate;
    PQclear(result);
    throw DbException(message, state);
  }
};

std::string bytes_to_hex(const unsigned char* bytes, size_t size) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (size_t i = 0; i < size; ++i) {
    out << std::setw(2) << static_cast<int>(bytes[i]);
  }
  return out.str();
}

std::vector<unsigned char> hex_to_bytes(const std::string& hex) {
  if (hex.size() % 2 != 0) {
    throw std::runtime_error("Invalid hex string");
  }
  std::vector<unsigned char> bytes;
  bytes.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    const auto part = hex.substr(i, 2);
    bytes.push_back(static_cast<unsigned char>(std::stoul(part, nullptr, 16)));
  }
  return bytes;
}

std::vector<unsigned char> random_bytes(size_t size) {
  std::vector<unsigned char> bytes(size);
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
    throw std::runtime_error("Unable to generate secure random bytes");
  }
  return bytes;
}

std::string hash_password(const std::string& password) {
  constexpr int iterations = 210000;
  const auto salt = random_bytes(16);
  unsigned char digest[32];
  if (PKCS5_PBKDF2_HMAC(password.c_str(),
                        static_cast<int>(password.size()),
                        salt.data(),
                        static_cast<int>(salt.size()),
                        iterations,
                        EVP_sha256(),
                        sizeof(digest),
                        digest) != 1) {
    throw std::runtime_error("Unable to hash password");
  }
  return "pbkdf2_sha256$" + std::to_string(iterations) + "$" +
         bytes_to_hex(salt.data(), salt.size()) + "$" + bytes_to_hex(digest, sizeof(digest));
}

bool verify_password(const std::string& password, const std::string& stored_hash) {
  std::vector<std::string> parts;
  std::stringstream stream(stored_hash);
  std::string part;
  while (std::getline(stream, part, '$')) {
    parts.push_back(part);
  }
  if (parts.size() != 4 || parts[0] != "pbkdf2_sha256") {
    return false;
  }

  try {
    const int iterations = std::stoi(parts[1]);
    const auto salt = hex_to_bytes(parts[2]);
    const auto expected = hex_to_bytes(parts[3]);
    std::vector<unsigned char> actual(expected.size());
    if (PKCS5_PBKDF2_HMAC(password.c_str(),
                          static_cast<int>(password.size()),
                          salt.data(),
                          static_cast<int>(salt.size()),
                          iterations,
                          EVP_sha256(),
                          static_cast<int>(actual.size()),
                          actual.data()) != 1) {
      return false;
    }
    return actual.size() == expected.size() &&
           CRYPTO_memcmp(actual.data(), expected.data(), expected.size()) == 0;
  } catch (const std::exception&) {
    return false;
  }
}

const std::string base64url_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url_encode(const std::string& input) {
  std::string out;
  int val = 0;
  int valb = -6;
  for (unsigned char ch : input) {
    val = (val << 8) + ch;
    valb += 8;
    while (valb >= 0) {
      out.push_back(base64url_chars[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) {
    out.push_back(base64url_chars[((val << 8) >> (valb + 8)) & 0x3F]);
  }
  return out;
}

std::optional<std::string> base64url_decode(const std::string& input) {
  std::vector<int> table(256, -1);
  for (size_t i = 0; i < base64url_chars.size(); ++i) {
    table[static_cast<unsigned char>(base64url_chars[i])] = static_cast<int>(i);
  }

  std::string out;
  int val = 0;
  int valb = -8;
  for (unsigned char ch : input) {
    if (table[ch] == -1) {
      return std::nullopt;
    }
    val = (val << 6) + table[ch];
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

std::string hmac_sha256(const std::string& secret, const std::string& message) {
  unsigned int length = 0;
  unsigned char digest[EVP_MAX_MD_SIZE];
  HMAC(EVP_sha256(),
       secret.data(),
       static_cast<int>(secret.size()),
       reinterpret_cast<const unsigned char*>(message.data()),
       message.size(),
       digest,
       &length);
  return std::string(reinterpret_cast<char*>(digest), length);
}

struct UserPrincipal {
  long long id = 0;
  std::string username;
  std::string email;
  std::string role;
};

std::string user_json(const UserPrincipal& user) {
  std::ostringstream data;
  data << "{";
  data << "\"id\":" << user.id << ",";
  data << "\"username\":" << json_string(user.username) << ",";
  data << "\"email\":" << json_string(user.email) << ",";
  data << "\"role\":" << json_string(user.role);
  data << "}";
  return data.str();
}

std::string create_jwt(const UserPrincipal& user, const std::string& secret) {
  const std::string header = R"({"alg":"HS256","typ":"JWT"})";
  std::ostringstream payload;
  payload << "{";
  payload << "\"sub\":" << json_string(std::to_string(user.id)) << ",";
  payload << "\"user_id\":" << user.id << ",";
  payload << "\"username\":" << json_string(user.username) << ",";
  payload << "\"role\":" << json_string(user.role) << ",";
  payload << "\"exp\":" << (unix_now() + 7LL * 24 * 60 * 60);
  payload << "}";

  const std::string signing_input =
      base64url_encode(header) + "." + base64url_encode(payload.str());
  return signing_input + "." + base64url_encode(hmac_sha256(secret, signing_input));
}

std::optional<long long> verify_jwt_and_get_user_id(const std::string& token,
                                                    const std::string& secret) {
  const size_t first_dot = token.find('.');
  const size_t second_dot = token.find('.', first_dot == std::string::npos ? 0 : first_dot + 1);
  if (first_dot == std::string::npos || second_dot == std::string::npos ||
      token.find('.', second_dot + 1) != std::string::npos) {
    return std::nullopt;
  }

  const std::string signing_input = token.substr(0, second_dot);
  const std::string signature_part = token.substr(second_dot + 1);
  const auto actual_signature = base64url_decode(signature_part);
  if (!actual_signature.has_value()) {
    return std::nullopt;
  }
  const std::string expected_signature = hmac_sha256(secret, signing_input);
  if (actual_signature->size() != expected_signature.size() ||
      CRYPTO_memcmp(actual_signature->data(), expected_signature.data(), expected_signature.size()) != 0) {
    return std::nullopt;
  }

  const auto payload_json = base64url_decode(token.substr(first_dot + 1, second_dot - first_dot - 1));
  if (!payload_json.has_value()) {
    return std::nullopt;
  }

  try {
    const JsonValue payload = JsonParser(*payload_json).parse();
    const JsonValue* exp = object_get(payload, "exp");
    const JsonValue* user_id = object_get(payload, "user_id");
    if (exp == nullptr || !exp->is_number() || user_id == nullptr || !user_id->is_number()) {
      return std::nullopt;
    }
    if (static_cast<long long>(exp->number_value) < unix_now()) {
      return std::nullopt;
    }
    return static_cast<long long>(user_id->number_value);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<UserPrincipal> authenticate(const httplib::Request& req,
                                          Database& db,
                                          const std::string& jwt_secret) {
  const std::string header = req.get_header_value("Authorization");
  constexpr std::string_view prefix = "Bearer ";
  if (header.size() <= prefix.size() || header.substr(0, prefix.size()) != std::string(prefix)) {
    return std::nullopt;
  }
  const auto user_id = verify_jwt_and_get_user_id(header.substr(prefix.size()), jwt_secret);
  if (!user_id.has_value()) {
    return std::nullopt;
  }

  auto result = db.exec_params(
      "SELECT u.id, u.username, u.email, r.name "
      "FROM users u JOIN roles r ON r.id = u.role_id "
      "WHERE u.id = $1 AND u.status = 'active'",
      {std::to_string(*user_id)});
  if (result.rows() != 1) {
    return std::nullopt;
  }

  UserPrincipal user;
  user.id = std::stoll(result.get(0, 0));
  user.username = result.get(0, 1);
  user.email = result.get(0, 2);
  user.role = result.get(0, 3);
  return user;
}

std::optional<UserPrincipal> require_auth(const httplib::Request& req,
                                          httplib::Response& res,
                                          const std::string& request_id,
                                          Database& db,
                                          const std::string& jwt_secret) {
  auto user = authenticate(req, db, jwt_secret);
  if (!user.has_value()) {
    send_error(res, 401, "UNAUTHORIZED", "Authentication is required", request_id);
    return std::nullopt;
  }
  return user;
}

std::optional<UserPrincipal> require_admin(const httplib::Request& req,
                                           httplib::Response& res,
                                           const std::string& request_id,
                                           Database& db,
                                           const std::string& jwt_secret) {
  auto user = require_auth(req, res, request_id, db, jwt_secret);
  if (!user.has_value()) {
    return std::nullopt;
  }
  if (user->role != "admin") {
    send_error(res, 403, "FORBIDDEN", "Administrator role is required", request_id);
    return std::nullopt;
  }
  return user;
}

bool valid_username(const std::string& username) {
  if (username.size() < 3 || username.size() > 32) {
    return false;
  }
  return std::all_of(username.begin(), username.end(), [](unsigned char ch) {
    return std::isalnum(ch) || ch == '_' || ch == '-';
  });
}

bool valid_email(const std::string& email) {
  return email.size() <= 254 && email.find('@') != std::string::npos && email.find('.') != std::string::npos;
}

int query_int(const httplib::Request& req,
              const std::string& key,
              int fallback,
              int min_value,
              int max_value) {
  if (!req.has_param(key)) {
    return fallback;
  }
  try {
    const int value = std::stoi(req.get_param_value(key));
    if (value < min_value || value > max_value) {
      return fallback;
    }
    return value;
  } catch (const std::exception&) {
    return fallback;
  }
}

std::string query_string(const httplib::Request& req, const std::string& key) {
  if (!req.has_param(key)) {
    return "";
  }
  return trim(req.get_param_value(key));
}

std::optional<long long> path_id(const httplib::Request& req) {
  if (req.matches.size() < 2) {
    return std::nullopt;
  }
  try {
    return std::stoll(req.matches[1]);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string problem_summary_json(const PgResult& result, int row) {
  std::ostringstream item;
  item << "{";
  item << "\"id\":" << result.get(row, 0) << ",";
  item << "\"title\":" << json_string(result.get(row, 1)) << ",";
  item << "\"difficulty\":" << json_string(result.get(row, 2)) << ",";
  item << "\"tags\":" << (result.get(row, 3).empty() ? "[]" : result.get(row, 3)) << ",";
  item << "\"accepted_count\":" << result.get(row, 4) << ",";
  item << "\"submission_count\":" << result.get(row, 5) << ",";
  item << "\"acceptance_rate\":" << result.get(row, 6) << ",";
  item << "\"status\":" << json_string(result.get(row, 7)) << ",";
  item << "\"created_at\":" << json_string(result.get(row, 8)) << ",";
  item << "\"updated_at\":" << json_string(result.get(row, 9));
  item << "}";
  return item.str();
}

std::string testcase_json(const PgResult& result, int row, bool include_paths, bool include_content) {
  const std::string input_path = result.get(row, 3);
  const std::string output_path = result.get(row, 4);
  const std::string kind = result.get(row, 2);
  const bool display_full = result.get(row, 7) == "t";
  const bool can_show_content = include_content && (kind == "sample" || (kind == "public" && display_full));

  std::ostringstream item;
  item << "{";
  item << "\"id\":" << result.get(row, 0) << ",";
  item << "\"problem_id\":" << result.get(row, 1) << ",";
  item << "\"kind\":" << json_string(kind) << ",";
  item << "\"input_summary\":" << json_string(result.get(row, 5)) << ",";
  item << "\"output_summary\":" << json_string(result.get(row, 6)) << ",";
  item << "\"display_full_content\":" << (display_full ? "true" : "false") << ",";
  item << "\"sort_order\":" << result.get(row, 8) << ",";
  item << "\"created_at\":" << json_string(result.get(row, 9)) << ",";
  item << "\"updated_at\":" << json_string(result.get(row, 10));
  if (include_paths) {
    item << ",\"input_path\":" << json_string(input_path);
    item << ",\"output_path\":" << json_string(output_path);
  }
  if (can_show_content) {
    item << ",\"input\":" << json_string(read_file(input_path).value_or(""));
    item << ",\"output\":" << json_string(read_file(output_path).value_or(""));
  }
  item << "}";
  return item.str();
}

std::string testcases_for_problem_json(Database& db,
                                       long long problem_id,
                                       bool admin_view,
                                       bool include_content) {
  std::string sql =
      "SELECT id, problem_id, kind, input_path, output_path, input_summary, output_summary, "
      "display_full_content, sort_order, created_at, updated_at "
      "FROM test_cases WHERE problem_id = $1";
  if (!admin_view) {
    sql += " AND kind IN ('sample', 'public')";
  }
  sql += " ORDER BY sort_order ASC, id ASC";

  auto result = db.exec_params(sql, {std::to_string(problem_id)});
  std::ostringstream array;
  array << "[";
  for (int i = 0; i < result.rows(); ++i) {
    if (i > 0) {
      array << ",";
    }
    array << testcase_json(result, i, admin_view, include_content);
  }
  array << "]";
  return array.str();
}

std::string problem_detail_json(Database& db, const PgResult& result, int row, bool admin_view) {
  const long long id = std::stoll(result.get(row, 0));
  std::ostringstream item;
  item << "{";
  item << "\"id\":" << result.get(row, 0) << ",";
  item << "\"title\":" << json_string(result.get(row, 1)) << ",";
  item << "\"description\":" << json_string(result.get(row, 2)) << ",";
  item << "\"difficulty\":" << json_string(result.get(row, 3)) << ",";
  item << "\"tags\":" << (result.get(row, 4).empty() ? "[]" : result.get(row, 4)) << ",";
  item << "\"time_limit_ms\":" << result.get(row, 5) << ",";
  item << "\"memory_limit_mb\":" << result.get(row, 6) << ",";
  item << "\"default_code_template\":" << json_string(result.get(row, 7)) << ",";
  item << "\"source\":" << json_string(result.get(row, 8)) << ",";
  item << "\"accepted_count\":" << result.get(row, 9) << ",";
  item << "\"submission_count\":" << result.get(row, 10) << ",";
  item << "\"acceptance_rate\":" << result.get(row, 11) << ",";
  item << "\"status\":" << json_string(result.get(row, 12)) << ",";
  item << "\"created_at\":" << json_string(result.get(row, 13)) << ",";
  item << "\"updated_at\":" << json_string(result.get(row, 14)) << ",";
  item << "\"test_cases\":" << testcases_for_problem_json(db, id, admin_view, true);
  item << "}";
  return item.str();
}

std::string page_json(const std::string& items_json, int page, int page_size, const std::string& total) {
  std::ostringstream data;
  data << "{";
  data << "\"items\":" << items_json << ",";
  data << "\"page\":" << page << ",";
  data << "\"page_size\":" << page_size << ",";
  data << "\"total\":" << total;
  data << "}";
  return data.str();
}

struct ProblemPayload {
  std::string title;
  std::string description;
  std::string difficulty;
  std::vector<std::string> tags;
  int time_limit_ms = 2000;
  int memory_limit_mb = 256;
  std::string default_code_template;
  std::string source;
  std::string status;
};

ProblemPayload parse_problem_payload(const JsonValue& body, bool creating) {
  ProblemPayload payload;
  payload.title = trim(json_string_field(body, "title"));
  payload.description = json_string_field(body, "description");
  payload.difficulty = lower_copy(trim(json_string_field(body, "difficulty", "easy")));
  payload.tags = json_string_array_field(body, "tags", 16);
  payload.time_limit_ms = json_int_field(body, "time_limit_ms", 2000, 100, 60000);
  payload.memory_limit_mb = json_int_field(body, "memory_limit_mb", 256, 16, 4096);
  payload.default_code_template = json_string_field(body, "default_code_template");
  payload.source = json_string_field(body, "source");
  payload.status = lower_copy(trim(json_string_field(body, "status", creating ? "draft" : "")));

  if (payload.title.empty() || payload.title.size() > 200) {
    throw std::runtime_error("Title must be 1-200 characters");
  }
  if (!one_of(payload.difficulty, {"easy", "medium", "hard"})) {
    throw std::runtime_error("Difficulty must be easy, medium, or hard");
  }
  if (!one_of(payload.status, {"draft", "published", "archived"})) {
    throw std::runtime_error("Status must be draft, published, or archived");
  }
  for (const auto& tag : payload.tags) {
    if (tag.size() > 40) {
      throw std::runtime_error("Tags must be 40 characters or fewer");
    }
  }
  return payload;
}

std::string problem_select_sql() {
  return "SELECT id, title, description, difficulty, COALESCE(array_to_json(tags)::text, '[]'), "
         "time_limit_ms, memory_limit_mb, default_code_template, source, accepted_count, "
         "submission_count, acceptance_rate, status, created_at, updated_at FROM problems";
}

std::string problem_returning_sql() {
  return "RETURNING id, title, description, difficulty, COALESCE(array_to_json(tags)::text, '[]'), "
         "time_limit_ms, memory_limit_mb, default_code_template, source, accepted_count, "
         "submission_count, acceptance_rate, status, created_at, updated_at";
}

std::string nullable_number(const PgResult& result, int row, int column) {
  return result.is_null(row, column) ? "null" : result.get(row, column);
}

std::string nullable_string(const PgResult& result, int row, int column) {
  return result.is_null(row, column) ? "null" : json_string(result.get(row, column));
}

std::string submission_case_result_json(const PgResult& result, int row) {
  std::ostringstream item;
  item << "{";
  item << "\"id\":" << result.get(row, 0) << ",";
  item << "\"submission_id\":" << result.get(row, 1) << ",";
  item << "\"test_case_id\":" << nullable_number(result, row, 2) << ",";
  item << "\"status\":" << json_string(result.get(row, 3)) << ",";
  item << "\"time_ms\":" << nullable_number(result, row, 4) << ",";
  item << "\"memory_mb\":" << nullable_number(result, row, 5) << ",";
  item << "\"error_summary\":" << json_string(result.get(row, 6)) << ",";
  item << "\"sort_order\":" << result.get(row, 7) << ",";
  item << "\"kind\":" << json_string(result.get(row, 8).empty() ? "custom" : result.get(row, 8)) << ",";
  item << "\"input_summary\":" << json_string(result.get(row, 9)) << ",";
  item << "\"output_summary\":" << json_string(result.get(row, 10)) << ",";
  item << "\"created_at\":" << json_string(result.get(row, 11));
  item << "}";
  return item.str();
}

std::string submission_case_results_json(Database& db, long long submission_id) {
  auto result = db.exec_params(
      "SELECT r.id, r.submission_id, r.test_case_id, r.status, r.time_ms, r.memory_mb, "
      "r.error_summary, r.sort_order, COALESCE(t.kind, 'custom'), COALESCE(t.input_summary, ''), "
      "COALESCE(t.output_summary, ''), r.created_at "
      "FROM submission_case_results r LEFT JOIN test_cases t ON t.id = r.test_case_id "
      "WHERE r.submission_id = $1 ORDER BY r.sort_order ASC, r.id ASC",
      {std::to_string(submission_id)});
  std::ostringstream array;
  array << "[";
  for (int i = 0; i < result.rows(); ++i) {
    if (i > 0) {
      array << ",";
    }
    array << submission_case_result_json(result, i);
  }
  array << "]";
  return array.str();
}

std::string submission_summary_json(const PgResult& result, int row) {
  std::ostringstream item;
  item << "{";
  item << "\"id\":" << result.get(row, 0) << ",";
  item << "\"user_id\":" << result.get(row, 1) << ",";
  item << "\"username\":" << json_string(result.get(row, 2)) << ",";
  item << "\"problem_id\":" << result.get(row, 3) << ",";
  item << "\"problem_title\":" << json_string(result.get(row, 4)) << ",";
  item << "\"language\":" << json_string(result.get(row, 5)) << ",";
  item << "\"status\":" << json_string(result.get(row, 6)) << ",";
  item << "\"total_time_ms\":" << nullable_number(result, row, 7) << ",";
  item << "\"peak_memory_mb\":" << nullable_number(result, row, 8) << ",";
  item << "\"created_at\":" << json_string(result.get(row, 9)) << ",";
  item << "\"finished_at\":" << nullable_string(result, row, 10) << ",";
  item << "\"judge_message\":" << nullable_string(result, row, 11) << ",";
  item << "\"testcase_passed_count\":" << result.get(row, 12) << ",";
  item << "\"testcase_total_count\":" << result.get(row, 13) << ",";
  item << "\"code_length\":" << result.get(row, 14) << ",";
  item << "\"ai_analysis_status\":" << json_string(result.get(row, 15));
  item << "}";
  return item.str();
}

std::string submission_detail_json(Database& db, const PgResult& result, int row) {
  const long long submission_id = std::stoll(result.get(row, 0));
  std::ostringstream item;
  item << "{";
  item << "\"id\":" << result.get(row, 0) << ",";
  item << "\"user_id\":" << result.get(row, 1) << ",";
  item << "\"username\":" << json_string(result.get(row, 2)) << ",";
  item << "\"problem_id\":" << result.get(row, 3) << ",";
  item << "\"problem_title\":" << json_string(result.get(row, 4)) << ",";
  item << "\"language\":" << json_string(result.get(row, 5)) << ",";
  item << "\"source_code\":" << json_string(result.get(row, 6)) << ",";
  item << "\"status\":" << json_string(result.get(row, 7)) << ",";
  item << "\"compile_error\":" << nullable_string(result, row, 8) << ",";
  item << "\"total_time_ms\":" << nullable_number(result, row, 9) << ",";
  item << "\"peak_memory_mb\":" << nullable_number(result, row, 10) << ",";
  item << "\"created_at\":" << json_string(result.get(row, 11)) << ",";
  item << "\"finished_at\":" << nullable_string(result, row, 12) << ",";
  item << "\"stdout\":" << nullable_string(result, row, 13) << ",";
  item << "\"stderr\":" << nullable_string(result, row, 14) << ",";
  item << "\"judge_message\":" << nullable_string(result, row, 15) << ",";
  item << "\"testcase_passed_count\":" << result.get(row, 16) << ",";
  item << "\"testcase_total_count\":" << result.get(row, 17) << ",";
  item << "\"ai_analysis_status\":" << json_string(result.get(row, 18)) << ",";
  item << "\"ai_analysis_id\":" << nullable_number(result, row, 19) << ",";
  item << "\"code_length\":" << result.get(row, 20) << ",";
  item << "\"case_results\":" << submission_case_results_json(db, submission_id);
  item << "}";
  return item.str();
}

std::string submission_select_sql() {
  return "SELECT s.id, s.user_id, u.username, s.problem_id, p.title, s.language, s.source_code, "
         "s.status, s.compile_error, s.total_time_ms, s.peak_memory_mb, s.created_at, s.finished_at, "
         "s.stdout, s.stderr, s.judge_message, s.testcase_passed_count, s.testcase_total_count, "
         "s.ai_analysis_status, s.ai_analysis_id, s.code_length "
         "FROM submissions s JOIN users u ON u.id = s.user_id JOIN problems p ON p.id = s.problem_id";
}

std::string submission_summary_select_sql() {
  return "SELECT s.id, s.user_id, u.username, s.problem_id, p.title, s.language, s.status, "
         "s.total_time_ms, s.peak_memory_mb, s.created_at, s.finished_at, s.judge_message, "
         "s.testcase_passed_count, s.testcase_total_count, s.code_length, s.ai_analysis_status "
         "FROM submissions s JOIN users u ON u.id = s.user_id JOIN problems p ON p.id = s.problem_id";
}

void enqueue_judge_task(oj::RedisClient& redis,
                        const std::string& queue_name,
                        const std::string& kind,
                        long long submission_id) {
  redis.lpush(queue_name, kind + ":" + std::to_string(submission_id));
}

long long create_submission_record(Database& db,
                                   long long user_id,
                                   long long problem_id,
                                   const std::string& source_code,
                                   const std::string& judge_message = "") {
  auto result = db.exec_params(
      "INSERT INTO submissions "
      "(user_id, problem_id, language, source_code, status, judge_message, code_length) "
      "VALUES ($1, $2, 'cpp17', $3, 'Pending', NULLIF($4, ''), $5) RETURNING id",
      {std::to_string(user_id),
       std::to_string(problem_id),
       source_code,
       judge_message,
       std::to_string(source_code.size())});
  return std::stoll(result.get(0, 0));
}

void fail_submission_enqueue(Database& db, long long submission_id, const std::string& message) {
  db.exec_params(
      "UPDATE submissions SET status = 'System Error', finished_at = now(), judge_message = $2 "
      "WHERE id = $1",
      {std::to_string(submission_id), message});
}

void bootstrap_admin(Database& db) {
  const std::string password = env_or("ADMIN_PASSWORD", "");
  if (password.empty() || !db.configured()) {
    return;
  }

  const std::string username = env_or("ADMIN_USERNAME", "admin");
  const std::string email = env_or("ADMIN_EMAIL", "admin@example.com");
  try {
    auto admins = db.exec(
        "SELECT count(*) FROM users u JOIN roles r ON r.id = u.role_id WHERE r.name = 'admin'");
    if (admins.rows() == 1 && std::stoll(admins.get(0, 0)) > 0) {
      return;
    }

    const std::string password_hash = hash_password(password);
    auto existing = db.exec_params(
        "SELECT id FROM users WHERE username = $1 OR email = $2 LIMIT 1",
        {username, email});
    if (existing.rows() == 1) {
      db.exec_params(
          "UPDATE users SET role_id = (SELECT id FROM roles WHERE name = 'admin'), "
          "password_hash = $2, status = 'active', updated_at = now() WHERE id = $1",
          {existing.get(0, 0), password_hash});
    } else {
      db.exec_params(
          "INSERT INTO users (username, email, password_hash, role_id) "
          "SELECT $1, $2, $3, id FROM roles WHERE name = 'admin'",
          {username, email, password_hash});
    }
    std::cout << "Admin account initialized for username=" << username << "\n";
  } catch (const std::exception& error) {
    std::cerr << "Admin bootstrap failed: " << error.what() << "\n";
  }
}

std::string health_payload(Database& db) {
  const bool database_configured = db.configured();
  const bool database_ok = db.healthy();
  const bool redis_configured = !env_or("REDIS_URL", "").empty();

  std::ostringstream data;
  data << "{";
  data << "\"service\":\"api-server\",";
  data << "\"version\":\"" << json_escape(API_SERVER_VERSION) << "\",";
  data << "\"status\":\"" << (database_configured && !database_ok ? "degraded" : "ok") << "\",";
  data << "\"database_configured\":" << (database_configured ? "true" : "false") << ",";
  data << "\"database_connected\":" << (database_ok ? "true" : "false") << ",";
  data << "\"redis_configured\":" << (redis_configured ? "true" : "false");
  data << "}";
  return data.str();
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
  const std::string jwt_secret = env_or("JWT_SECRET", "dev-secret-change-me");
  const std::string testdata_root = env_or("TESTDATA_ROOT", "testdata");
  const std::string redis_url = env_or("REDIS_URL", "");
  const std::string judge_queue = env_or("JUDGE_QUEUE", "judge:queue");

  Database db(env_or("DATABASE_URL", ""));
  std::optional<oj::RedisClient> redis;
  if (!redis_url.empty()) {
    redis.emplace(redis_url);
  }
  bootstrap_admin(db);

  httplib::Server server;
  server.set_payload_max_length(2 * 1024 * 1024);

  server.Options(R"(/.*)", [](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    res.status = 204;
  });

  server.Get("/health", [&db](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    send_ok(res, request_id, health_payload(db));
  });

  server.Get("/api/health", [&db](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    send_ok(res, request_id, health_payload(db));
  });

  server.Get("/api/openapi.json", [openapi_path](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);

    const auto content = read_file(openapi_path);
    if (!content.has_value()) {
      send_error(res, 500, "OPENAPI_NOT_FOUND", "OpenAPI document is not available", request_id);
      return;
    }

    res.status = 200;
    res.set_content(*content, "application/json; charset=utf-8");
  });

  server.Get("/api/v1", [](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    send_ok(res, request_id, "{\"name\":\"AI Native Online Judge API\",\"phase\":\"phase3\"}");
  });

  server.Post("/api/auth/register", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    try {
      const JsonValue body = parse_json_body(req);
      const std::string username = trim(json_string_field(body, "username"));
      const std::string email = lower_copy(trim(json_string_field(body, "email")));
      const std::string password = json_string_field(body, "password");
      if (!valid_username(username)) {
        send_error(res, 400, "VALIDATION_ERROR", "Username must be 3-32 letters, numbers, underscores, or hyphens", request_id);
        return;
      }
      if (!valid_email(email)) {
        send_error(res, 400, "VALIDATION_ERROR", "Email is invalid", request_id);
        return;
      }
      if (password.size() < 8 || password.size() > 128) {
        send_error(res, 400, "VALIDATION_ERROR", "Password must be 8-128 characters", request_id);
        return;
      }

      auto result = db.exec_params(
          "INSERT INTO users (username, email, password_hash, role_id) "
          "SELECT $1, $2, $3, id FROM roles WHERE name = 'user' "
          "RETURNING id, username, email, (SELECT name FROM roles WHERE name = 'user')",
          {username, email, hash_password(password)});
      UserPrincipal user;
      user.id = std::stoll(result.get(0, 0));
      user.username = result.get(0, 1);
      user.email = result.get(0, 2);
      user.role = result.get(0, 3);
      const std::string token = create_jwt(user, jwt_secret);
      send_created(res, request_id, "{\"token\":" + json_string(token) + ",\"user\":" + user_json(user) + "}");
    } catch (const DbException& error) {
      if (error.sqlstate() == "23505") {
        send_error(res, 409, "CONFLICT", "Username or email already exists", request_id);
      } else {
        send_error(res, 500, "DATABASE_ERROR", "Unable to register user", request_id);
      }
    } catch (const std::exception& error) {
      send_error(res, 400, "BAD_REQUEST", error.what(), request_id);
    }
  });

  server.Post("/api/auth/login", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    try {
      const JsonValue body = parse_json_body(req);
      std::string login = trim(json_string_field(body, "login"));
      if (login.empty()) {
        login = trim(json_string_field(body, "identifier"));
      }
      const std::string password = json_string_field(body, "password");
      if (login.empty() || password.empty()) {
        send_error(res, 400, "VALIDATION_ERROR", "Login and password are required", request_id);
        return;
      }

      auto result = db.exec_params(
          "SELECT u.id, u.username, u.email, u.password_hash, r.name, u.status "
          "FROM users u JOIN roles r ON r.id = u.role_id "
          "WHERE lower(u.username) = lower($1) OR lower(u.email) = lower($1) "
          "LIMIT 1",
          {login});
      if (result.rows() != 1 || result.get(0, 5) != "active" ||
          !verify_password(password, result.get(0, 3))) {
        send_error(res, 401, "INVALID_CREDENTIALS", "Invalid login or password", request_id);
        return;
      }

      UserPrincipal user;
      user.id = std::stoll(result.get(0, 0));
      user.username = result.get(0, 1);
      user.email = result.get(0, 2);
      user.role = result.get(0, 4);
      const std::string token = create_jwt(user, jwt_secret);
      send_ok(res, request_id, "{\"token\":" + json_string(token) + ",\"user\":" + user_json(user) + "}");
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to log in", request_id);
    } catch (const std::exception& error) {
      send_error(res, 400, "BAD_REQUEST", error.what(), request_id);
    }
  });

  server.Get("/api/me", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    auto user = require_auth(req, res, request_id, db, jwt_secret);
    if (!user.has_value()) {
      return;
    }
    send_ok(res, request_id, user_json(*user));
  });

  server.Get("/api/v1/problems", [&db](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    try {
      const int page = query_int(req, "page", 1, 1, 100000);
      const int page_size = query_int(req, "page_size", 20, 1, 100);
      const int offset = (page - 1) * page_size;
      const std::string difficulty = lower_copy(query_string(req, "difficulty"));
      const std::string tag = query_string(req, "tag");
      const std::string query = query_string(req, "q");
      if (!difficulty.empty() && !one_of(difficulty, {"easy", "medium", "hard"})) {
        send_error(res, 400, "VALIDATION_ERROR", "Difficulty must be easy, medium, or hard", request_id);
        return;
      }

      auto total = db.exec_params(
          "SELECT count(*) FROM problems "
          "WHERE status = 'published' "
          "AND ($1 = '' OR difficulty = $1) "
          "AND ($2 = '' OR $2 = ANY(tags)) "
          "AND ($3 = '' OR title ILIKE '%' || $3 || '%')",
          {difficulty, tag, query});
      auto items = db.exec_params(
          "SELECT id, title, difficulty, COALESCE(array_to_json(tags)::text, '[]'), "
          "accepted_count, submission_count, acceptance_rate, status, created_at, updated_at "
          "FROM problems "
          "WHERE status = 'published' "
          "AND ($1 = '' OR difficulty = $1) "
          "AND ($2 = '' OR $2 = ANY(tags)) "
          "AND ($3 = '' OR title ILIKE '%' || $3 || '%') "
          "ORDER BY id DESC LIMIT $4 OFFSET $5",
          {difficulty, tag, query, std::to_string(page_size), std::to_string(offset)});
      std::ostringstream array;
      array << "[";
      for (int i = 0; i < items.rows(); ++i) {
        if (i > 0) {
          array << ",";
        }
        array << problem_summary_json(items, i);
      }
      array << "]";
      send_ok(res, request_id, page_json(array.str(), page, page_size, total.get(0, 0)));
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to list problems", request_id);
    }
  });

  server.Get(R"(/api/v1/problems/([0-9]+))", [&db](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    try {
      const auto id = path_id(req);
      if (!id.has_value()) {
        send_error(res, 400, "BAD_REQUEST", "Invalid problem id", request_id);
        return;
      }
      auto result = db.exec_params(
          problem_select_sql() + " WHERE id = $1 AND status = 'published'",
          {std::to_string(*id)});
      if (result.rows() != 1) {
        send_error(res, 404, "NOT_FOUND", "Problem not found", request_id);
        return;
      }
      send_ok(res, request_id, problem_detail_json(db, result, 0, false));
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to load problem", request_id);
    }
  });

  server.Get("/api/admin/problems", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    if (!require_admin(req, res, request_id, db, jwt_secret).has_value()) {
      return;
    }
    try {
      const int page = query_int(req, "page", 1, 1, 100000);
      const int page_size = query_int(req, "page_size", 20, 1, 100);
      const int offset = (page - 1) * page_size;
      const std::string status = lower_copy(query_string(req, "status"));
      const std::string query = query_string(req, "q");
      if (!status.empty() && !one_of(status, {"draft", "published", "archived"})) {
        send_error(res, 400, "VALIDATION_ERROR", "Status must be draft, published, or archived", request_id);
        return;
      }
      auto total = db.exec_params(
          "SELECT count(*) FROM problems "
          "WHERE ($1 = '' OR status = $1) AND ($2 = '' OR title ILIKE '%' || $2 || '%')",
          {status, query});
      auto items = db.exec_params(
          "SELECT id, title, difficulty, COALESCE(array_to_json(tags)::text, '[]'), "
          "accepted_count, submission_count, acceptance_rate, status, created_at, updated_at "
          "FROM problems "
          "WHERE ($1 = '' OR status = $1) AND ($2 = '' OR title ILIKE '%' || $2 || '%') "
          "ORDER BY id DESC LIMIT $3 OFFSET $4",
          {status, query, std::to_string(page_size), std::to_string(offset)});
      std::ostringstream array;
      array << "[";
      for (int i = 0; i < items.rows(); ++i) {
        if (i > 0) {
          array << ",";
        }
        array << problem_summary_json(items, i);
      }
      array << "]";
      send_ok(res, request_id, page_json(array.str(), page, page_size, total.get(0, 0)));
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to list admin problems", request_id);
    }
  });

  server.Get(R"(/api/admin/problems/([0-9]+))", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    if (!require_admin(req, res, request_id, db, jwt_secret).has_value()) {
      return;
    }
    try {
      const auto id = path_id(req);
      auto result = db.exec_params(problem_select_sql() + " WHERE id = $1", {std::to_string(*id)});
      if (result.rows() != 1) {
        send_error(res, 404, "NOT_FOUND", "Problem not found", request_id);
        return;
      }
      send_ok(res, request_id, problem_detail_json(db, result, 0, true));
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to load admin problem", request_id);
    }
  });

  server.Post("/api/admin/problems", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    if (!require_admin(req, res, request_id, db, jwt_secret).has_value()) {
      return;
    }
    try {
      const ProblemPayload payload = parse_problem_payload(parse_json_body(req), true);
      auto result = db.exec_params(
          "INSERT INTO problems "
          "(title, description, difficulty, tags, time_limit_ms, memory_limit_mb, "
          "default_code_template, source, status) "
          "VALUES ($1, $2, $3, $4::text[], $5, $6, $7, $8, $9) " +
              problem_returning_sql(),
          {payload.title,
           payload.description,
           payload.difficulty,
           db_array_literal(payload.tags),
           std::to_string(payload.time_limit_ms),
           std::to_string(payload.memory_limit_mb),
           payload.default_code_template,
           payload.source,
           payload.status});
      send_created(res, request_id, problem_detail_json(db, result, 0, true));
    } catch (const DbException& error) {
      std::cerr << "Create problem failed: " << error.what() << "\n";
      send_error(res, 500, "DATABASE_ERROR", "Unable to create problem", request_id);
    } catch (const std::exception& error) {
      send_error(res, 400, "BAD_REQUEST", error.what(), request_id);
    }
  });

  server.Put(R"(/api/admin/problems/([0-9]+))", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    if (!require_admin(req, res, request_id, db, jwt_secret).has_value()) {
      return;
    }
    try {
      const auto id = path_id(req);
      if (!id.has_value()) {
        send_error(res, 400, "BAD_REQUEST", "Invalid problem id", request_id);
        return;
      }
      const ProblemPayload payload = parse_problem_payload(parse_json_body(req), false);
      auto result = db.exec_params(
          "UPDATE problems SET title = $2, description = $3, difficulty = $4, tags = $5::text[], "
          "time_limit_ms = $6, memory_limit_mb = $7, default_code_template = $8, "
          "source = $9, status = $10, updated_at = now() WHERE id = $1 " +
              problem_returning_sql(),
          {std::to_string(*id),
           payload.title,
           payload.description,
           payload.difficulty,
           db_array_literal(payload.tags),
           std::to_string(payload.time_limit_ms),
           std::to_string(payload.memory_limit_mb),
           payload.default_code_template,
           payload.source,
           payload.status});
      if (result.rows() != 1) {
        send_error(res, 404, "NOT_FOUND", "Problem not found", request_id);
        return;
      }
      send_ok(res, request_id, problem_detail_json(db, result, 0, true));
    } catch (const DbException& error) {
      std::cerr << "Update problem failed: " << error.what() << "\n";
      send_error(res, 500, "DATABASE_ERROR", "Unable to update problem", request_id);
    } catch (const std::exception& error) {
      send_error(res, 400, "BAD_REQUEST", error.what(), request_id);
    }
  });

  server.Get(R"(/api/admin/problems/([0-9]+)/testcases)", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    if (!require_admin(req, res, request_id, db, jwt_secret).has_value()) {
      return;
    }
    try {
      const auto id = path_id(req);
      if (!id.has_value()) {
        send_error(res, 400, "BAD_REQUEST", "Invalid problem id", request_id);
        return;
      }
      send_ok(res, request_id, "{\"items\":" + testcases_for_problem_json(db, *id, true, false) + "}");
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to list test cases", request_id);
    }
  });

  server.Post(R"(/api/admin/problems/([0-9]+)/testcases)", [&db, &jwt_secret, &testdata_root](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    if (!require_admin(req, res, request_id, db, jwt_secret).has_value()) {
      return;
    }
    try {
      const auto problem_id = path_id(req);
      if (!problem_id.has_value()) {
        send_error(res, 400, "BAD_REQUEST", "Invalid problem id", request_id);
        return;
      }
      auto problem = db.exec_params("SELECT id FROM problems WHERE id = $1", {std::to_string(*problem_id)});
      if (problem.rows() != 1) {
        send_error(res, 404, "NOT_FOUND", "Problem not found", request_id);
        return;
      }

      const JsonValue body = parse_json_body(req);
      const std::string kind = lower_copy(trim(json_string_field(body, "kind", "sample")));
      if (!one_of(kind, {"sample", "public", "hidden"})) {
        send_error(res, 400, "VALIDATION_ERROR", "Test case kind must be sample, public, or hidden", request_id);
        return;
      }
      const std::string input = json_string_field(body, "input");
      const std::string output = json_string_field(body, "output");
      if (input.size() > 1024 * 1024 || output.size() > 1024 * 1024) {
        send_error(res, 400, "VALIDATION_ERROR", "Test data content must be at most 1MB per file", request_id);
        return;
      }
      const bool display_full = json_bool_field(body, "display_full_content", kind == "sample");
      const int sort_order = json_int_field(body, "sort_order", 0, 0, 1000000);
      const std::string input_summary =
          trim(json_string_field(body, "input_summary", kind == "hidden" ? "Hidden test input" : first_line_summary(input)));
      const std::string output_summary =
          trim(json_string_field(body, "output_summary", kind == "hidden" ? "Hidden expected output" : first_line_summary(output)));

      auto inserted = db.exec_params(
          "INSERT INTO test_cases "
          "(problem_id, kind, input_path, output_path, input_summary, output_summary, display_full_content, sort_order) "
          "VALUES ($1, $2, '', '', $3, $4, $5, $6) RETURNING id",
          {std::to_string(*problem_id),
           kind,
           input_summary,
           output_summary,
           display_full ? "true" : "false",
           std::to_string(sort_order)});
      const std::string testcase_id = inserted.get(0, 0);
      const std::filesystem::path root(testdata_root);
      const std::string kind_dir = kind == "sample" ? "samples" : kind;
      const std::filesystem::path dir = root / "problems" / std::to_string(*problem_id) / kind_dir;
      const std::filesystem::path input_path = dir / ("case_" + testcase_id + ".in");
      const std::filesystem::path output_path = dir / ("case_" + testcase_id + ".out");
      try {
        write_file_checked(input_path, input);
        write_file_checked(output_path, output);
      } catch (...) {
        db.exec_params("DELETE FROM test_cases WHERE id = $1", {testcase_id});
        throw;
      }

      auto result = db.exec_params(
          "UPDATE test_cases SET input_path = $2, output_path = $3, updated_at = now() "
          "WHERE id = $1 RETURNING id, problem_id, kind, input_path, output_path, input_summary, "
          "output_summary, display_full_content, sort_order, created_at, updated_at",
          {testcase_id, input_path.generic_string(), output_path.generic_string()});
      send_created(res, request_id, testcase_json(result, 0, true, false));
    } catch (const DbException& error) {
      std::cerr << "Create test case failed: " << error.what() << "\n";
      send_error(res, 500, "DATABASE_ERROR", "Unable to create test case", request_id);
    } catch (const std::exception& error) {
      send_error(res, 400, "BAD_REQUEST", error.what(), request_id);
    }
  });

  server.Get("/api/admin/tags", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    if (!require_admin(req, res, request_id, db, jwt_secret).has_value()) {
      return;
    }
    try {
      auto result = db.exec(
          "SELECT t.id, t.name, t.created_at, count(p.id) "
          "FROM problem_tags t LEFT JOIN problems p ON t.name = ANY(p.tags) "
          "GROUP BY t.id, t.name, t.created_at ORDER BY t.name ASC");
      std::ostringstream items;
      items << "[";
      for (int i = 0; i < result.rows(); ++i) {
        if (i > 0) {
          items << ",";
        }
        items << "{";
        items << "\"id\":" << result.get(i, 0) << ",";
        items << "\"name\":" << json_string(result.get(i, 1)) << ",";
        items << "\"created_at\":" << json_string(result.get(i, 2)) << ",";
        items << "\"problem_count\":" << result.get(i, 3);
        items << "}";
      }
      items << "]";
      send_ok(res, request_id, "{\"items\":" + items.str() + "}");
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to list tags", request_id);
    }
  });

  server.Post("/api/admin/tags", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    if (!require_admin(req, res, request_id, db, jwt_secret).has_value()) {
      return;
    }
    try {
      const std::string name = trim(json_string_field(parse_json_body(req), "name"));
      if (name.empty() || name.size() > 40) {
        send_error(res, 400, "VALIDATION_ERROR", "Tag name must be 1-40 characters", request_id);
        return;
      }
      auto result = db.exec_params(
          "INSERT INTO problem_tags (name) VALUES ($1) "
          "ON CONFLICT (name) DO UPDATE SET name = EXCLUDED.name "
          "RETURNING id, name, created_at",
          {name});
      std::ostringstream data;
      data << "{\"id\":" << result.get(0, 0) << ",\"name\":" << json_string(result.get(0, 1))
           << ",\"created_at\":" << json_string(result.get(0, 2)) << "}";
      send_created(res, request_id, data.str());
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to save tag", request_id);
    } catch (const std::exception& error) {
      send_error(res, 400, "BAD_REQUEST", error.what(), request_id);
    }
  });

  server.Get("/api/admin/difficulties", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    if (!require_admin(req, res, request_id, db, jwt_secret).has_value()) {
      return;
    }
    try {
      auto result = db.exec("SELECT difficulty, count(*) FROM problems GROUP BY difficulty");
      std::map<std::string, std::string> counts{{"easy", "0"}, {"medium", "0"}, {"hard", "0"}};
      for (int i = 0; i < result.rows(); ++i) {
        counts[result.get(i, 0)] = result.get(i, 1);
      }
      std::ostringstream data;
      data << "{\"items\":[";
      int index = 0;
      for (const auto& name : {"easy", "medium", "hard"}) {
        if (index++ > 0) {
          data << ",";
        }
        data << "{\"name\":" << json_string(name) << ",\"problem_count\":" << counts[name] << "}";
      }
      data << "]}";
      send_ok(res, request_id, data.str());
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to list difficulties", request_id);
    }
  });

  server.Post("/api/submissions", [&db, &jwt_secret, &redis, &judge_queue](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    const auto user = require_auth(req, res, request_id, db, jwt_secret);
    if (!user.has_value()) {
      return;
    }
    try {
      const JsonValue body = parse_json_body(req);
      const long long problem_id = json_int_field(body, "problem_id", 0, 1, 2147483647);
      const std::string language = lower_copy(trim(json_string_field(body, "language", "cpp17")));
      const std::string source_code = json_string_field(body, "source_code");
      if (language != "cpp17") {
        send_error(res, 400, "VALIDATION_ERROR", "Only cpp17 is supported in V1", request_id);
        return;
      }
      if (source_code.empty() || source_code.size() > 256 * 1024) {
        send_error(res, 400, "VALIDATION_ERROR", "Source code must be 1-262144 bytes", request_id);
        return;
      }

      auto problem = db.exec_params(
          "SELECT id FROM problems WHERE id = $1 AND status = 'published'",
          {std::to_string(problem_id)});
      if (problem.rows() != 1) {
        send_error(res, 404, "NOT_FOUND", "Problem not found", request_id);
        return;
      }

      const long long submission_id = create_submission_record(db, user->id, problem_id, source_code);
      try {
        if (!redis.has_value()) {
          throw std::runtime_error("REDIS_URL is not configured");
        }
        enqueue_judge_task(*redis, judge_queue, "submission", submission_id);
      } catch (const std::exception& error) {
        fail_submission_enqueue(db, submission_id, std::string("Unable to enqueue judge task: ") + error.what());
        send_error(res, 503, "QUEUE_UNAVAILABLE", "Judge queue is unavailable", request_id);
        return;
      }

      auto result = db.exec_params(submission_select_sql() + " WHERE s.id = $1", {std::to_string(submission_id)});
      send_created(res, request_id, submission_detail_json(db, result, 0));
    } catch (const DbException& error) {
      std::cerr << "Create submission failed: " << error.what() << "\n";
      send_error(res, 500, "DATABASE_ERROR", "Unable to create submission", request_id);
    } catch (const std::exception& error) {
      send_error(res, 400, "BAD_REQUEST", error.what(), request_id);
    }
  });

  server.Get(R"(/api/submissions/([0-9]+))", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    const auto user = require_auth(req, res, request_id, db, jwt_secret);
    if (!user.has_value()) {
      return;
    }
    try {
      const auto submission_id = path_id(req);
      if (!submission_id.has_value()) {
        send_error(res, 400, "BAD_REQUEST", "Invalid submission id", request_id);
        return;
      }
      auto result = db.exec_params(submission_select_sql() + " WHERE s.id = $1", {std::to_string(*submission_id)});
      if (result.rows() != 1) {
        send_error(res, 404, "NOT_FOUND", "Submission not found", request_id);
        return;
      }
      if (user->role != "admin" && std::stoll(result.get(0, 1)) != user->id) {
        send_error(res, 403, "FORBIDDEN", "Cannot view another user's submission", request_id);
        return;
      }
      send_ok(res, request_id, submission_detail_json(db, result, 0));
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to load submission", request_id);
    }
  });

  server.Get("/api/me/submissions", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    const auto user = require_auth(req, res, request_id, db, jwt_secret);
    if (!user.has_value()) {
      return;
    }
    try {
      const int page = query_int(req, "page", 1, 1, 100000);
      const int page_size = query_int(req, "page_size", 20, 1, 100);
      const int offset = (page - 1) * page_size;
      auto total = db.exec_params("SELECT count(*) FROM submissions WHERE user_id = $1", {std::to_string(user->id)});
      auto result = db.exec_params(
          submission_summary_select_sql() +
              " WHERE s.user_id = $1 ORDER BY s.created_at DESC LIMIT $2 OFFSET $3",
          {std::to_string(user->id), std::to_string(page_size), std::to_string(offset)});
      std::ostringstream items;
      items << "[";
      for (int i = 0; i < result.rows(); ++i) {
        if (i > 0) {
          items << ",";
        }
        items << submission_summary_json(result, i);
      }
      items << "]";
      send_ok(res, request_id, page_json(items.str(), page, page_size, total.get(0, 0)));
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to list submissions", request_id);
    }
  });

  server.Post("/api/run", [&db, &jwt_secret, &redis, &judge_queue, &testdata_root](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    const auto user = require_auth(req, res, request_id, db, jwt_secret);
    if (!user.has_value()) {
      return;
    }
    try {
      const JsonValue body = parse_json_body(req);
      const long long problem_id = json_int_field(body, "problem_id", 0, 1, 2147483647);
      const std::string source_code = json_string_field(body, "source_code");
      const std::string mode = lower_copy(trim(json_string_field(body, "mode", "samples")));
      const std::string custom_input = json_string_field(body, "custom_input", "");
      if (!one_of(mode, {"samples", "custom"})) {
        send_error(res, 400, "VALIDATION_ERROR", "Run mode must be samples or custom", request_id);
        return;
      }
      if (source_code.empty() || source_code.size() > 256 * 1024) {
        send_error(res, 400, "VALIDATION_ERROR", "Source code must be 1-262144 bytes", request_id);
        return;
      }
      if (custom_input.size() > 1024 * 1024) {
        send_error(res, 400, "VALIDATION_ERROR", "Custom input must be at most 1MB", request_id);
        return;
      }

      auto problem = db.exec_params(
          "SELECT id FROM problems WHERE id = $1 AND status = 'published'",
          {std::to_string(problem_id)});
      if (problem.rows() != 1) {
        send_error(res, 404, "NOT_FOUND", "Problem not found", request_id);
        return;
      }

      const long long submission_id =
          create_submission_record(db, user->id, problem_id, source_code, mode == "custom" ? "Custom run queued" : "Sample run queued");
      if (mode == "custom") {
        const std::filesystem::path input_path =
            std::filesystem::path(testdata_root) / "runs" / ("submission_" + std::to_string(submission_id) + ".in");
        write_file_checked(input_path, custom_input);
      }

      try {
        if (!redis.has_value()) {
          throw std::runtime_error("REDIS_URL is not configured");
        }
        enqueue_judge_task(*redis, judge_queue, mode == "custom" ? "run_custom" : "run_samples", submission_id);
      } catch (const std::exception& error) {
        fail_submission_enqueue(db, submission_id, std::string("Unable to enqueue run task: ") + error.what());
        send_error(res, 503, "QUEUE_UNAVAILABLE", "Judge queue is unavailable", request_id);
        return;
      }

      auto result = db.exec_params(submission_select_sql() + " WHERE s.id = $1", {std::to_string(submission_id)});
      send_created(res, request_id, submission_detail_json(db, result, 0));
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to create run task", request_id);
    } catch (const std::exception& error) {
      send_error(res, 400, "BAD_REQUEST", error.what(), request_id);
    }
  });
  server.Post("/api/ai/hint", handle_not_implemented);
  server.Get("/api/admin/submissions", [&db, &jwt_secret](const httplib::Request& req, httplib::Response& res) {
    const auto request_id = request_id_from(req);
    apply_common_headers(res, request_id);
    if (!require_admin(req, res, request_id, db, jwt_secret).has_value()) {
      return;
    }
    try {
      const int page = query_int(req, "page", 1, 1, 100000);
      const int page_size = query_int(req, "page_size", 20, 1, 100);
      const int offset = (page - 1) * page_size;
      const std::string status = query_string(req, "status");
      auto total = db.exec_params(
          "SELECT count(*) FROM submissions WHERE ($1 = '' OR status = $1)",
          {status});
      auto result = db.exec_params(
          submission_summary_select_sql() +
              " WHERE ($1 = '' OR s.status = $1) ORDER BY s.created_at DESC LIMIT $2 OFFSET $3",
          {status, std::to_string(page_size), std::to_string(offset)});
      std::ostringstream items;
      items << "[";
      for (int i = 0; i < result.rows(); ++i) {
        if (i > 0) {
          items << ",";
        }
        items << submission_summary_json(result, i);
      }
      items << "]";
      send_ok(res, request_id, page_json(items.str(), page, page_size, total.get(0, 0)));
    } catch (const DbException&) {
      send_error(res, 500, "DATABASE_ERROR", "Unable to list admin submissions", request_id);
    }
  });
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
