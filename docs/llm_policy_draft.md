# LLM data-handling policy (current prototype)

This document describes the behavior of the checked-in implementation. It is
not a claim that unimplemented privacy controls already exist.

## Network boundary

- The checked-in Ollama host is `127.0.0.1:11434`.
- `IME3_OLLAMA_HOST`, `IME3_OLLAMA_PORT`, and the JSON settings can redirect
  requests to another Ollama-compatible endpoint.
- If a non-loopback endpoint is configured, input text leaves the local
  process and the operator must review that endpoint's transport security,
  retention, access control, and terms.
- Plain HTTP is appropriate only for the loopback default. A remote endpoint
  should not be used until TLS and authentication have been designed.

## Input and storage

- The text needed for paraphrasing or translation is sent in the Ollama
  request body.
- Candidate caches are bounded, in-memory process state; they are not written
  as a translation-history database.
- The prototype does not currently implement automatic masking of email
  addresses, phone numbers, usernames, or other sensitive text. Users should
  not enter confidential material, especially when a non-loopback host is
  configured.

## Logging

- File debug logging is disabled in the checked-in configuration.
- If explicitly enabled, the configured log is size-bounded and rotated by
  the project logging helper.
- Current Ollama timing logs contain operation/model/timing or error
  information, not the prompt or generated response body.
- This boundary must remain covered by code review when logging changes.

## Planned hardening

Before treating RTAS as a production input method:

- reject non-loopback hosts unless TLS and explicit user consent are enabled;
- add an optional, tested sensitive-token masking layer;
- add tests proving prompts and responses do not enter logs; and
- document cache clearing and process-lifetime behavior with integration
  evidence.
