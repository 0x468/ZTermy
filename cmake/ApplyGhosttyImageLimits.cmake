# The pinned upstream exposes storage limits, but not loading/inflation limits.
# Keep this mechanical patch fail-closed across dependency upgrades.
function(ztermy_replace_ghostty_text relative_path original replacement)
    set(path "${GHOSTTY_SOURCE_DIR}/${relative_path}")
    file(READ "${path}" source)
    string(FIND "${source}" "${replacement}" already_patched)
    if(NOT already_patched EQUAL -1)
        return()
    endif()
    string(FIND "${source}" "${original}" original_offset)
    if(original_offset EQUAL -1)
        message(FATAL_ERROR "Ghostty image-limit patch no longer matches ${relative_path}; review the dependency upgrade")
    endif()
    string(REPLACE "${original}" "${replacement}" patched "${source}")
    file(WRITE "${path}" "${patched}")
endfunction()

ztermy_replace_ghostty_text("src/terminal/kitty/graphics_image.zig"
    "const max_dimension = 10000;"
    "const max_dimension = 8192; // ztermy image admission policy")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_image.zig"
    "const max_size = 400 * 1024 * 1024; // 400MB"
    "const max_size = 32 * 1024 * 1024; // ztermy: loading and inflation, not only storage")

ztermy_replace_ghostty_text("src/terminal/kitty/graphics_exec.zig"
    "    const load = loadAndAddImage(io, alloc, terminal, cmd) catch |err| {"
    "    // Preserve the first chunk's response identity on final-chunk failure.\n    if (terminal.screens.active.kitty_images.loading) |loading| {\n        if (!loading.image.implicit_id) {\n            result.id = loading.image.id;\n            result.image_number = loading.image.number;\n            if (loading.display) |d| result.placement_id = d.placement_id;\n        }\n    }\n    const load = loadAndAddImage(io, alloc, terminal, cmd) catch |err| {")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_exec.zig"
    "        try loading.addData(alloc, cmd.data);"
    "        loading.addData(alloc, cmd.data) catch |err| {\n            loading.deinit(alloc);\n            alloc.destroy(loading);\n            storage.loading = null;\n            return err;\n        };")

# Unsupported animation commands still need an identifiable failure response.
# The pinned parser otherwise discards i/I for these actions, making its error
# response empty. Keep static image support explicit; do not pretend to animate.
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_command.zig"
    "pub const Command = struct {\n    control: Control,"
    "pub const Command = struct {\n    response_id: u32 = 0,\n    response_image_number: u32 = 0,\n    control: Control,")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_command.zig"
    "            .control = control,\n            .quiet = quiet,"
    "            .control = control,\n            .quiet = quiet,\n            .response_id = self.kv.get('i') orelse 0,\n            .response_image_number = self.kv.get('I') orelse 0,")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_exec.zig"
    "        => .{ .message = \"ERROR: unimplemented action\" },"
    "        => .{ .id = cmd.response_id, .image_number = cmd.response_image_number, .message = if (cmd.response_id != 0 and cmd.response_image_number != 0) \"EINVAL: specify either image id or number\" else \"ENOTSUP: animation is not supported\" },")

ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "                if (evicted > req) return true;"
    "                if (evicted >= req) return true; // ztermy: exact capacity is sufficient")

# Pixel budgets alone do not bound tiny-image metadata or tracked placements.
# Existing IDs may still be replaced at capacity; deletion restores admission.
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "        const total_bytes = self.total_bytes + img.data.len;"
    "        // ztermy: replacement consumes only its net increase in stored pixels.\n        const replaced_bytes = if (self.images.get(img.id)) |old| old.data.len else 0;\n        const total_bytes = self.total_bytes - replaced_bytes + img.data.len;")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "    fn evictImage(self: *ImageStorage, io: std.Io, alloc: Allocator, req: usize) !bool {"
    "    fn evictImage(self: *ImageStorage, io: std.Io, alloc: Allocator, req: usize) !bool {\n        return self.evictImageExcept(io, alloc, req, null);\n    }\n\n    fn evictImageExcept(self: *ImageStorage, io: std.Io, alloc: Allocator, req: usize, protected_id: ?u32) !bool {")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "            log.info(\"evicting images to make space for {} bytes\", .{req_bytes});\n            if (!try self.evictImage(io, alloc, req_bytes)) {"
    "            log.info(\"evicting images to make space for {} bytes\", .{req_bytes});\n            if (!try self.evictImageExcept(io, alloc, req_bytes, img.id)) {")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "        for (candidates) |c| {\n            // Delete all the placements for this image and the image."
    "        for (candidates) |c| {\n            if (protected_id) |id| {\n                if (c.id == id) continue;\n            }\n            // Delete all the placements for this image and the image.")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "        if (img.data.len > self.total_limit) return error.OutOfMemory;"
    "        if (img.data.len > self.total_limit) return error.OutOfMemory;\n        if (self.images.count() >= 4096 and !self.images.contains(img.id)) return error.OutOfMemory;")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "        const gop = try self.placements.getOrPut(alloc, key);"
    "        if (self.placements.count() >= 4096 and !self.placements.contains(key)) return error.OutOfMemory;\n        const gop = try self.placements.getOrPut(alloc, key);")

