# Development process and AI assistance

RTAS is an individual project. The author owns the engineering decisions and
used Codex and other generative-AI tools as implementation assistants.

The author was responsible for:

- defining the problem and product direction;
- deciding which AI proposals to accept, revise, or reject;
- supplying follow-up requirements and constraints;
- integrating changes and resolving conflicts between implementations;
- building and testing on Windows;
- verifying the TSF input flow, candidate UI, Google Japanese Input
  compatibility experiments, and local Ollama integration on the development
  machine; and
- deciding what was ready to keep, publish, or describe as unfinished.

AI assistants were used to draft or revise portions of source code, tests,
design notes, investigation plans, and portfolio documentation. Their output
was not treated as authoritative: the author reviewed the changes, ran the
build and tests, and issued additional instructions when behavior or design
did not match the intended result.

Branch names and old notes containing `codex` identify AI-assisted work, not a
second human contributor. The public snapshot keeps this disclosure while
removing temporary prompts, patch fragments, machine-specific captures, and
obsolete planning files that are not part of the product or its evidence.

## Current verification boundary

On 2026-07-25, the author verified:

- `Release|x64` solution build: success, zero warnings and zero errors;
- unit-test project build: success; and
- `Ime3Tests.exe`: exit code 0.

The unit tests cover settings parsing, dictionary loading, provider behavior,
and the learning store. Automated tests do not prove end-to-end behavior inside
every Windows application, a live local Ollama model, or every Google Japanese
Input version. Those are integration and manual-test boundaries and are
described as such in the README and `tests/manual/`.
