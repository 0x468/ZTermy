// ztermy-owned extension for inserting decoded pixels, not a VT protocol.
const std = @import("std");
const lib = @import("../lib.zig");
const terminal_c = @import("terminal.zig");
const Result = @import("result.zig").Result;
const Image = @import("../kitty/graphics_image.zig").Image;
const Storage = @import("../kitty/graphics_storage.zig").ImageStorage;
const unicode = @import("../kitty/graphics_unicode.zig");

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
