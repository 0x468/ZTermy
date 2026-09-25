#pragma once

#include <QByteArrayView>
#include <QImage>

namespace ztermy::terminal
{
// In-memory only; runs on the terminal worker, never performs file/network I/O.
[[nodiscard]] QImage decodeTerminalPng(QByteArrayView bytes);
// Call once during startup, before any terminal workers are created.
[[nodiscard]] bool installTerminalPngDecoder();
} // namespace ztermy::terminal
