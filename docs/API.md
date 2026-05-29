# API

Phase 1 maintains the OpenAPI document by hand at `backend/openapi/openapi.json`.

When the API Server is running, the document is available at:

```txt
GET /api/openapi.json
```

The common response envelope follows `SPEC.md`:

```json
{
  "code": "OK",
  "message": "success",
  "data": {},
  "request_id": "req_1"
}
```

