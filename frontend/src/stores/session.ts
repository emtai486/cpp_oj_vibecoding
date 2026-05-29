import { reactive } from 'vue';

export type SessionRole = 'guest' | 'user' | 'admin';

export interface AuthUser {
  id: number;
  username: string;
  email: string;
  role: Exclude<SessionRole, 'guest'>;
}

export interface SessionState {
  token: string | null;
  username: string | null;
  email: string | null;
  role: SessionRole;
}

const storageKey = 'ai_native_oj_session';

function readStoredSession(): SessionState {
  try {
    const raw = localStorage.getItem(storageKey);
    if (!raw) {
      return { token: null, username: null, email: null, role: 'guest' };
    }
    const parsed = JSON.parse(raw) as SessionState;
    if (!parsed.token || (parsed.role !== 'user' && parsed.role !== 'admin')) {
      return { token: null, username: null, email: null, role: 'guest' };
    }
    return {
      token: parsed.token,
      username: parsed.username,
      email: parsed.email,
      role: parsed.role
    };
  } catch {
    return { token: null, username: null, email: null, role: 'guest' };
  }
}

const initialSession = readStoredSession();

export const session = reactive<SessionState>({
  token: initialSession.token,
  username: initialSession.username,
  email: initialSession.email,
  role: initialSession.role
});

export function setSession(token: string, user: AuthUser) {
  session.token = token;
  session.username = user.username;
  session.email = user.email;
  session.role = user.role;
  localStorage.setItem(storageKey, JSON.stringify(session));
}

export function clearSession() {
  session.token = null;
  session.username = null;
  session.email = null;
  session.role = 'guest';
  localStorage.removeItem(storageKey);
}
