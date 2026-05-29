CREATE TABLE IF NOT EXISTS schema_migrations (
  version TEXT PRIMARY KEY,
  applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS roles (
  id BIGSERIAL PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS users (
  id BIGSERIAL PRIMARY KEY,
  username TEXT NOT NULL UNIQUE,
  email TEXT NOT NULL UNIQUE,
  password_hash TEXT NOT NULL,
  role_id BIGINT NOT NULL REFERENCES roles(id),
  status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'disabled')),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS problems (
  id BIGSERIAL PRIMARY KEY,
  title TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  difficulty TEXT NOT NULL CHECK (difficulty IN ('easy', 'medium', 'hard')),
  tags TEXT[] NOT NULL DEFAULT '{}',
  time_limit_ms INTEGER NOT NULL DEFAULT 2000 CHECK (time_limit_ms > 0),
  memory_limit_mb INTEGER NOT NULL DEFAULT 256 CHECK (memory_limit_mb > 0),
  default_code_template TEXT NOT NULL DEFAULT '',
  source TEXT NOT NULL DEFAULT '',
  accepted_count BIGINT NOT NULL DEFAULT 0 CHECK (accepted_count >= 0),
  submission_count BIGINT NOT NULL DEFAULT 0 CHECK (submission_count >= 0),
  acceptance_rate NUMERIC(6, 3) NOT NULL DEFAULT 0 CHECK (acceptance_rate >= 0 AND acceptance_rate <= 100),
  status TEXT NOT NULL DEFAULT 'draft' CHECK (status IN ('draft', 'published', 'archived')),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS problem_tags (
  id BIGSERIAL PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS test_cases (
  id BIGSERIAL PRIMARY KEY,
  problem_id BIGINT NOT NULL REFERENCES problems(id) ON DELETE CASCADE,
  kind TEXT NOT NULL CHECK (kind IN ('sample', 'public', 'hidden')),
  input_path TEXT NOT NULL,
  output_path TEXT NOT NULL,
  input_summary TEXT NOT NULL DEFAULT '',
  output_summary TEXT NOT NULL DEFAULT '',
  display_full_content BOOLEAN NOT NULL DEFAULT false,
  sort_order INTEGER NOT NULL DEFAULT 0,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS submissions (
  id BIGSERIAL PRIMARY KEY,
  user_id BIGINT NOT NULL REFERENCES users(id),
  problem_id BIGINT NOT NULL REFERENCES problems(id),
  language TEXT NOT NULL DEFAULT 'cpp17' CHECK (language = 'cpp17'),
  source_code TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'Pending' CHECK (
    status IN (
      'Pending',
      'Judging',
      'Accepted',
      'Wrong Answer',
      'Time Limit Exceeded',
      'Memory Limit Exceeded',
      'Runtime Error',
      'Compilation Error',
      'System Error'
    )
  ),
  compile_error TEXT,
  total_time_ms INTEGER,
  peak_memory_mb INTEGER,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  finished_at TIMESTAMPTZ,
  stdout TEXT,
  stderr TEXT,
  judge_message TEXT,
  testcase_passed_count INTEGER NOT NULL DEFAULT 0,
  testcase_total_count INTEGER NOT NULL DEFAULT 0,
  ai_analysis_status TEXT NOT NULL DEFAULT 'not_requested' CHECK (
    ai_analysis_status IN ('not_requested', 'pending', 'ready', 'failed')
  ),
  ai_analysis_id BIGINT,
  code_length INTEGER NOT NULL DEFAULT 0 CHECK (code_length >= 0)
);

CREATE TABLE IF NOT EXISTS submission_case_results (
  id BIGSERIAL PRIMARY KEY,
  submission_id BIGINT NOT NULL REFERENCES submissions(id) ON DELETE CASCADE,
  test_case_id BIGINT REFERENCES test_cases(id) ON DELETE SET NULL,
  status TEXT NOT NULL,
  time_ms INTEGER,
  memory_mb INTEGER,
  error_summary TEXT NOT NULL DEFAULT '',
  sort_order INTEGER NOT NULL DEFAULT 0,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS ai_analyses (
  id BIGSERIAL PRIMARY KEY,
  submission_id BIGINT REFERENCES submissions(id) ON DELETE SET NULL,
  user_id BIGINT REFERENCES users(id) ON DELETE SET NULL,
  problem_id BIGINT REFERENCES problems(id) ON DELETE SET NULL,
  kind TEXT NOT NULL CHECK (kind IN ('hint', 'compile_error', 'wrong_answer', 'time_limit_exceeded')),
  status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'ready', 'failed')),
  prompt_summary TEXT NOT NULL DEFAULT '',
  response TEXT,
  error_message TEXT,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  finished_at TIMESTAMPTZ
);

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1
    FROM pg_constraint
    WHERE conname = 'submissions_ai_analysis_id_fk'
  ) THEN
    ALTER TABLE submissions
      ADD CONSTRAINT submissions_ai_analysis_id_fk
      FOREIGN KEY (ai_analysis_id) REFERENCES ai_analyses(id) DEFERRABLE INITIALLY DEFERRED;
  END IF;
END $$;

CREATE TABLE IF NOT EXISTS ai_usage_logs (
  id BIGSERIAL PRIMARY KEY,
  user_id BIGINT REFERENCES users(id) ON DELETE SET NULL,
  ai_analysis_id BIGINT REFERENCES ai_analyses(id) ON DELETE SET NULL,
  provider TEXT NOT NULL DEFAULT 'openai',
  model TEXT NOT NULL DEFAULT '',
  prompt_tokens INTEGER NOT NULL DEFAULT 0,
  completion_tokens INTEGER NOT NULL DEFAULT 0,
  total_tokens INTEGER NOT NULL DEFAULT 0,
  cost_cents INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT 'pending',
  request_id TEXT NOT NULL DEFAULT '',
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_submissions_user_id ON submissions(user_id);
CREATE INDEX IF NOT EXISTS idx_submissions_problem_id ON submissions(problem_id);
CREATE INDEX IF NOT EXISTS idx_submissions_status ON submissions(status);
CREATE INDEX IF NOT EXISTS idx_submissions_created_at ON submissions(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_problems_difficulty ON problems(difficulty);
CREATE INDEX IF NOT EXISTS idx_problems_status ON problems(status);
CREATE INDEX IF NOT EXISTS idx_test_cases_problem_id ON test_cases(problem_id);
CREATE INDEX IF NOT EXISTS idx_ai_usage_logs_created_at ON ai_usage_logs(created_at DESC);

INSERT INTO roles (name)
VALUES ('user'), ('admin')
ON CONFLICT (name) DO NOTHING;

INSERT INTO schema_migrations (version)
VALUES ('001_init')
ON CONFLICT (version) DO NOTHING;
