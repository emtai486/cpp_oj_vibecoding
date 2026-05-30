export type ProblemDifficulty = 'easy' | 'medium' | 'hard';
export type ProblemStatus = 'draft' | 'published' | 'archived';

export interface ProblemSummary {
  id: number;
  title: string;
  difficulty: ProblemDifficulty;
  tags: string[];
  accepted_count: number;
  submission_count: number;
  acceptance_rate: number;
  status: ProblemStatus;
}

export interface TestCaseSummary {
  id: number;
  problem_id: number;
  kind: 'sample' | 'public' | 'hidden';
  input_summary: string;
  output_summary: string;
  display_full_content: boolean;
  sort_order: number;
  input?: string;
  output?: string;
  input_path?: string;
  output_path?: string;
}

export interface ProblemDetail extends ProblemSummary {
  description: string;
  time_limit_ms: number;
  memory_limit_mb: number;
  default_code_template: string;
  source: string;
  created_at: string;
  updated_at: string;
  test_cases: TestCaseSummary[];
}

export interface PageResponse<T> {
  items: T[];
  page: number;
  page_size: number;
  total: number;
}

export interface AuthResponse {
  token: string;
  user: {
    id: number;
    username: string;
    email: string;
    role: 'user' | 'admin';
  };
}

export type SubmissionStatus =
  | 'Pending'
  | 'Judging'
  | 'Accepted'
  | 'Wrong Answer'
  | 'Time Limit Exceeded'
  | 'Memory Limit Exceeded'
  | 'Runtime Error'
  | 'Compilation Error'
  | 'System Error';

export interface SubmissionCaseResult {
  id: number;
  submission_id: number;
  test_case_id: number | null;
  status: SubmissionStatus;
  time_ms: number | null;
  memory_mb: number | null;
  error_summary: string;
  sort_order: number;
  kind: 'sample' | 'public' | 'hidden' | 'custom';
  input_summary: string;
  output_summary: string;
  created_at: string;
}

export interface SubmissionSummary {
  id: number;
  user_id: number;
  username: string;
  problem_id: number;
  problem_title: string;
  language: 'cpp17';
  status: SubmissionStatus;
  total_time_ms: number | null;
  peak_memory_mb: number | null;
  created_at: string;
  finished_at: string | null;
  judge_message: string | null;
  testcase_passed_count: number;
  testcase_total_count: number;
  code_length: number;
  ai_analysis_status: 'not_requested' | 'pending' | 'ready' | 'failed';
}

export interface SubmissionDetail extends SubmissionSummary {
  source_code: string;
  compile_error: string | null;
  stdout: string | null;
  stderr: string | null;
  ai_analysis_id: number | null;
  case_results: SubmissionCaseResult[];
}
