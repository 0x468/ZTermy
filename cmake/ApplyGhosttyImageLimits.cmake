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

# Pixel budgets alone do not bound tiny-image metadata or tracked placements.
# Existing IDs may still be replaced at capacity; deletion restores admission.
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

configure_file("${CMAKE_CURRENT_LIST_DIR}/ghostty/ztermy_image.zig"
    "${GHOSTTY_SOURCE_DIR}/src/terminal/c/ztermy_image.zig" COPYONLY)
ztermy_replace_ghostty_text("src/lib_vt.zig"
    "        @export(&c.terminal_vt_write, .{ .name = \"ghostty_terminal_vt_write\" });"
    "        @export(&@import(\"terminal/c/ztermy_image.zig\").insert, .{ .name = \"ztermy_ghostty_insert_image\" });\n        @export(&c.terminal_vt_write, .{ .name = \"ghostty_terminal_vt_write\" });")
ztermy_replace_ghostty_text("src/lib_vt.zig"
    "        @export(&@import(\"terminal/c/ztermy_image.zig\").insert, .{ .name = \"ztermy_ghostty_insert_image\" });"
    "        @export(&@import(\"terminal/c/ztermy_image.zig\").unicodePlacements, .{ .name = \"ztermy_ghostty_unicode_placements\" });\n        @export(&@import(\"terminal/c/ztermy_image.zig\").insert, .{ .name = \"ztermy_ghostty_insert_image\" });")

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
if(NOT updated_wrapper STREQUAL wrapper)
    file(WRITE "${wrapper_path}" "${updated_wrapper}")
endif()

# Upstream's CMake wrapper otherwise never rebuilds an existing archive when a
# Zig source changes. This dependency is specific to our patched policy file.
ztermy_replace_ghostty_text("CMakeLists.txt"
    "    COMMENT \"Building libghostty-vt via zig build...\""
    "    DEPENDS \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_image.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_exec.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/lib_vt.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/c/ztermy_image.zig\" \"\${CMAKE_CURRENT_SOURCE_DIR}/src/terminal/kitty/graphics_storage.zig\"\n    COMMENT \"Building libghostty-vt via zig build...\"")
