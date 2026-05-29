# AI Native Online Judge V1

Phase 1 scaffold for the AI Native Online Judge described in `SPEC.md`.

## Phase 1 Contents

- Separated backend, frontend, deploy, docs, scripts, and testdata directories.
- C++ API Server skeleton based on `cpp-httplib`.
- Hand-written OpenAPI JSON exposed by the API Server.
- Vue 3 + TypeScript + Vite frontend shell with initial routes and pages.
- Docker Compose environment for frontend, API Server, PostgreSQL, and Redis.
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

