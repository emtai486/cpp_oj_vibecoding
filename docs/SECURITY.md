# Security

The V1 security model is centered on Docker sandboxed judging, authenticated API access, administrator role checks, and AI data minimization.

Phase 1 establishes the directories and deployment surfaces that later phases will harden:

- User code execution must always run inside Docker with no network access.
- Hidden test case input and output must not be returned to users or sent to AI prompts.
- All user and administrator APIs must return uniform authorization errors.
- All requests should carry or receive a `request_id` for traceability.
- AI requests should be rate-limited and cost-capped before OpenAI integration is enabled.

