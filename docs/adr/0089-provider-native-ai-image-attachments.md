# ADR 0089: Provider-native AI image attachments

Status: Accepted

Date: 2026-08-15

## Context

Terminal troubleshooting often depends on screenshots of dashboards, error
dialogs, topology diagrams, and rendered output that cannot be represented by a
terminal selection. NetCatty and mainstream multimodal assistants expose images
as explicit message attachments. The provider protocols are not identical:
OpenAI Responses uses `input_image`, OpenAI-compatible chat uses `image_url`,
Anthropic uses a Base64 image source, and Ollama uses the message `images`
array.

Treating images as text-file context would inflate token estimates, expose raw
Base64 in the context preview, and lose provider-native vision behavior.

## Decision

- Images are explicit draft attachments owned by the active terminal sidebar.
  They are never discovered from the filesystem or another terminal.
- Load and decode on a worker thread. Accept PNG, JPEG, WebP, and GIF by decoded
  content rather than filename, with limits of four images, 5 MiB per image,
  12 MiB per draft, and 40 megapixels per decoded image.
- Generate a bounded local preview and show removable thumbnails in the draft
  context row and user message. Sending an image-only message is valid.
- Serialize the current user turn using each provider's native multimodal
  request shape. Image token estimation uses a provider-neutral 512-pixel tile
  approximation instead of counting Base64 characters as text.
- Keep binary image bytes only in the current in-memory message. Follow-up
  requests and encrypted transcript history retain a filename, media type, and
  size omission marker, not the original binary payload. Restoring a history
  item also clears any unrelated draft attachments.
- Debug traces retain the provider request structure and image metadata but
  replace image Base64/data-URL payloads with their encoded length. This keeps
  the trace useful without producing multi-megabyte duplicate logs.
- Do not guess whether a model supports vision. The selected provider/model
  receives its native request, and an unsupported-model response is surfaced as
  an ordinary provider error.

## Consequences

- One implementation supports the four existing provider families without
  provider-specific UI or a Web runtime.
- Draft ingestion and preview generation do not block the GUI/render thread.
- A restored conversation explains that an earlier image existed but cannot
  resend it implicitly; the user can attach it again when the next turn needs
  visual evidence.
- Animated GIF input is preserved for providers that accept it, while the local
  thumbnail displays the decoded preview frame.
