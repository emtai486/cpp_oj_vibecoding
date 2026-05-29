#!/usr/bin/env bash
set -euo pipefail

DATABASE_URL="${DATABASE_URL:-postgres://oj_user:oj_password@localhost:5432/oj_db}"

psql "${DATABASE_URL}" -f backend/migrations/001_init.sql

