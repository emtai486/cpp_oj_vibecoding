#include "redis_client.hpp"

#include <libpq-fe.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/wait.h>
#endif

namespace {

namespace fs = std::filesystem;

std::string env_or(const char* key, const std::string& fallback) {
  const char* value = std::getenv(key);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  return value;
}

int env_int_or(const char* key, int fallback, int min_value, int max_value) {
  const char* value = std::getenv(key);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  try {
    const int parsed = std::stoi(value);
    if (parsed < min_value || parsed > max_value) {
      return fallback;
    }
    return parsed;
  } catch (const std::exception&) {
    return fallback;
  }
}

std::string trim_copy(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  return value.substr(begin);
}

std::string read_file(const fs::path& path) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file) {
    throw std::runtime_error("Unable to read file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string read_file_if_exists(const fs::path& path) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file) {
    return "";
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void write_file(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error("Unable to write file: " + path.string());
  }
  file << content;
  if (!file) {
    throw std::runtime_error("Unable to finish writing file: " + path.string());
  }
}

std::string truncate_text(const std::string& value, size_t limit) {
  if (value.size() <= limit) {
    return value;
  }
  return value.substr(0, limit) + "\n[truncated]";
}

std::string normalize_output(std::string value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\r') {
      if (i + 1 < value.size() && value[i + 1] == '\n') {
        continue;
      }
      normalized.push_back('\n');
    } else {
      normalized.push_back(value[i]);
    }
  }
  while (!normalized.empty() &&
         std::isspace(static_cast<unsigned char>(normalized.back()))) {
    normalized.pop_back();
  }
  return normalized;
}

std::string shell_quote(const std::string& value) {
  std::string out = "'";
  for (char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out.push_back(ch);
    }
  }
  out += "'";
  return out;
}

int run_shell(const std::string& command) {
  const int rc = std::system(command.c_str());
  if (rc == -1) {
    return -1;
  }
#ifdef _WIN32
  return rc;
#else
  if (WIFEXITED(rc)) {
    return WEXITSTATUS(rc);
  }
  if (WIFSIGNALED(rc)) {
    return 128 + WTERMSIG(rc);
  }
  return rc;
#endif
}

void make_world_writable(const fs::path& path) {
#ifndef _WIN32
  chmod(path.string().c_str(), 0777);
#else
  (void)path;
#endif
}

void make_world_readable(const fs::path& path) {
#ifndef _WIN32
  chmod(path.string().c_str(), 0644);
#else
  (void)path;
#endif
}

void make_world_executable(const fs::path& path) {
#ifndef _WIN32
  chmod(path.string().c_str(), 0755);
#else
  (void)path;
#endif
}

class DbException : public std::runtime_error {
 public:
  explicit DbException(std::string message) : std::runtime_error(std::move(message)) {}
};

class PgResult {
 public:
  explicit PgResult(PGresult* result = nullptr) : result_(result) {}
  PgResult(const PgResult&) = delete;
  PgResult& operator=(const PgResult&) = delete;

  PgResult(PgResult&& other) noexcept : result_(other.result_) { other.result_ = nullptr; }

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

  PgResult exec(const std::string& sql) {
    ensure_connection();
    PGresult* result = PQexec(conn_, sql.c_str());
    return checked_result(result);
  }

  PgResult exec_params(const std::string& sql, const std::vector<std::string>& params = {}) {
    ensure_connection();
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

  void ensure_connection() {
    if (url_.empty()) {
      throw DbException("DATABASE_URL is not configured");
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
      throw DbException(message);
    }
  }

  PgResult checked_result(PGresult* result) {
    if (result == nullptr) {
      throw DbException(PQerrorMessage(conn_));
    }
    const ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
      const std::string message = PQresultErrorMessage(result);
      PQclear(result);
      throw DbException(message);
    }
    return PgResult(result);
  }
};

