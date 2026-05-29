# AI Native Online Judge V1

Phase 2 implementation for the AI Native Online Judge described in `SPEC.md`.

## Current Contents

- Separated backend, frontend, deploy, docs, scripts, and testdata directories.
- C++ API Server based on `cpp-httplib`, PostgreSQL `libpq`, and OpenSSL.
- User registration, login, PBKDF2 password hashing, and HMAC-SHA256 JWT authentication.
- Published problem list/detail APIs and administrator problem/test-data management APIs.
- Hand-written OpenAPI JSON exposed by the API Server.
- Vue 3 + TypeScript + Vite frontend shell with initial routes and pages.
- Docker Compose environment for frontend, API Server, PostgreSQL, Redis, and mounted test data.
- PostgreSQL migration bootstrap and Redis configuration.

## Quick Start

From the repository root:

```bash
docker compose -f deploy/docker-compose.yml up --build
```

Default service URLs:

- Frontend: `http://localhost:5173`
- API health: `http://localhost:8080/api/health`
- OpenAPI JSON: `http://localhost:8080/api/openapi.json`
- PostgreSQL: `localhost:5432`
- Redis: `localhost:6379`

Default local admin bootstrap values are configured in `deploy/docker-compose.yml`:

- Username: `admin`
- Email: `admin@example.com`
- Password: `admin123456`

Override `ADMIN_PASSWORD` and `JWT_SECRET` before deploying outside local development.

## Local Backend Build

```bash
cmake -S backend -B backend/build
cmake --build backend/build --target api-server
```

The backend CMake configuration fetches `cpp-httplib` when it is not already available as a CMake package.

## Local Frontend Dev

```bash
cd frontend
npm install
npm run dev
```
