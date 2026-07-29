# Contributing

ztermy is currently a personal, private project managed with public-project
engineering practices.

## Workflow

1. Create a focused branch from `main`.
2. Keep commits small and describe why a change is needed.
3. Add or update tests and documentation with behavior changes.
4. Run the applicable configure, build, test, format, and analysis checks.
5. Record architecture changes in an ADR.

## Commit style

All commits must follow the Conventional Commits specification:

```text
feat: add host key verification state
fix: preserve terminal grid during DPI transition
docs: record ConPTY threading decision
```

Use an imperative, lower-case description. Add a scope when it improves
clarity, for example `feat(window): support native resize hit testing`.
Breaking changes must use `!` or a `BREAKING CHANGE:` footer.

Do not commit generated build output, credentials, private keys, production
host details, terminal transcripts, crash dumps, or local configuration.