struct WorkerConfig {
  std::string database_url;
  std::string redis_url;
  std::string queue_name = "judge:queue";
  std::string docker_bin = "docker";
  std::string sandbox_image = "gcc:13-bookworm";
  std::string testdata_root = "testdata";
  std::string workspace_root = "/tmp/oj-workspaces";
  std::string workspace_host_root = "/tmp/oj-workspaces";
  int compile_memory_mb = 512;
  int compile_timeout_seconds = 30;
  int output_limit_bytes = 1024 * 1024;
  bool keep_workspace = false;
};

WorkerConfig load_config() {
  WorkerConfig config;
  config.database_url = env_or("DATABASE_URL", "");
  config.redis_url = env_or("REDIS_URL", "");
  config.queue_name = env_or("JUDGE_QUEUE", config.queue_name);
  config.docker_bin = env_or("DOCKER_BIN", config.docker_bin);
  config.sandbox_image = env_or("JUDGE_SANDBOX_IMAGE", config.sandbox_image);
  config.testdata_root = env_or("TESTDATA_ROOT", config.testdata_root);
  config.workspace_root = env_or("JUDGE_WORKSPACE_ROOT", config.workspace_root);
  config.workspace_host_root = env_or("JUDGE_WORKSPACE_HOST_ROOT", config.workspace_root);
  config.compile_memory_mb = env_int_or("JUDGE_COMPILE_MEMORY_MB", config.compile_memory_mb, 128, 4096);
  config.compile_timeout_seconds = env_int_or("JUDGE_COMPILE_TIMEOUT_SECONDS", config.compile_timeout_seconds, 5, 300);
  config.output_limit_bytes = env_int_or("JUDGE_OUTPUT_LIMIT_BYTES", config.output_limit_bytes, 4096, 16 * 1024 * 1024);
  config.keep_workspace = env_or("JUDGE_KEEP_WORKSPACE", "") == "1";
  return config;
}

enum class TaskMode { Submit, RunSamples, RunCustom };

struct JudgeTask {
  TaskMode mode = TaskMode::Submit;
  long long submission_id = 0;
};

JudgeTask parse_task(const std::string& raw) {
  const auto colon = raw.find(':');
  if (colon == std::string::npos) {
    throw std::runtime_error("Invalid judge task payload: " + raw);
  }
  const std::string kind = raw.substr(0, colon);
  JudgeTask task;
  task.submission_id = std::stoll(raw.substr(colon + 1));
  if (kind == "submission") {
    task.mode = TaskMode::Submit;
  } else if (kind == "run_samples") {
    task.mode = TaskMode::RunSamples;
  } else if (kind == "run_custom") {
    task.mode = TaskMode::RunCustom;
  } else {
    throw std::runtime_error("Unknown judge task kind: " + kind);
  }
  return task;
}

std::string task_mode_name(TaskMode mode) {
  switch (mode) {
    case TaskMode::Submit:
      return "submission";
    case TaskMode::RunSamples:
      return "run_samples";
    case TaskMode::RunCustom:
      return "run_custom";
  }
  return "submission";
}

struct SubmissionData {
  long long id = 0;
  long long user_id = 0;
  long long problem_id = 0;
  std::string source_code;
  int time_limit_ms = 2000;
  int memory_limit_mb = 256;
};

struct TestCaseData {
  std::optional<long long> id;
  std::string kind;
  std::string input_path;
  std::string output_path;
  std::string input_summary;
  std::string output_summary;
  bool display_full_content = false;
  int sort_order = 0;
  std::string input;
  std::string expected_output;
  bool compare_output = false;
};

SubmissionData load_submission(Database& db, long long submission_id) {
  auto result = db.exec_params(
      "SELECT s.id, s.user_id, s.problem_id, s.source_code, p.time_limit_ms, p.memory_limit_mb "
      "FROM submissions s JOIN problems p ON p.id = s.problem_id WHERE s.id = $1",
      {std::to_string(submission_id)});
  if (result.rows() != 1) {
    throw std::runtime_error("Submission not found: " + std::to_string(submission_id));
  }

  SubmissionData submission;
  submission.id = std::stoll(result.get(0, 0));
  submission.user_id = std::stoll(result.get(0, 1));
  submission.problem_id = std::stoll(result.get(0, 2));
  submission.source_code = result.get(0, 3);
  submission.time_limit_ms = std::stoi(result.get(0, 4));
  submission.memory_limit_mb = std::stoi(result.get(0, 5));
  return submission;
}

