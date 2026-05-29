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
