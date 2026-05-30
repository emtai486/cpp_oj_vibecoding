# Judge Worker

Phase 3 implements the Judge Worker as a C++ executable that consumes Redis tasks from `judge:queue`.

Runtime responsibilities:

- Load submission source, problem limits, and test case metadata from PostgreSQL.
- Compile C++17 source in a Docker container.
- Run each test case with network disabled, memory/CPU/pid limits, read-only rootfs, and output limits.
- Write `submission_case_results` and final submission status back to PostgreSQL.

Important environment variables:

- `DATABASE_URL`: PostgreSQL connection string.
- `REDIS_URL`: Redis connection string.
- `JUDGE_QUEUE`: queue name, default `judge:queue`.
- `TESTDATA_ROOT`: mounted test data path.
- `JUDGE_WORKSPACE_ROOT`: path visible inside the worker container.
- `JUDGE_WORKSPACE_HOST_ROOT`: matching host path passed to Docker bind mounts.
- `JUDGE_SANDBOX_IMAGE`: default `gcc:13-bookworm`.

The Docker Compose deployment mounts `/var/run/docker.sock` so the worker can start sandbox containers.