# Storage replaces the value but cannot release a screen-owned pin itself.
# Release the old pin only after successful replacement, not on admission failure.
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_exec.zig"
    "    storage.addPlacement(\n"
    "    const previous_placement = if (result.placement_id != 0) storage.placements.get(.{\n        .image_id = img.id,\n        .placement_id = .{ .tag = .external, .id = result.placement_id },\n    }) else null;\n    storage.addPlacement(\n")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_exec.zig"
    "    // Apply cursor movement setting. This only applies to pin placements."
    "    if (previous_placement) |previous| previous.deinit(terminal.screens.active);\n\n    // Apply cursor movement setting. This only applies to pin placements.")

# Reuse pinned engine geometry and pin-aware deletion; never duplicate either.
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "    fn evictImageExcept(self: *ImageStorage, io: std.Io, alloc: Allocator, req: usize, protected_id: ?u32) !bool {"
    "    fn evictImageExcept(self: *ImageStorage, io: std.Io, alloc: Allocator, req: usize, protected_id: ?u32) !bool {\n        if (protected_id) |id| {\n            if (@import(\"../c/ztermy_image_budget.zig\").reclaimScreen(req, id)) |recovered| return recovered;\n        }")
file(READ "${GHOSTTY_SOURCE_DIR}/src/terminal/kitty/graphics_storage.zig" budget_storage)
string(FIND "${budget_storage}" "if (!budget.reserve(extra_charge)) return error.OutOfMemory;" old_budget_call)
if(NOT old_budget_call EQUAL -1)
    ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
        "if (!budget.reserve(extra_charge)) return error.OutOfMemory;"
        "if (!budget.reserve(extra_charge, img.id)) return error.OutOfMemory;")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/ghostty/ztermy_image_budget.zig"
    "${GHOSTTY_SOURCE_DIR}/src/terminal/c/ztermy_image_budget.zig" COPYONLY)
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_image.zig"
    "pub const Image = struct {"
    "pub const Image = struct {\n    ztermy_budget_bytes: usize = 0,")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_image.zig"
    "    pub fn deinit(self: *Image, alloc: Allocator) void {"
    "    pub fn deinit(self: *Image, alloc: Allocator) void {\n        @import(\"../c/ztermy_image_budget.zig\").release(self.ztermy_budget_bytes);\n        self.ztermy_budget_bytes = 0;")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "        const total_bytes = self.total_bytes - replaced_bytes + img.data.len;"
    "        const total_bytes = self.total_bytes - replaced_bytes + img.data.len;\n        const budget = @import(\"../c/ztermy_image_budget.zig\");\n        const old_charge = if (self.images.get(img.id)) |old| old.ztermy_budget_bytes else 0;\n        const extra_charge = img.data.len -| old_charge;\n        if (!budget.reserve(extra_charge, img.id)) return error.OutOfMemory;\n        errdefer budget.release(extra_charge);")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "            self.total_bytes -= gop.value_ptr.data.len;\n            gop.value_ptr.deinit(alloc);"
    "            self.total_bytes -= gop.value_ptr.data.len;\n            gop.value_ptr.ztermy_budget_bytes = 0; // transfer the old charge to its successor\n            gop.value_ptr.deinit(alloc);")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "        gop.value_ptr.* = img;\n        self.total_bytes += img.data.len;"
    "        gop.value_ptr.* = img;\n        gop.value_ptr.ztermy_budget_bytes = img.data.len;\n        budget.release(old_charge -| img.data.len);\n        self.total_bytes += img.data.len;")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "        if (total_bytes > self.total_limit) {\n            const req_bytes = total_bytes - self.total_limit;"
    "        // Shared reclamation may already have freed pixels in this screen.\n        if (total_bytes > self.total_limit and self.total_bytes - replaced_bytes + img.data.len > self.total_limit) {\n            const req_bytes = self.total_bytes - replaced_bytes + img.data.len - self.total_limit;")
