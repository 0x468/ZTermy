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
    offset_x: u32,
    offset_y: u32,
    width: u32,
    height: u32,
    source_x: u32,
    source_y: u32,
    source_width: u32,
    source_height: u32,
};

// Borrow only the pinned engine's existing placeholder iterator. It handles
// palette IDs, combining marks, inherited coordinates and aspect-fit fragments.
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
        const render = placement.renderPlacement(&screen.kitty_images, &image, cell_width, cell_height) catch continue;
        if (render.dest_width == 0 or render.dest_height == 0 or
            render.source_width == 0 or render.source_height == 0) continue;
        const point = screen.pages.pointFromPin(.viewport, render.top_left) orelse continue;
        destination[written.*] = .{
            .image_id = placement.image_id,
            .placement_id = placement.placement_id,
            .column = @intCast(point.viewport.x),
            .row = @intCast(point.viewport.y),
            .offset_x = render.offset_x,
            .offset_y = render.offset_y,
            .width = render.dest_width,
            .height = render.dest_height,
            .source_x = render.source_x,
            .source_y = render.source_y,
            .source_width = render.source_width,
            .source_height = render.source_height,
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
