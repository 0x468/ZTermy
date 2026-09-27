// ztermy-owned extension for inserting decoded pixels, not a VT protocol.
const std = @import("std");
const lib = @import("../lib.zig");
const terminal_c = @import("terminal.zig");
const Result = @import("result.zig").Result;
const Image = @import("../kitty/graphics_image.zig").Image;
const Storage = @import("../kitty/graphics_storage.zig").ImageStorage;
const unicode = @import("../kitty/graphics_unicode.zig");
const graphics_c = @import("kitty_graphics.zig");
const Screen = @import("../Screen.zig");
const ScreenSet = @import("../ScreenSet.zig");
const image_budget = @import("ztermy_image_budget.zig");

pub fn configureImageBudget(bytes: usize) callconv(lib.calling_conv) Result {
    return if (image_budget.configure(bytes)) .success else .invalid_value;
}

pub fn imageBudgetUsage() callconv(lib.calling_conv) usize {
    return image_budget.current();
}

pub fn exchangeImageReclaimer(value: image_budget.Reclaimer) callconv(lib.calling_conv) image_budget.Reclaimer {
    return image_budget.exchangeReclaimer(value);
}

pub const ImageRecord = extern struct {
    image_id: u32,
    visible: u32,
    generation: u64,
    bytes: u64,
    screen_generation: u64,
};

// Worker-thread only. Visibility belongs to the image, across every placement.
fn imageVisible(t: *terminal_c.ZigTerminal, screen: *Screen, image: *const Image) bool {
    if (screen != t.screens.active) return false;
    var placements = screen.kitty_images.placements.iterator();
    var has_virtual = false;
    while (placements.next()) |entry| {
        if (entry.key_ptr.image_id != image.id) continue;
        if (entry.value_ptr.location == .virtual) {
            has_virtual = true;
        } else if (graphics_c.computeViewportPos(entry.value_ptr, image, t).visible) {
            return true;
        }
    }
    if (!has_virtual) return false;
    const bottom = screen.pages.getBottomRight(.viewport) orelse return true;
    var placeholders = unicode.placementIterator(screen.pages.getTopLeft(.viewport), bottom);
    while (placeholders.next()) |placement| {
        if (placement.image_id == image.id) return true;
    }
    return false;
}

pub fn imageInventory(
    handle: terminal_c.Terminal,
    alternate: bool,
    output: ?[*]ImageRecord,
    capacity: usize,
    count: ?*usize,
) callconv(lib.calling_conv) Result {
    const t = (handle orelse return .invalid_value).terminal;
    const written = count orelse return .invalid_value;
    written.* = 0;
    const key: ScreenSet.Key = if (alternate) .alternate else .primary;
    const screen = t.screens.get(key) orelse return .success;
    const storage = &screen.kitty_images;
    written.* = storage.images.count();
    if (capacity < written.*) return .out_of_memory;
    if (written.* == 0) return .success;
    const destination = output orelse return .invalid_value;
    var indices: std.AutoHashMapUnmanaged(u32, usize) = .empty;
    defer indices.deinit(t.gpa());
    indices.ensureTotalCapacity(t.gpa(), @intCast(written.*)) catch return .out_of_memory;
    var images = storage.images.valueIterator();
    var index: usize = 0;
    while (images.next()) |image| : (index += 1) {
        indices.putAssumeCapacity(image.id, index);
        destination[index] = .{
            .image_id = image.id,
            .visible = 0,
            .generation = image.generation,
            .bytes = image.data.len,
            .screen_generation = t.screens.generation(key),
        };
    }
    if (screen != t.screens.active) return .success;
    // One pass over placements and one over Unicode placeholders, rather than
    // rescanning both once per image (up to 4096 times at the metadata limit).
    var placements = storage.placements.iterator();
    var has_virtual = false;
    while (placements.next()) |entry| {
        const slot = indices.get(entry.key_ptr.image_id) orelse continue;
        const record = &destination[slot];
        if (record.visible == 1) continue;
        if (entry.value_ptr.location == .virtual) {
            record.visible = 2; // temporary: has a virtual prototype
            has_virtual = true;
        } else {
            const image = storage.images.getPtr(entry.key_ptr.image_id) orelse continue;
            if (graphics_c.computeViewportPos(entry.value_ptr, image, t).visible)
                record.visible = 1;
        }
    }
    if (has_virtual) {
        if (screen.pages.getBottomRight(.viewport)) |bottom| {
            var placeholders = unicode.placementIterator(screen.pages.getTopLeft(.viewport), bottom);
            while (placeholders.next()) |placement| {
                const slot = indices.get(placement.image_id) orelse continue;
                if (destination[slot].visible == 2) destination[slot].visible = 1;
            }
        } else {
            // Missing viewport geometry cannot prove a prototype is invisible.
            for (destination[0..written.*]) |*record| {
                if (record.visible == 2) record.visible = 1;
            }
        }
        for (destination[0..written.*]) |*record| record.visible &= 1;
    }
    return .success;
}

