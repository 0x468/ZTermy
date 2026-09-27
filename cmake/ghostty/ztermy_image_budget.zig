// ztermy-owned decoded-storage accounting. Loader/snapshot/GPU copies are
// deliberately outside this counter. Configure before terminal workers start.
const std = @import("std");
const lib = @import("../lib.zig");

pub const Reclaimer = extern struct {
    callback: ?*const fn (?*anyopaque, usize, u32, bool) callconv(lib.calling_conv) bool = null,
    userdata: ?*anyopaque = null,
};
threadlocal var reclaimer: Reclaimer = .{};

pub fn exchangeReclaimer(value: Reclaimer) Reclaimer {
    const previous = reclaimer;
    reclaimer = value;
    return previous;
}

var used = std.atomic.Value(usize).init(0);
var ceiling = std.atomic.Value(usize).init(std.math.maxInt(usize));

pub fn configure(bytes: usize) bool {
    if (used.load(.acquire) != 0) return false;
    ceiling.store(bytes, .release);
    return true;
}

pub fn current() usize {
    return used.load(.acquire);
}

pub fn reclaimScreen(bytes: usize, protected_id: u32) ?bool {
    const callback = reclaimer.callback orelse return null;
    return callback(reclaimer.userdata, bytes, protected_id, true);
}

pub fn reserve(bytes: usize, protected_id: u32) bool {
    if (tryReserve(bytes)) return true;
    const callback = reclaimer.callback orelse return false;
    const current_bytes = used.load(.acquire);
    const limit = ceiling.load(.acquire);
    const deficit = bytes -| (limit -| current_bytes);
    if (deficit == 0) return tryReserve(bytes);
    if (!callback(reclaimer.userdata, deficit, protected_id, false)) return false;
    return tryReserve(bytes);
}

fn tryReserve(bytes: usize) bool {
    var previous = used.load(.monotonic);
    while (true) {
        const limit = ceiling.load(.acquire);
        if (previous > limit or bytes > limit - previous) return false;
        if (used.cmpxchgWeak(previous, previous + bytes, .acq_rel, .monotonic)) |actual| {
            previous = actual;
        } else return true;
    }
}

pub fn release(bytes: usize) void {
    const previous = used.fetchSub(bytes, .acq_rel);
    std.debug.assert(previous >= bytes);
}