ztermy_replace_ghostty_text("src/terminal/c/kitty_graphics.zig"
    "fn computeViewportPos(" "pub fn computeViewportPos(")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "    fn deleteById(" "    pub fn deleteById(")
ztermy_replace_ghostty_text("src/terminal/kitty/graphics_storage.zig"
    "    fn markMutated(" "    pub fn markMutated(")

configure_file("${CMAKE_CURRENT_LIST_DIR}/ghostty/ztermy_image.zig"
    "${GHOSTTY_SOURCE_DIR}/src/terminal/c/ztermy_image.zig" COPYONLY)
# Normalize only our own export lines. Independent insert-before patches sharing
# one anchor otherwise reinsert exports when another patch changes adjacency.
set(exports_path "${GHOSTTY_SOURCE_DIR}/src/lib_vt.zig")
file(READ "${exports_path}" original_exports)
set(updated_exports "${original_exports}")
set(owned_exports "")
foreach(entry IN ITEMS "insert|insert_image" "unicodePlacements|unicode_placements"
                       "imageInventory|image_inventory" "evictImage|evict_image"
                       "configureImageBudget|configure_image_budget" "imageBudgetUsage|image_budget_usage"
                       "exchangeImageReclaimer|exchange_image_reclaimer")
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 function_name)
    list(GET parts 1 export_name)
    set(export_line "        @export(&@import(\"terminal/c/ztermy_image.zig\").${function_name}, .{ .name = \"ztermy_ghostty_${export_name}\" });\n")
    string(REPLACE "${export_line}" "" updated_exports "${updated_exports}")
    string(APPEND owned_exports "${export_line}")
endforeach()
set(export_anchor "        @export(&c.terminal_vt_write, .{ .name = \"ghostty_terminal_vt_write\" });")
string(FIND "${updated_exports}" "${export_anchor}" export_offset)
if(export_offset EQUAL -1)
    message(FATAL_ERROR "Ghostty extension export anchor changed; review the dependency upgrade")
endif()

string(REPLACE "${export_anchor}" "${owned_exports}${export_anchor}" updated_exports "${updated_exports}")
if(NOT updated_exports STREQUAL original_exports)
    file(WRITE "${exports_path}" "${updated_exports}")
endif()

# Migrate the earlier build dependency when reusing a local checkout.
set(wrapper_path "${GHOSTTY_SOURCE_DIR}/CMakeLists.txt")
file(READ "${wrapper_path}" wrapper)
string(REPLACE
    "    DEPENDS \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_image.zig\"\n"
    "    DEPENDS \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_image.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_exec.zig\"\n"
    updated_wrapper "${wrapper}")
string(REPLACE
    "    DEPENDS \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_image.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_exec.zig\"\n"
    "    DEPENDS \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_image.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_exec.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/lib_vt.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/c/ztermy_image.zig\"\n"
    updated_wrapper "${updated_wrapper}")
string(REPLACE
    "\"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/c/ztermy_image.zig\"\n"
    "\"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/c/ztermy_image.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_storage.zig\"\n"
    updated_wrapper "${updated_wrapper}")
string(REPLACE
    "\"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_storage.zig\"\n"
    "\"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_storage.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_command.zig\"\n"
    updated_wrapper "${updated_wrapper}")
string(REPLACE
    "\"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_command.zig\"\n"
    "\"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_command.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/c/ztermy_image_budget.zig\"\n"
    updated_wrapper "${updated_wrapper}")
if(NOT updated_wrapper STREQUAL wrapper)
    file(WRITE "${wrapper_path}" "${updated_wrapper}")
endif()

# Upstream's CMake wrapper otherwise never rebuilds an existing archive when a
# Zig source changes. This dependency is specific to our patched policy file.
ztermy_replace_ghostty_text("CMakeLists.txt"
    "    COMMENT \"Building libghostty-vt via zig build...\""
    "    DEPENDS \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_image.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_exec.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/lib_vt.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/c/ztermy_image.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_storage.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_command.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/c/ztermy_image_budget.zig\"\n    COMMENT \"Building libghostty-vt via zig build...\"")