// A queued candidate is only a hint. Recheck identity and visibility on its
// owning worker immediately before releasing pixels and screen-owned pins.
pub fn evictImage(
    handle: terminal_c.Terminal,
    alternate: bool,
    id: u32,
    screen_generation: u64,
    generation: u64,
) callconv(lib.calling_conv) Result {
    const t = (handle orelse return .invalid_value).terminal;
    const key: ScreenSet.Key = if (alternate) .alternate else .primary;
    if (t.screens.generation(key) != screen_generation) return .no_value;
    const screen = t.screens.get(key) orelse return .no_value;
    const storage = &screen.kitty_images;
    const image = storage.images.getPtr(id) orelse return .no_value;
    if (image.generation != generation or imageVisible(t, screen, image)) return .no_value;
    storage.deleteById(t.gpa(), screen, id, 0, true);
    storage.markMutated(t.io());
    return .success;
}

pub const UnicodePlacement = extern struct {
    image_id: u32,
    placement_id: u32,
    column: i32,
    row: i32,
    offset_x: f64,
    offset_y: f64,
    width: f64,
    height: f64,
    source_x: f64,
    source_y: f64,
    source_width: f64,
    source_height: f64,
};

// Borrow only the pinned engine's existing placeholder iterator. It handles
// palette IDs, combining marks and inherited coordinates. Geometry stays in
// floating point: the upstream integer render rectangle loses magnified texels.
pub fn unicodePlacements(
    handle: terminal_c.Terminal,
    output: ?[*]UnicodePlacement,
    capacity: usize,
    count: ?*usize,
) callconv(lib.calling_conv) Result {
    const t = (handle orelse return .invalid_value).terminal;
    const written = count orelse return .invalid_value;
    written.* = 0;
    if (capacity == 0) return .success;
    const destination = output orelse return .invalid_value;
    if (capacity > 4096) return .invalid_value;
    if (t.cols == 0 or t.rows == 0) return .success;
    const cell_width = t.width_px / t.cols;
    const cell_height = t.height_px / t.rows;
    if (cell_width == 0 or cell_height == 0) return .success;
    const screen = t.screens.active;
    const bottom = screen.pages.getBottomRight(.viewport) orelse return .success;
    var iterator = unicode.placementIterator(screen.pages.getTopLeft(.viewport), bottom);
    while (written.* < capacity) {
        const placement = iterator.next() orelse break;
        const image = screen.kitty_images.imageById(placement.image_id) orelse continue;
        const prototype = prototype: {
            if (placement.placement_id != 0) {
                break :prototype screen.kitty_images.placements.get(.{
                    .image_id = placement.image_id,
                    .placement_id = .{ .tag = .external, .id = placement.placement_id },
                }) orelse continue;
            }
            var candidates = screen.kitty_images.placements.iterator();
            while (candidates.next()) |candidate| {
                if (candidate.key_ptr.image_id == placement.image_id and candidate.value_ptr.location == .virtual)
                    break :prototype candidate.value_ptr.*;
            }
            continue;
        };
        if (prototype.location != .virtual or image.width == 0 or image.height == 0) continue;
        const columns = if (prototype.columns != 0) prototype.columns else (image.width + cell_width - 1) / cell_width;
        const rows = if (prototype.rows != 0) prototype.rows else (image.height + cell_height - 1) / cell_height;
        if (columns == 0 or rows == 0 or columns > 65535 or rows > 65535) continue;
        const grid_w = @as(f64, @floatFromInt(columns)) * @as(f64, @floatFromInt(cell_width));
        const grid_h = @as(f64, @floatFromInt(rows)) * @as(f64, @floatFromInt(cell_height));
        const image_w: f64 = @floatFromInt(image.width);
        const image_h: f64 = @floatFromInt(image.height);
        const scale = @min(grid_w / image_w, grid_h / image_h);
        const left = (grid_w - image_w * scale) / 2;
        const top = (grid_h - image_h * scale) / 2;
        const fragment_x = @as(f64, @floatFromInt(placement.col)) * @as(f64, @floatFromInt(cell_width));
        const fragment_y = @as(f64, @floatFromInt(placement.row)) * @as(f64, @floatFromInt(cell_height));
        const x = @max(fragment_x, left);
        const y = @max(fragment_y, top);
        const right = @min(fragment_x + @as(f64, @floatFromInt(placement.width)) * @as(f64, @floatFromInt(cell_width)), left + image_w * scale);
        const bottom_edge = @min(fragment_y + @as(f64, @floatFromInt(placement.height)) * @as(f64, @floatFromInt(cell_height)), top + image_h * scale);
        if (right <= x or bottom_edge <= y) continue;
        const point = screen.pages.pointFromPin(.viewport, placement.pin) orelse continue;
        destination[written.*] = .{
            .image_id = placement.image_id,
            .placement_id = placement.placement_id,
            .column = @intCast(point.viewport.x),
            .row = @intCast(point.viewport.y),
            .offset_x = x - fragment_x,
            .offset_y = y - fragment_y,
            .width = right - x,
            .height = bottom_edge - y,
            .source_x = (x - left) / scale,
            .source_y = (y - top) / scale,
            .source_width = (right - x) / scale,
            .source_height = (bottom_edge - y) / scale,
        };
        written.* += 1;
    }
    return .success;
}