std::vector<TestCaseData> load_test_cases(Database& db, const WorkerConfig& config, const SubmissionData& submission, TaskMode mode) {
  std::vector<TestCaseData> cases;
  if (mode == TaskMode::RunCustom) {
    const fs::path input_path =
        fs::path(config.testdata_root) / "runs" / ("submission_" + std::to_string(submission.id) + ".in");
    TestCaseData test_case;
    test_case.kind = "custom";
    test_case.input_path = input_path.generic_string();
    test_case.input = read_file(input_path);
    test_case.output_summary = "Custom run has no expected output";
    cases.push_back(std::move(test_case));
    return cases;
  }

  std::string sql =
      "SELECT id, kind, input_path, output_path, input_summary, output_summary, "
      "display_full_content, sort_order FROM test_cases WHERE problem_id = $1";
  if (mode == TaskMode::RunSamples) {
    sql += " AND kind = 'sample'";
  }
  sql += " ORDER BY sort_order ASC, id ASC";

  auto result = db.exec_params(sql, {std::to_string(submission.problem_id)});
  for (int i = 0; i < result.rows(); ++i) {
    TestCaseData test_case;
    test_case.id = std::stoll(result.get(i, 0));
    test_case.kind = result.get(i, 1);
    test_case.input_path = result.get(i, 2);
    test_case.output_path = result.get(i, 3);
    test_case.input_summary = result.get(i, 4);
    test_case.output_summary = result.get(i, 5);
    test_case.display_full_content = result.get(i, 6) == "t";
    test_case.sort_order = std::stoi(result.get(i, 7));
    test_case.input = read_file(test_case.input_path);
    test_case.expected_output = read_file(test_case.output_path);
    test_case.compare_output = true;
    cases.push_back(std::move(test_case));
  }
  return cases;
}

struct Workspace {
  fs::path worker_path;
  std::string host_path;
};

Workspace create_workspace(const WorkerConfig& config, long long submission_id) {
  const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  const std::string name = "submission_" + std::to_string(submission_id) + "_" + std::to_string(ticks);
  const fs::path worker_path = fs::path(config.workspace_root) / name;
  fs::create_directories(worker_path);
  make_world_writable(worker_path);

  fs::path relative = fs::relative(worker_path, fs::path(config.workspace_root));
  const fs::path host_path = fs::path(config.workspace_host_root) / relative;
  return {worker_path, host_path.generic_string()};
}

struct CompileResult {
  bool ok = false;
  bool system_error = false;
  std::string message;
};

CompileResult compile_submission(const WorkerConfig& config, const Workspace& workspace) {
  const std::string script =
      "timeout " + std::to_string(config.compile_timeout_seconds) +
      "s g++ -std=c++17 -O2 -pipe main.cpp -o main > compile_stdout.txt 2> compile_stderr.txt";

  std::ostringstream command;
  command << config.docker_bin
          << " run --rm --network none"
          << " --memory " << config.compile_memory_mb << "m"
          << " --cpus 1 --pids-limit 128"
          << " -v " << shell_quote(workspace.host_path + ":/workspace:rw")
          << " -w /workspace"
          << " " << shell_quote(config.sandbox_image)
          << " sh -lc " << shell_quote(script)
          << " > " << shell_quote((workspace.worker_path / "docker_compile_stdout.txt").string())
          << " 2> " << shell_quote((workspace.worker_path / "docker_compile_stderr.txt").string());

  const int rc = run_shell(command.str());
  const std::string compiler_stderr = read_file_if_exists(workspace.worker_path / "compile_stderr.txt");
  const std::string docker_stderr = read_file_if_exists(workspace.worker_path / "docker_compile_stderr.txt");

  CompileResult result;
  if (rc == 0) {
    result.ok = true;
    return result;
  }
  if (compiler_stderr.empty()) {
    result.system_error = true;
    result.message = docker_stderr.empty() ? "Docker compile command failed" : truncate_text(docker_stderr, 4096);
  } else {
    result.message = truncate_text(compiler_stderr, 12000);
  }
  return result;
}

