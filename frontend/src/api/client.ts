export interface ApiResponse<T> {
  code: string;
  message: string;
  data: T;
  request_id: string;
}

import { session } from '../stores/session';

const API_BASE_URL = import.meta.env.VITE_API_BASE_URL ?? '';

export class ApiError extends Error {
  status: number;
  code: string;
  requestId: string | null;

  constructor(status: number, code: string, message: string, requestId: string | null) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
    this.requestId = requestId;
  }
}

async function parseError(response: Response, fallbackPath: string): Promise<ApiError> {
  try {
    const body = (await response.json()) as ApiResponse<unknown>;
    return new ApiError(response.status, body.code, body.message, body.request_id);
  } catch {
    return new ApiError(
      response.status,
      'HTTP_ERROR',
      `${fallbackPath} failed with ${response.status}`,
      response.headers.get('X-Request-Id')
    );
  }
}

async function apiRequest<T>(
  method: 'GET' | 'POST' | 'PUT' | 'DELETE',
  path: string,
  body?: unknown
): Promise<ApiResponse<T>> {
  const headers: Record<string, string> = {
    Accept: 'application/json'
  };

  const init: RequestInit = {
    method,
    headers
  };

  if (session.token) {
    headers.Authorization = `Bearer ${session.token}`;
  }

  if (body !== undefined) {
    headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }

  const response = await fetch(`${API_BASE_URL}${path}`, {
    ...init
  });

  if (!response.ok) {
    throw await parseError(response, `${method} ${path}`);
  }

  return response.json() as Promise<ApiResponse<T>>;
}

export function apiGet<T>(path: string): Promise<ApiResponse<T>> {
  return apiRequest<T>('GET', path);
}

export function apiPost<T>(path: string, body: unknown): Promise<ApiResponse<T>> {
  return apiRequest<T>('POST', path, body);
}

export function apiPut<T>(path: string, body: unknown): Promise<ApiResponse<T>> {
  return apiRequest<T>('PUT', path, body);
}