pub fn insert(
    handle: terminal_c.Terminal,
    pixels: ?[*]const u8,
    length: usize,
    width: u32,
    height: u32,
    numerator: u32,
    denominator: u32,
    absolute: bool,
) callconv(lib.calling_conv) Result {
    const t = (handle orelse return .invalid_value).terminal;
    const source = pixels orelse return .invalid_value;
    if (width == 0 or height == 0 or width > 8192 or height > 8192 or
        numerator == 0 or denominator == 0 or length != @as(u64, width) * height * 4)
        return .invalid_value;
    const scaled_height = (@as(u64, height) * numerator + denominator - 1) / denominator;
    if (scaled_height == 0 or scaled_height > 8192 or
        scaled_height * width * 4 > 32 * 1024 * 1024) return .invalid_value;
    const output_width = if (absolute) @min(width, t.width_px) else width;
    const output_height: u32 = @intCast(if (absolute) @min(scaled_height, t.height_px) else scaled_height);
    if (output_width == 0 or output_height == 0) return .invalid_value;
    const alloc = t.gpa();
    const io = t.io();
    const screen = t.screens.active;
    const storage = &screen.kitty_images;
    if (!storage.enabled()) return .no_value;
    const output = alloc.alloc(u8, @as(usize, output_width) * output_height * 4) catch return .out_of_memory;
    var owns_pixels = true;
    defer if (owns_pixels) alloc.free(output);
    const stride = @as(usize, output_width) * 4;
    for (0..output_height) |y| {
        const source_y = @min(@as(u64, y) * denominator / numerator, height - 1);
        const start: usize = @intCast(source_y * width * 4);
        @memcpy(output[y * stride ..][0..stride], source[start..][0..stride]);
    }
    const location = if (absolute)
        screen.pages.pin(.{ .active = .{} }) orelse return .invalid_value
    else
        screen.cursor.page_pin.*;
    const pin = screen.pages.trackPin(location) catch return .out_of_memory;
    var owns_pin = true;
    defer if (owns_pin) screen.pages.untrackPin(pin);

    // Do not replace an explicit Kitty ID, including one still being uploaded.
    while (storage.next_image_id == 0 or storage.imageById(storage.next_image_id) != null or
        (if (storage.loading) |loading| loading.image.id == storage.next_image_id else false))
        storage.next_image_id +%= 1;
    const id = storage.next_image_id;
    storage.next_image_id +%= 1;
    const image: Image = .{
        .id = id,
        .width = output_width,
        .height = output_height,
        .format = .rgba,
        .data = output,
        .implicit_id = true,
    };
    storage.addImage(io, alloc, image) catch return .out_of_memory;
    owns_pixels = false;
    const placement: Storage.Placement = .{ .location = .{ .pin = pin }, .z = -1 };
    storage.addPlacement(io, alloc, id, 0, placement) catch {
        if (storage.images.fetchRemove(id)) |removed| {
            var orphan = removed.value;
            storage.total_bytes -= orphan.data.len;
            orphan.deinit(alloc);
        }
        return .out_of_memory;
    };
    owns_pin = false;
    return .success;
}
