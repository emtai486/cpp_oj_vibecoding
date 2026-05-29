#!/usr/bin/env bash
set -euo pipefail

cat <<'MESSAGE'
Admin initialization is handled by the API Server on startup in Phase 2.

Set these environment variables before starting docker compose:

  ADMIN_USERNAME=admin
  ADMIN_EMAIL=admin@example.com
  ADMIN_PASSWORD=admin123456
  JWT_SECRET=replace-with-a-long-random-secret

The server creates the first admin if no admin user exists. If a user with the
same username or email exists, that user is promoted to admin and the password
is updated.
MESSAGE