std::string timeout_arg(int time_limit_ms) {
  const double seconds = std::max(0.1, static_cast<double>(time_limit_ms) / 1000.0 + 0.2);
  std::ostringstream out;
  out << std::fixed << std::setprecision(3) << seconds << "s";
  return out.str();
}

std::optional<int> parse_memory_mb(const fs::path& path) {
  const std::string content = read_file_if_exists(path);
  const std::string prefix = "MAX_RSS_KB=";
  const auto pos = content.find(prefix);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  const std::string tail = trim_copy(content.substr(pos + prefix.size()));
  try {
    const int kb = std::stoi(tail);
    return (kb + 1023) / 1024;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

struct CaseOutcome {
  std::string status = "System Error";
  int time_ms = 0;
  int memory_mb = 0;
  std::string error_summary;
  std::string stdout_data;
  std::string stderr_data;
};

bool expected_matches(const std::string& actual, const std::string& expected) {
  return normalize_output(actual) == normalize_output(expected);
}

std::string wrong_answer_summary(const TestCaseData& test_case,
                                 const std::string& actual,
                                 const std::string& expected) {
  if (test_case.kind == "hidden") {
    return "Wrong answer on a hidden test case";
  }
  if (test_case.kind == "public" && !test_case.display_full_content) {
    return "Wrong answer on public test case: " + truncate_text(test_case.output_summary, 200);
  }
  return "Expected: " + truncate_text(normalize_output(expected), 200) +
         "\nActual: " + truncate_text(normalize_output(actual), 200);
}

CaseOutcome run_case(const WorkerConfig& config,
                     const Workspace& workspace,
                     const SubmissionData& submission,
                     const TestCaseData& test_case,
                     int index) {
  const fs::path input_file = workspace.worker_path / ("input_" + std::to_string(index) + ".txt");
  const fs::path output_file = workspace.worker_path / ("stdout_" + std::to_string(index) + ".txt");
  const fs::path stderr_file = workspace.worker_path / ("stderr_" + std::to_string(index) + ".txt");
  const fs::path exit_file = workspace.worker_path / ("exit_" + std::to_string(index) + ".txt");
  const fs::path time_file = workspace.worker_path / ("time_" + std::to_string(index) + ".txt");
  const fs::path sandbox_error_file = workspace.worker_path / ("sandbox_" + std::to_string(index) + ".error.txt");
  const fs::path docker_stderr_file = workspace.worker_path / ("docker_run_" + std::to_string(index) + ".stderr.txt");

  write_file(input_file, test_case.input);
  make_world_readable(input_file);

  const int file_blocks = std::max(8, (config.output_limit_bytes + 511) / 512);
  std::ostringstream script;
  script << "ulimit -f " << file_blocks << "; "
         << "cp /workspace/main /runner/main 2> /workspace/" << sandbox_error_file.filename().string()
         << " && chmod 700 /runner/main 2>> /workspace/" << sandbox_error_file.filename().string()
         << "; prep=$?; if [ $prep -ne 0 ]; then echo $prep > /workspace/"
         << exit_file.filename().string() << "; exit $prep; fi; "
         << "if [ -x /usr/bin/time ]; then "
         << "/usr/bin/time -f 'MAX_RSS_KB=%M' -o /workspace/" << time_file.filename().string() << " "
         << "timeout " << timeout_arg(submission.time_limit_ms) << " /runner/main "
         << "< /workspace/" << input_file.filename().string() << " "
         << "> /workspace/" << output_file.filename().string() << " "
         << "2> /workspace/" << stderr_file.filename().string() << "; "
         << "else "
         << "timeout " << timeout_arg(submission.time_limit_ms) << " /runner/main "
         << "< /workspace/" << input_file.filename().string() << " "
         << "> /workspace/" << output_file.filename().string() << " "
         << "2> /workspace/" << stderr_file.filename().string() << "; "
         << "fi; code=$?; echo $code > /workspace/" << exit_file.filename().string()
         << "; exit $code";

  std::ostringstream command;
  command << config.docker_bin
          << " run --rm --network none --read-only"
          << " --security-opt no-new-privileges --cap-drop ALL"
          << " --memory " << submission.memory_limit_mb << "m"
          << " --cpus 1 --pids-limit 64"
          << " --tmpfs /tmp:rw,noexec,nosuid,size=64m"
          << " --tmpfs /runner:rw,exec,nosuid,size=8m,uid=65534,gid=65534,mode=0700"
          << " --user 65534:65534"
          << " -v " << shell_quote(workspace.host_path + ":/workspace:rw")
          << " -w /workspace"
          << " " << shell_quote(config.sandbox_image)
          << " sh -lc " << shell_quote(script.str())
          << " > " << shell_quote((workspace.worker_path / ("docker_run_" + std::to_string(index) + ".stdout.txt")).string())
          << " 2> " << shell_quote(docker_stderr_file.string());

  const auto started = std::chrono::steady_clock::now();
  const int docker_rc = run_shell(command.str());
  const auto finished = std::chrono::steady_clock::now();
  const int elapsed_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count());

  CaseOutcome outcome;
  outcome.time_ms = elapsed_ms;

  const std::string exit_text = trim_copy(read_file_if_exists(exit_file));
  int program_rc = docker_rc;
  const std::string docker_error = read_file_if_exists(docker_stderr_file);
  const std::string sandbox_error = read_file_if_exists(sandbox_error_file);
  const bool sandbox_started = !exit_text.empty() || fs::exists(output_file) || fs::exists(stderr_file);
  if (!sandbox_error.empty()) {
    outcome.status = "System Error";
    outcome.error_summary = truncate_text(sandbox_error, 600);
    return outcome;
  }
  if (docker_rc != 0 && !sandbox_started) {
    outcome.status = "System Error";
    outcome.error_summary = truncate_text(
        docker_error.empty() ? "Docker sandbox failed before running the program" : docker_error,
        600);
    return outcome;
  }
  if (!exit_text.empty()) {
    try {
      program_rc = std::stoi(exit_text);
    } catch (const std::exception&) {
      program_rc = docker_rc;
    }
  }

  outcome.memory_mb = parse_memory_mb(time_file).value_or(0);
  outcome.stdout_data = read_file_if_exists(output_file);
  outcome.stderr_data = read_file_if_exists(stderr_file);

  if (fs::exists(output_file) && fs::file_size(output_file) > static_cast<uintmax_t>(config.output_limit_bytes)) {
    outcome.status = "Runtime Error";
    outcome.error_summary = test_case.kind == "hidden" ? "Output limit exceeded on a hidden test case"
                                                       : "Output limit exceeded";
    outcome.stdout_data = truncate_text(outcome.stdout_data, static_cast<size_t>(config.output_limit_bytes));
    return outcome;
  }

  if (program_rc == 124 || elapsed_ms > submission.time_limit_ms + 500) {
    outcome.status = "Time Limit Exceeded";
    outcome.error_summary = "Time limit exceeded";
    return outcome;
  }
  if (program_rc == 137 || docker_rc == 137) {
    outcome.status = "Memory Limit Exceeded";
    outcome.error_summary = "Memory limit exceeded";
    return outcome;
  }
  if (program_rc != 0 || docker_rc != 0) {
    outcome.status = "Runtime Error";
    if (test_case.kind == "hidden") {
      outcome.error_summary = "Runtime error on a hidden test case";
      return outcome;
    }
    outcome.error_summary = truncate_text(
        outcome.stderr_data.empty() ? (docker_error.empty() ? "Program exited with a non-zero status" : docker_error)
                                    : outcome.stderr_data,
        600);
    return outcome;
  }
  if (test_case.compare_output && !expected_matches(outcome.stdout_data, test_case.expected_output)) {
    outcome.status = "Wrong Answer";
    outcome.error_summary = wrong_answer_summary(test_case, outcome.stdout_data, test_case.expected_output);
    return outcome;
  }

  outcome.status = "Accepted";
  outcome.error_summary = "";
  return outcome;
}

void insert_case_result(Database& db,
                        const SubmissionData& submission,
                        const TestCaseData& test_case,
                        const CaseOutcome& outcome) {
  if (test_case.id.has_value()) {
    db.exec_params(
        "INSERT INTO submission_case_results "
        "(submission_id, test_case_id, status, time_ms, memory_mb, error_summary, sort_order) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7)",
        {std::to_string(submission.id),
         std::to_string(*test_case.id),
         outcome.status,
         std::to_string(outcome.time_ms),
         std::to_string(outcome.memory_mb),
         outcome.error_summary,
         std::to_string(test_case.sort_order)});
  } else {
    db.exec_params(
        "INSERT INTO submission_case_results "
        "(submission_id, test_case_id, status, time_ms, memory_mb, error_summary, sort_order) "
        "VALUES ($1, NULL, $2, $3, $4, $5, $6)",
        {std::to_string(submission.id),
         outcome.status,
         std::to_string(outcome.time_ms),
         std::to_string(outcome.memory_mb),
         outcome.error_summary,
         std::to_string(test_case.sort_order)});
  }
}

void update_submission_final(Database& db,
                             const SubmissionData& submission,
                             const std::string& status,
                             const std::string& compile_error,
                             int total_time_ms,
                             int peak_memory_mb,
                             const std::string& stdout_data,
                             const std::string& stderr_data,
                             const std::string& judge_message,
                             int passed_count,
                             int total_count) {
  db.exec_params(
      "UPDATE submissions SET status = $2, compile_error = NULLIF($3, ''), total_time_ms = $4, "
      "peak_memory_mb = $5, finished_at = now(), stdout = NULLIF($6, ''), stderr = NULLIF($7, ''), "
      "judge_message = NULLIF($8, ''), testcase_passed_count = $9, testcase_total_count = $10 "
      "WHERE id = $1",
      {std::to_string(submission.id),
       status,
       compile_error,
       std::to_string(total_time_ms),
       std::to_string(peak_memory_mb),
       stdout_data,
       stderr_data,
       judge_message,
       std::to_string(passed_count),
       std::to_string(total_count)});
}

void update_problem_stats(Database& db, const SubmissionData& submission, bool accepted) {
  db.exec_params(
      "UPDATE problems SET submission_count = submission_count + 1, "
      "accepted_count = accepted_count + $2::int, "
      "acceptance_rate = CASE WHEN submission_count + 1 = 0 THEN 0 "
      "ELSE round(((accepted_count + $2::int)::numeric / (submission_count + 1)::numeric) * 100, 3) END, "
      "updated_at = now() WHERE id = $1",
      {std::to_string(submission.problem_id), accepted ? "1" : "0"});
}

void mark_system_error(Database& db, long long submission_id, const std::string& message) {
  db.exec_params(
      "UPDATE submissions SET status = 'System Error', finished_at = now(), judge_message = $2 "
      "WHERE id = $1",
      {std::to_string(submission_id), truncate_text(message, 4000)});
}

void judge_task(Database& db, const WorkerConfig& config, const JudgeTask& task) {
  SubmissionData submission = load_submission(db, task.submission_id);
  db.exec_params("UPDATE submissions SET status = 'Judging' WHERE id = $1", {std::to_string(submission.id)});
  db.exec_params("DELETE FROM submission_case_results WHERE submission_id = $1", {std::to_string(submission.id)});

  std::vector<TestCaseData> cases = load_test_cases(db, config, submission, task.mode);
  if (cases.empty()) {
    update_submission_final(db,
                            submission,
                            "System Error",
                            "",
                            0,
                            0,
                            "",
                            "",
                            task.mode == TaskMode::RunSamples ? "No sample test cases are configured"
                                                              : "No test cases are configured",
                            0,
                            0);
    if (task.mode == TaskMode::Submit) {
      update_problem_stats(db, submission, false);
    }
    return;
  }

  Workspace workspace = create_workspace(config, submission.id);
  try {
    write_file(workspace.worker_path / "main.cpp", submission.source_code);
    make_world_readable(workspace.worker_path / "main.cpp");
    CompileResult compile = compile_submission(config, workspace);
    if (!compile.ok) {
      const std::string status = compile.system_error ? "System Error" : "Compilation Error";
      update_submission_final(db,
                              submission,
                              status,
                              compile.system_error ? "" : compile.message,
                              0,
                              0,
                              "",
                              compile.system_error ? compile.message : "",
                              compile.system_error ? compile.message : "Compilation failed",
                              0,
                              static_cast<int>(cases.size()));
      if (task.mode == TaskMode::Submit) {
        update_problem_stats(db, submission, false);
      }
      if (!config.keep_workspace) {
        fs::remove_all(workspace.worker_path);
      }
      return;
    }

    make_world_executable(workspace.worker_path / "main");

    int passed = 0;
    int total_time = 0;
    int peak_memory = 0;
    std::string final_status = "Accepted";
    std::string final_message;
    std::string visible_stdout;
    std::string visible_stderr;

    for (size_t i = 0; i < cases.size(); ++i) {
      const CaseOutcome outcome = run_case(config, workspace, submission, cases[i], static_cast<int>(i + 1));
      insert_case_result(db, submission, cases[i], outcome);
      total_time += outcome.time_ms;
      peak_memory = std::max(peak_memory, outcome.memory_mb);
      if (outcome.status == "Accepted") {
        ++passed;
        if (task.mode != TaskMode::Submit || cases[i].kind != "hidden") {
          visible_stdout = truncate_text(outcome.stdout_data, 12000);
          visible_stderr = truncate_text(outcome.stderr_data, 12000);
        }
        continue;
      }

      final_status = outcome.status;
      final_message = outcome.error_summary;
      if (cases[i].kind != "hidden") {
        visible_stdout = truncate_text(outcome.stdout_data, 12000);
        visible_stderr = truncate_text(outcome.stderr_data, 12000);
      } else {
        visible_stdout.clear();
        visible_stderr.clear();
      }
      break;
    }

    if (final_status == "Accepted") {
      final_message = task.mode == TaskMode::RunCustom ? "Custom input run completed" : "All test cases passed";
    }

    update_submission_final(db,
                            submission,
                            final_status,
                            "",
                            total_time,
                            peak_memory,
                            visible_stdout,
                            visible_stderr,
                            final_message,
                            passed,
                            static_cast<int>(cases.size()));
    if (task.mode == TaskMode::Submit) {
      update_problem_stats(db, submission, final_status == "Accepted");
    }
  } catch (...) {
    if (!config.keep_workspace) {
      fs::remove_all(workspace.worker_path);
    }
    throw;
  }

  if (!config.keep_workspace) {
    fs::remove_all(workspace.worker_path);
  }
}

}  // namespace

int main() {
  WorkerConfig config = load_config();
  if (config.database_url.empty() || config.redis_url.empty()) {
    std::cerr << "DATABASE_URL and REDIS_URL are required\n";
    return 1;
  }

  Database db(config.database_url);
  oj::RedisClient redis(config.redis_url);

  std::cout << "judge-worker listening on queue " << config.queue_name
            << " with sandbox image " << config.sandbox_image << "\n";

  while (true) {
    try {
      auto raw_task = redis.brpop(config.queue_name, 5);
      if (!raw_task.has_value()) {
        continue;
      }
      JudgeTask task = parse_task(*raw_task);
      std::cout << "judge-worker picked " << task_mode_name(task.mode)
                << " submission_id=" << task.submission_id << "\n";
      try {
        judge_task(db, config, task);
      } catch (const std::exception& error) {
        std::cerr << "Judge task failed: " << error.what() << "\n";
        mark_system_error(db, task.submission_id, error.what());
      }
    } catch (const std::exception& error) {
      std::cerr << "Worker loop error: " << error.what() << "\n";
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  }
}
