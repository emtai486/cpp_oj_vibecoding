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

