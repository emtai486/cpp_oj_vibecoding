export interface ApiResponse<T> {
  code: string;
  message: string;
  data: T;
  request_id: string;
}

const API_BASE_URL = import.meta.env.VITE_API_BASE_URL ?? '';

export async function apiGet<T>(path: string): Promise<ApiResponse<T>> {
  const response = await fetch(`${API_BASE_URL}${path}`, {
    headers: {
      Accept: 'application/json'
    }
  });

  if (!response.ok) {
    throw new Error(`GET ${path} failed with ${response.status}`);
  }

  return response.json() as Promise<ApiResponse<T>>;
}

