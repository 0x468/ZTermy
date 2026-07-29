# Security policy

ztermy is not ready for production security reporting or public distribution.

## Security invariants

- Unknown SSH host keys require an explicit user decision.
- Changed host keys block connection by default.
- Passwords and passphrases are runtime-only secrets.
- Private key contents are never copied into application configuration.
- Secrets and terminal input must not appear in logs, crash metadata, tests, or
  screenshots.
- Persisted credentials must use Windows Credential Manager or another approved
  Windows protection mechanism.
- Network and file operations use explicit timeouts and cancellation.

Security-sensitive behavior must have automated regression tests and manual
acceptance evidence.

