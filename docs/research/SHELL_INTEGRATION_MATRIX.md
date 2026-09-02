# Ztermy shell coverage and integration matrix

Status: implemented baseline, 2026-09-03

## Evidence and product contract

Microsoft documents `wsl --list --verbose`, distribution selection, and ordinary WSL launches as the supported Windows
surface. Ztermy uses the installed `wsl.exe` plus the current user's registered default distribution so startup discovery
does not block the Qt GUI thread. See [Basic commands for WSL](https://learn.microsoft.com/windows/wsl/basic-commands).

Windows Terminal documents OSC 133 prompt, command-start, output-start and command-finished zones. Ztermy's decoder
accepts these interoperable zones and OSC 633, while exact command text remains stronger only when it carries a
ztermy session nonce. See [Shell integration in Windows Terminal](https://learn.microsoft.com/windows/terminal/tutorials/shell-integration).

Nushell enables OSC 133 and OSC 633 semantic zones in its default shell-integration configuration. Its interactive
`pre_prompt` and `pre_execution` hooks are available only in REPL mode. Ztermy launches the normal interactive shell and
does not replace or edit `config.nu`. See the [Nushell default configuration](https://github.com/nushell/nushell/blob/main/crates/nu-config/default_files/doc_config.nu)
and [Nushell hooks](https://www.nushell.sh/book/hooks.html).

## Matrix

| Shell/session | Launch coverage | Semantic lifecycle | Exact command text | Working directory | Current level |
|---|---|---|---|---|---|
| PowerShell 7 / 5.1 local | detected and selectable | ztermy session OSC 633 | nonce verified | OSC 633 `Cwd` | rich |
| Nushell local | detected and selectable | native OSC 133/633 | terminal-input fallback | native OSC 633/OSC 7 | rich lifecycle, basic command provenance |
| Git Bash local | detected and selectable | accepted when user shell emits OSC 133/633 | terminal-input fallback | accepted OSC property | basic by default |
| WSL default distribution | registered default detected and selectable | depends on the distribution shell | terminal-input fallback | accepted OSC property | basic by default |
| Bash/Zsh/Fish over SSH | existing remote shell/history detection | accepted when host integration emits OSC 133/633 | nonce only for installed ztermy integration | accepted OSC property | basic by default, rich when explicitly integrated |
| Cmd local | detected and selectable | none | terminal-input fallback | unavailable | basic |

## Deliberate boundaries

- Detection never installs WSL, Nushell, shell frameworks, or plugins.
- Startup does not synchronously execute discovery commands and does not modify dotfiles.
- Normal user configuration is preserved. Persistent shell integration remains an explicit, reversible operation under
  ADR 0055 rather than a hidden side effect of selecting a shell.
- Capability is reported from observed markers. A shell name alone never upgrades evidence from basic to rich.
