# Notices

RTAS is a research prototype and portfolio artifact, not a supported product.

- Google Japanese Input and Ollama are installed separately and are not
  redistributed by this repository.
- The default public configuration uses the documented Windows IMM32 API for
  compatibility with an installed Japanese IME.
- The optional `bridge` transport is an experimental investigation of
  implementation details that are not a stable or public Google API. It may
  stop working after an update and is not recommended for production or
  redistribution without an independent terms and legal review. Users should
  review the current terms for their locale rather than treating this notice as
  legal clearance: https://policies.google.com/terms
- The repository does not contain an Ollama model, Google executable, Mozc
  binary, full UniDic/JMdict corpus, or converted OPUS-MT model.
- The small TSV files under `data/dictionary/` are contributor-authored sample
  data for demonstrating the provider boundary; they are not extracted
  dictionary corpora.
- Input can be sent to a non-loopback Ollama-compatible endpoint if the user
  explicitly changes the host setting or environment variables. The default
  checked-in configuration is `127.0.0.1`.

See `DEVELOPMENT_PROCESS.md` for the use of AI coding assistants and
`THIRD_PARTY_NOTICES.md` for bundled third-party source.
