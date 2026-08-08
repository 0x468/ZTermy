# V2.6 scope: bounded remote telemetry

Status: implemented for `0.2.6`

V2.6 adds read-only remote system telemetry to connected SSH terminal tabs. It
keeps the compact information hierarchy demonstrated by NetCatty while using
ztermy's native session architecture and stricter lifetime/resource bounds.

## User-visible contract

- A connected, active SSH tab can show compact CPU, memory, root-disk, network,
  and SSH probe-latency values in the terminal identity row.
- Each metric is keyboard focusable and can open a themed detail surface. Hover
  is a convenience, never the only route to the details.
- Detail surfaces show bounded recent trends plus the relevant detail set:
  per-core CPU, memory/swap and top processes, mounted filesystems, or network
  interfaces.
- Unsupported, paused, and temporarily unavailable telemetry is quiet and does
  not replace the terminal status or produce modal UI.
- Monitoring is Linux-first in 0.2.6. The protocol has an explicit OS field so
  later macOS and Windows adapters do not change the UI contract.

## Runtime contract

- Monitoring uses the authenticated session's isolated auxiliary exec channel;
  commands are never written to the interactive PTY.
- Shell-history requests have priority and may pre-empt an in-flight telemetry
  probe. Terminal reads, writes, resize, selection, and rendering remain
  independent.
- Polling runs only while the SSH tab is connected, selected, and its terminal
  page is visible in the foreground window.
- Fast samples have a minimum five-second interval. Expensive process and
  filesystem details are sampled no more than once every 30 seconds.
- A probe has a three-second deadline, a 64 KiB output ceiling, at most 32 CPU
  cores/filesystems/interfaces and eight processes, and no persistence.
- Three consecutive failures suspend polling. Returning to the tab or an
  explicit refresh resets the suspension; successful samples reset backoff.
- Each tab retains at most 24 samples in memory. Closing the tab destroys the
  samples and cancels any auxiliary probe.

## Non-goals

- Monitoring local Windows resource usage.
- macOS, BSD, network-appliance, or remote Windows collectors.
- Alerting, long-term storage, exports, or background monitoring of hidden tabs.
- Executing user-provided monitoring commands.
- Reproducing third-party code, assets, or branding.

## Reference comparison

NetCatty reads Linux `/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, `df`, and a
bounded `ps` list over SSH exec, computes counter deltas client-side, and renders
compact values with hover details. ztermy follows that product-level model, but
splits fast counters from slow details, gives shell history explicit priority,
uses shorter deadlines, stops on visibility loss, and bounds every collection.
