// ztermy-owned synchronization shim for the pinned libghostty C ABI.
const lib = @import("../lib.zig");
const mouse_c = @import("mouse_encode.zig");
const terminal_c = @import("terminal.zig");
const Result = @import("result.zig").Result;

pub fn syncEncoder(encoder: mouse_c.Encoder, handle: terminal_c.Terminal) callconv(lib.calling_conv) Result {
    const wrapper = encoder orelse return .invalid_value;
    const terminal = (handle orelse return .invalid_value).terminal;
    // Refresh from effective engine flags, not DECSET bits: repeating an
    // already-enabled mode can change which tracking mode/format owns input.
    // Upstream refresh unconditionally clears last-cell deduplication.
    if (wrapper.opts.event != terminal.flags.mouse_event or wrapper.opts.format != terminal.flags.mouse_format) {
        mouse_c.setopt_from_terminal(encoder, handle);
    }
    return .success;
}
