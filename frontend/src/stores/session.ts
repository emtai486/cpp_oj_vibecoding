import { reactive } from 'vue';

export interface SessionState {
  token: string | null;
  username: string | null;
  role: 'guest' | 'user' | 'admin';
}

export const session = reactive<SessionState>({
  token: null,
  username: null,
  role: 'guest'
});

