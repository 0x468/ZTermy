# Application resources

This directory separates editable brand sources from platform integration files and generated build artifacts.

## Layout

- `branding/` contains the editable, platform-neutral SVG masters. These files are the source of truth.
- `icons/` contains the monochrome 20x20 SVG masters used by `AppIcon`; see its README for the icon contract.
- `windows/` contains Windows resource templates and the application manifest. It must not contain hand-edited raster copies of the brand.
- `<build preset>/generated/branding/` contains derived PNG layers.
- `<build preset>/generated/ztermy.ico` is the derived multi-resolution Windows icon.

The generated files are intentionally kept out of source control. Build the `ztermy_branding_assets` target to regenerate them from the SVG masters.

## Output policy

- Use the simplified app-icon master at 16 px and 20 px.
- Use the full inset-prompt master at 24 px and larger.
- Generate ICO layers at 16, 20, 24, 32, 40, 48, 64, 128, and 256 px.
- Never edit PNG or ICO output directly; update the SVG master and regenerate.
