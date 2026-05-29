# PostgreSQL

`deploy/postgres/init/001_init.sql` runs the versioned migration in `backend/migrations/001_init.sql` when the PostgreSQL container initializes an empty data directory.

Use `scripts/migrate_db.sh` for manual local migration runs against an existing database.

