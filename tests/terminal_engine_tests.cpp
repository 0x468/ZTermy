#include "domain/terminal/GhosttyTerminalEngine.h"
#include "domain/terminal/ShellPathQuoter.h"
#include "domain/terminal/SixelDecoder.h"
#include "domain/terminal/TerminalGraphicsStream.h"
#include "infrastructure/terminal/TerminalPngDecoder.h"

#include <QBuffer>
#include <QTest>
#include <QtEndian>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace
{

class GraphicsSinkFixture final : public ztermy::terminal::TerminalGraphicsSink
{
public:
    void writeTerminal(std::string_view bytes) override
    {
        terminal.append(bytes);
        transcript.append(bytes);
        ++writes;
    }
    void beginSixel(std::string_view parameters) override
    {
        headers.emplace_back(parameters);
        payloads.emplace_back();
    }
    void writeSixel(std::string_view bytes) override { payloads.back().append(bytes); }
    void endSixel(bool cancelled) override
    {
        cancellations.push_back(cancelled);
        transcript += "<image>";
    }
    void controlSequence(std::string_view value) override
    {
        controls.emplace_back(value);
        controlOffsets.push_back(terminal.size());
    }
    void resetTerminal() override { ++resets; }
    std::string terminal;
    std::string transcript;
    std::vector<std::string> headers;
    std::vector<std::string> payloads;
    std::vector<bool> cancellations;
    std::vector<std::string> controls;
    std::vector<std::size_t> controlOffsets;
    int resets = 0;
    int writes = 0;
};

class TerminalEngineTests final : public QObject
{
    Q_OBJECT

private slots:
    void separatesSixelWithoutChangingOtherVtOrUtf8();
    void recoversFromCancelledOrIncompleteGraphicsStrings();
    void observesGraphicsModesWithoutConsumingTerminalControls();
    void placesSixelAndPreservesSavedCursorAcrossScrolling();
    void scalesSixelPixelsWithoutChangingTransparency();
    void rejectsInflationBeyondImageBudgetAndRecovers();
    void releasesRejectedMultipartImageBeforeNextTransmission();
    void keepsKittyUploadIndependentOfSixelOutput();
    void placesAbsoluteSixelWithoutChangingCursorOrWrap();
    void reportsSixelCapabilitiesWithoutChangingScreen();
    void decodesFragmentedSixelOverprintsAndDecColors();
    void preservesSixelBackgroundAndRasterExtent();
    void boundsSixelRepeatAndOverdrawWork();
    void decodesPngThroughKittyWithoutPremultiplyingAlpha();
    void rejectsOversizedAndMalformedPngBeforeRasterDecode();
    void retainsKittyPixelsAcrossReplacementAndDeletion();
    void tracksKittyPlacementAcrossScreenAndScrollChanges();
    void rendersUnicodeImageFragmentsWithInheritedCoordinates();
    void resolvesUnicodeImageIdentityAndReleasesSnapshotPixels();
    void enlargesSinglePixelUnicodeImageAcrossRows();
    void boundsImageMetadataAndAllowsRecovery();
    void preservesBlinkAttributesWithoutChangingText();
    void retainsProgressAcrossFragmentedReports();
    void boundsAndThrottlesProgramNotifications();
    void rejectsInvalidGeometry();
    void tracksSynchronizedOutputMode();
    void parsesSplitVtSequences();
    void preservesContentAcrossResize();
    void exposesImmutableStyledCells();
    void appliesColorSchemeToDefaultsAndPalette();
    void updatesPaletteUnderlineColorAfterThemeAndOscChange();
    void exposesWideCellAndCursorWidth();
    void preservesPrimaryScreenAcrossAlternateScreen();
    void normalizesWideCellSelection();
    void handlesEraseAndCursorVisibility();
    void exposesCombiningAndEmojiGraphemes();
    void reportsAndResetsRenderDamage();
    void selectsAndFormatsViewportText();
    void navigatesCopyModeWithTerminalOwnedSelection();
    void appliesTrackedWordSelectionGestures();
    void autoscrollsTrackedSelectionWithoutDoubleScrolling();
    void selectsCompleteScrollback();
    void scrollsThroughHistory();
    void searchesAcrossScrollbackAndWrappedLines();
    void encodesPasteForTerminalMode();
    void encodesKeysFromLiveTerminalModes();
    void doesNotScrollWhenNushellClearsToViewportBottom();
    void encodesMouseAndFocusEventsFromLiveTerminalModes();
    void reportsAlternateScrollMode();
    void exposesShellWorkingDirectorySequences();
    void preservesTransientTitlesAcrossFragmentedOutput();
    void exposesExplicitOsc8Hyperlinks();
    void detectsAutomaticHttpLinksWithoutOverridingOsc8();
    void drainsBoundedOsc52ClipboardWrites();
    void returnsTerminalQueryResponses();
    void reportsCurrentLightOrDarkColorScheme();
    void reportsConfiguredBackgroundColorToShell();
    void notifiesSubscribedShellWhenColorSchemeChanges();
    void reportsCurrentTerminalDimensions();
    void pagesThroughScrollback();
    void quotesDroppedPathsForShellDialects();
};

void TerminalEngineTests::separatesSixelWithoutChangingOtherVtOrUtf8()
{
    const std::string before = "before\x1b]2;title q#1~\x07\x1b_Ga=q,i=7;AAAA\x1b\\\x1bP$qm\x1b\\"
                               "\xf0\x9f\x90\x9c\x1b[31m";
    const std::string payload = "\"1;1;2;6#1;2;100;0;0!2~";
    const std::string after = "after\xc2\xa2\x1b[0m";
    for (const bool c1 : {false, true})
    {
        auto input = before;
        input.append(c1 ? "\x90" : "\x1bP")
            .append("0;1;0q")
            .append(payload)
            .append(c1 ? "\x9c" : "\x1b\\")
            .append(after);
        auto transcript = before;
        transcript.append("<image>").append(after);
        for (std::size_t chunk = 1; chunk <= input.size(); ++chunk)
        {
            ztermy::terminal::TerminalGraphicsStream stream;
            GraphicsSinkFixture sink;
            for (std::size_t offset = 0; offset < input.size(); offset += chunk)
                stream.append(std::string_view(input).substr(offset, chunk), sink);
            stream.finish(sink);
            QCOMPARE(sink.terminal, before + after);
            QCOMPARE(sink.transcript, transcript);
            QCOMPARE(sink.headers, std::vector<std::string>({"0;1;0"}));
            QCOMPARE(sink.payloads, std::vector<std::string>({payload}));
            QCOMPARE(sink.cancellations, std::vector<bool>({false}));
        }
    }
    ztermy::terminal::TerminalGraphicsStream stream;
    GraphicsSinkFixture sink;
    const std::string plain(std::size_t{64} * 1024, 'A');
    stream.append(plain, sink);
    QCOMPARE(sink.terminal, plain);
    QCOMPARE(sink.writes, 1);
}

void TerminalEngineTests::recoversFromCancelledOrIncompleteGraphicsStrings()
{
    const std::string input = "A\x1bPq~\x18tail\x1bPq!2~\x1b[31mred\x1bPq!";
    for (std::size_t chunk = 1; chunk <= input.size(); ++chunk)
    {
        ztermy::terminal::TerminalGraphicsStream stream;
        GraphicsSinkFixture sink;
        for (std::size_t offset = 0; offset < input.size(); offset += chunk)
            stream.append(std::string_view(input).substr(offset, chunk), sink);
        stream.finish(sink);
        QCOMPARE(sink.terminal, std::string("A\x18tail\x1b[31mred"));
        QCOMPARE(sink.cancellations, std::vector<bool>({true, true, true}));
        QCOMPARE(sink.payloads, std::vector<std::string>({"~", "!2~", "!"}));
    }
    for (const auto &passthrough : {std::string("\x1bP") + std::string(150, '1') + "qanything\x1b\\after",
                                    std::string("\x1bP1;2"), std::string("hello\x1b"),
                                    std::string("\x1bP12\x9c"
                                                "after")})
    {
        ztermy::terminal::TerminalGraphicsStream stream;
        GraphicsSinkFixture sink;
        for (const char &byte : passthrough)
            stream.append({&byte, 1}, sink);
        stream.finish(sink);
        QCOMPARE(sink.terminal, passthrough);
        QVERIFY(sink.headers.empty());
    }
}

void TerminalEngineTests::observesGraphicsModesWithoutConsumingTerminalControls()
{
    const std::string prefix = "A\x1b[?80;8452h";
    const std::string input = prefix
                              + "\x9b?80l\x1b[?84\x07"
                                "52r"
                                "\x1b[?80\x18h\x1b]2;fake\x1b[?80h\x07"
                                "\x1bP$q?80h\x1b\\\xf0\x9f\x9b\x80"
                                "\x1b["
                              + std::string(160, '1')
                              + "h\x1b"
                                "c\x1b[!p\x1b[?8\x9b?8452l\x1b[?80";
    for (std::size_t chunk = 1; chunk <= input.size(); ++chunk)
    {
        ztermy::terminal::TerminalGraphicsStream stream;
        GraphicsSinkFixture sink;
        for (std::size_t offset = 0; offset < input.size(); offset += chunk)
            stream.append(std::string_view(input).substr(offset, chunk), sink);
        stream.finish(sink);
        QCOMPARE(sink.terminal, input);
        QCOMPARE(sink.controls, std::vector<std::string>({"?80;8452h", "?80l", "?8452r", "!p", "?8452l"}));
        QCOMPARE(sink.controlOffsets.front(), prefix.size());
        QCOMPARE(sink.resets, 1);
        QVERIFY(sink.headers.empty());
    }
}

void TerminalEngineTests::placesSixelAndPreservesSavedCursorAcrossScrolling()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 10, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 6});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](std::string_view value) {
        return !engine.feed(std::as_bytes(std::span(value)));
    };
    QVERIFY(feed("\x1b[1;2H\x1b"
                 "7\x1b[3;3H"));
    const std::string_view sixel = "\x1bP0;1q\"1;1;16;12#1;2;100;0;0!16~-!16~\x1b\\";
    for (const char &byte : sixel)
        QVERIFY(feed({&byte, 1}));
    auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->images.size(), std::size_t{1});
    QCOMPARE(snapshot->images[0].column, 2);
    QCOMPARE(snapshot->images[0].row, 1); // Two output rows scroll once at the bottom.
    QCOMPARE(snapshot->images[0].width, std::uint32_t{16});
    QCOMPARE(snapshot->images[0].height, std::uint32_t{12});
    QCOMPARE(snapshot->images[0].image->pixels.size(), std::size_t{16} * 12 * 4);
    QCOMPARE(snapshot->images[0].image->pixels[0], std::uint8_t{255});
    QCOMPARE(snapshot->cursor.column, std::uint16_t{2});
    QCOMPARE(snapshot->cursor.row, std::uint16_t{3});
    QVERIFY(feed("X\x1b"
                 "8"));
    snapshot = engine.snapshot();
    QCOMPARE(snapshot->cursor.column, std::uint16_t{1});
    QCOMPARE(snapshot->cursor.row, std::uint16_t{0});
    QVERIFY(engine.takePtyWrite().empty());

    // Mode 8452 places subsequent text to the right on the last image row.
    QVERIFY(feed("\x1b[?8452h\x1b[1;3H"));
    QVERIFY(feed(sixel));
    snapshot = engine.snapshot();
    QCOMPARE(snapshot->cursor.column, std::uint16_t{4});
    QCOMPARE(snapshot->cursor.row, std::uint16_t{1});
    QCOMPARE(snapshot->images.size(), std::size_t{2});
    QVERIFY(feed("\x1b[?1049h"));
    QVERIFY(engine.snapshot()->images.empty());
    QVERIFY(feed("\x1b[?1049l"));
    QCOMPARE(engine.snapshot()->images.size(), std::size_t{2});
}

void TerminalEngineTests::scalesSixelPixelsWithoutChangingTransparency()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 20, .rows = 24, .cellWidthPixels = 8, .cellHeightPixels = 6});
    QVERIFY(created);
    auto &engine = **created;
    const std::string_view input = "\x1bP0;1q\"2;1;64;30#1;2;100;0;0!64~\x1b\\";
    QVERIFY(!engine.feed(std::as_bytes(std::span(input))));
    const auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->images.size(), std::size_t{1});
    const auto &image = *snapshot->images[0].image;
    QCOMPARE(image.width, std::uint32_t{64});
    QCOMPARE(image.height, std::uint32_t{60});
    QCOMPARE(image.pixels.size(), std::size_t{64} * 60 * 4);
    for (std::size_t pixel = 0; pixel < image.pixels.size() / 4; ++pixel)
    {
        const bool red = pixel / 64 < 12; // Six lit rows, each doubled in height.
        QCOMPARE(image.pixels[pixel * 4], static_cast<std::uint8_t>(red ? 255 : 0));
        QCOMPARE(image.pixels[pixel * 4 + 3], static_cast<std::uint8_t>(red ? 255 : 0));
    }
    QCOMPARE(snapshot->cursor.row, std::uint16_t{10});
    QVERIFY(engine.takePtyWrite().empty());
}

void TerminalEngineTests::rejectsInflationBeyondImageBudgetAndRecovers()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 10, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 16});
    QVERIFY(created);
    auto &engine = **created;
    const auto transmit = [&](int width, int height, int id) {
        const QByteArray raster(qsizetype{width} * height * 4, '\0');
        // qCompress prefixes the uncompressed length; Kitty expects only zlib.
        const QByteArray payload = qCompress(raster, 9).sliced(4).toBase64();
        for (qsizetype offset = 0; offset < payload.size(); offset += 4096)
        {
            std::string command = offset == 0 ? "\x1b_Ga=T,f=32,o=z,C=1,c=1,r=1,i=" + std::to_string(id) + ",s="
                                                    + std::to_string(width) + ",v=" + std::to_string(height) + ",m="
                                              : "\x1b_Gm=";
            command += offset + 4096 < payload.size() ? "1;" : "0;";
            const auto chunk =
                QByteArrayView(payload).sliced(offset, std::min(qsizetype{4096}, payload.size() - offset));
            command.append(chunk.data(), static_cast<std::size_t>(chunk.size())).append("\x1b\\");
            QVERIFY(!engine.feed(std::as_bytes(std::span(command))));
        }
    };
    transmit(4096, 2049, 901); // Valid RGBA raster, one row beyond 32 MiB.
    QVERIFY(engine.snapshot()->images.empty());
    const auto rejected = engine.takePtyWrite();
    QVERIFY(!rejected.empty());
    const std::string reply(reinterpret_cast<const char *>(rejected.data()), rejected.size());
    // Storage-limit rejection would be ENOMEM after allocating the whole raster.
    // Require rejection by the inflater, not merely absence of a placement.
    QVERIFY(reply.find("decompression failed") != std::string::npos);
    transmit(2, 2, 902);
    QCOMPARE(engine.snapshot()->images.size(), std::size_t{1});
    const std::string_view text = "healthy";
    QVERIFY(!engine.feed(std::as_bytes(std::span(text))));
    QCOMPARE(engine.snapshot()->cursor.column, std::uint16_t{7});
}

void TerminalEngineTests::releasesRejectedMultipartImageBeforeNextTransmission()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 10, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 16});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](std::string_view value) {
        return !engine.feed(std::as_bytes(std::span(value)));
    };
    QVERIFY(feed("\x1b_Ga=T,f=32,s=4096,v=2049,i=903,m=1;\x1b\\"));
    const std::string chunk = "\x1b_Gm=1;" + std::string(4096, 'A') + "\x1b\\";
    const std::size_t chunksUntilOverflow = (std::size_t{32} * 1024 * 1024) / 3072 + 1;
    for (std::size_t index = 0; index < chunksUntilOverflow; ++index)
        QVERIFY(feed(chunk));
    const auto rejected = engine.takePtyWrite();
    const std::string reply(reinterpret_cast<const char *>(rejected.data()), rejected.size());
    QVERIFY(reply.find("i=903") != std::string::npos);
    QVERIFY(reply.find("invalid data") != std::string::npos);
    // No final m=0 was sent: rejection itself must release the active loader.
    QVERIFY(feed("\x1b_Ga=T,f=32,s=1,v=1,i=904,C=1;/wAA/w==\x1b\\"));
    const auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->images.size(), std::size_t{1});
    QCOMPARE(snapshot->images[0].image->pixels, std::vector<std::uint8_t>({255, 0, 0, 255}));
}

void TerminalEngineTests::keepsKittyUploadIndependentOfSixelOutput()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 10, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 16});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](std::string_view value) {
        return !engine.feed(std::as_bytes(std::span(value)));
    };
    // A blue RGB image is only half transmitted when a red Sixel arrives.
    QVERIFY(feed("\x1b_Ga=T,f=24,s=2,v=1,i=905,C=1,m=1;AAD/\x1b\\"));
    QVERIFY(feed("\x1bP0;1q\"1;1;1;6#1;2;100;0;0~\x1b\\"));
    auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->images.size(), std::size_t{1});
    QCOMPARE(snapshot->images[0].image->pixels[0], std::uint8_t{255});
    QVERIFY(feed("\x1b_Gm=0;AAD/\x1b\\"));
    snapshot = engine.snapshot();
    QCOMPARE(snapshot->images.size(), std::size_t{2});
    const auto blue = std::find_if(snapshot->images.begin(), snapshot->images.end(), [](const auto &placement) {
        return placement.imageId == 905;
    });
    QVERIFY(blue != snapshot->images.end());
    QCOMPARE(blue->image->pixels, std::vector<std::uint8_t>({0, 0, 255, 0, 0, 255}));
}

void TerminalEngineTests::placesAbsoluteSixelWithoutChangingCursorOrWrap()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 4, .rows = 3, .cellWidthPixels = 8, .cellHeightPixels = 6});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](std::string_view value) {
        return !engine.feed(std::as_bytes(std::span(value)));
    };
    QVERIFY(feed("\x1b[?80h\x1b[2;4HX")); // Pending soft wrap, not just a column number.
    QVERIFY(feed("\x1bP0;1q\"1;1;64;30#1;2;100;0;0!64~\x1b\\"));
    auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->images.size(), std::size_t{1});
    QCOMPARE(snapshot->images[0].column, 0);
    QCOMPARE(snapshot->images[0].row, 0);
    QCOMPARE(snapshot->images[0].width, std::uint32_t{32});
    QCOMPARE(snapshot->images[0].height, std::uint32_t{18});
    QCOMPARE(snapshot->cursor.column, std::uint16_t{3});
    QCOMPARE(snapshot->cursor.row, std::uint16_t{1});
    QVERIFY(feed("Y"));
    snapshot = engine.snapshot();
    QCOMPARE(snapshot->cursor.column, std::uint16_t{1});
    QCOMPARE(snapshot->cursor.row, std::uint16_t{2});
}

void TerminalEngineTests::reportsSixelCapabilitiesWithoutChangingScreen()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 10, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 16});
    QVERIFY(created);
    auto &engine = **created;
    const std::string_view queries = "\x1b[c\x1b[?1;1S\x1b[?2;1S\x1b[?2;4S\x1b[?3;1S\x1b[?1;3;16S\x1b[6n";
    for (const char &byte : queries)
        QVERIFY(!engine.feed(std::as_bytes(std::span(&byte, 1))));
    const auto replies = engine.takePtyWrite();
    QCOMPARE(std::string(reinterpret_cast<const char *>(replies.data()), replies.size()),
             std::string("\x1b[?62;4;22c\x1b[?1;0;256S\x1b[?2;0;80;64S\x1b[?2;0;8192;8192S"
                         "\x1b[?3;1S\x1b[?1;3S\x1b[1;1R"));
    const auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->cursor.column, std::uint16_t{0});
    QCOMPARE(snapshot->cursor.row, std::uint16_t{0});
    QVERIFY(snapshot->images.empty());
    QVERIFY(engine.takePtyWrite().empty());
}

void TerminalEngineTests::decodesFragmentedSixelOverprintsAndDecColors()
{
    const std::string_view payload = "\"1;1;3;2#1;2;100;0;0!2@?$#2;2;0;100;0A-#3;1;0;50;100B";
    for (std::size_t chunk = 1; chunk <= payload.size(); ++chunk)
    {
        ztermy::terminal::SixelDecoder decoder({.transparent = true});
        for (std::size_t offset = 0; offset < payload.size(); offset += chunk)
            QVERIFY(decoder.append(payload.substr(offset, chunk)));
        const auto result = decoder.finish();
        QVERIFY(result);
        QCOMPARE(result->image.width, std::uint32_t{3});
        QCOMPARE(result->image.height, std::uint32_t{12});
        QCOMPARE(result->options.aspectNumerator, std::uint32_t{1});
        QCOMPARE(result->cursorX, std::uint32_t{1});
        QCOMPARE(result->cursorY, std::uint32_t{6});
        const auto pixel = [&](std::size_t x, std::size_t y) {
            const auto *p = result->image.pixels.data() + (y * 3 + x) * 4;
            return QColor(p[0], p[1], p[2], p[3]);
        };
        QCOMPARE(pixel(0, 0), QColor(Qt::red));
        QCOMPARE(pixel(1, 0), QColor(Qt::red));
        QCOMPARE(pixel(0, 1), QColor(Qt::green));
        QCOMPARE(pixel(2, 0).alpha(), 0);
        QCOMPARE(pixel(0, 6), QColor(Qt::blue)); // DEC HLS hue 0 is blue, not red.
        QCOMPARE(pixel(0, 7), QColor(Qt::blue));
    }
}

void TerminalEngineTests::preservesSixelBackgroundAndRasterExtent()
{
    ztermy::terminal::SixelDecoder decoder({.background = {.red = 7, .green = 8, .blue = 9}});
    QVERIFY(decoder.append("\"2;1;4;8#7;2;100;0;0@"));
    const auto result = decoder.finish();
    QVERIFY(result);
    QCOMPARE(result->image.width, std::uint32_t{4});
    QCOMPARE(result->image.height, std::uint32_t{8});
    QCOMPARE(result->options.aspectNumerator, std::uint32_t{2});
    QCOMPARE(result->image.pixels[0], std::uint8_t{255});
    QCOMPARE(result->image.pixels[4], std::uint8_t{7});
    QCOMPARE(result->image.pixels[5], std::uint8_t{8});
    QCOMPARE(result->image.pixels[6], std::uint8_t{9});
    QCOMPARE(result->image.pixels[7], std::uint8_t{255});
    QCOMPARE(result->image.pixels.back(), std::uint8_t{255});
    QVERIFY(!decoder.append("@"));
    QVERIFY(!decoder.finish());

    // Grow beyond a large declared raster after writing a pixel. Earlier rows
    // must survive any change of row stride, without retaining unused capacity.
    ztermy::terminal::SixelDecoder growth({.transparent = true});
    QVERIFY(growth.append("\"1;1;5000;1000#1;2;100;0;0@"));
    QVERIFY(growth.append(std::string(167, '-')));
    QVERIFY(growth.append("#2;2;0;0;100@"));
    const auto grown = growth.finish();
    QVERIFY(grown);
    QCOMPARE(grown->image.width, std::uint32_t{5000});
    QCOMPARE(grown->image.height, std::uint32_t{1008});
    QCOMPARE(grown->image.pixels[0], std::uint8_t{255});
    QCOMPARE(grown->image.pixels[3], std::uint8_t{255});
    const auto last = std::size_t{1002} * 5000 * 4;
    QCOMPARE(grown->image.pixels[last + 2], std::uint8_t{255});
    QCOMPARE(grown->image.pixels[last + 3], std::uint8_t{255});
}

void TerminalEngineTests::boundsSixelRepeatAndOverdrawWork()
{
    using ztermy::terminal::SixelDecoder;
    using ztermy::terminal::SixelError;
    SixelDecoder hugeRepeat;
    QVERIFY(!hugeRepeat.append("!4294967295~"));
    QCOMPARE(hugeRepeat.finish().error(), SixelError::limit);
    SixelDecoder integerOverflow;
    QVERIFY(!integerOverflow.append("!42949672960"));
    QCOMPARE(integerOverflow.finish().error(), SixelError::limit);
    SixelDecoder hugeRaster;
    QVERIFY(!hugeRaster.append("\"1;1;8192;8192@"));
    QCOMPARE(hugeRaster.finish().error(), SixelError::limit);
    SixelDecoder missingRepeat;
    QVERIFY(missingRepeat.append("!42"));
    QCOMPARE(missingRepeat.finish().error(), SixelError::invalid);
    SixelDecoder work;
    bool rejected = false;
    // Repainting the same short row must not evade the work budget merely
    // because the final bitmap is small (a few KiB of compressed input).
    for (int i = 0; i < 1500 && !rejected; ++i)
        rejected = !work.append("!8192~$");
    QVERIFY(rejected);
    QCOMPARE(work.finish().error(), SixelError::limit);
}

QByteArray pngFixture()
{
    QImage source(2, 1, QImage::Format_RGBA8888);
    source.setPixelColor(0, 0, QColor(240, 30, 80, 128));
    source.setPixelColor(1, 0, QColor(10, 200, 50, 255));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !source.save(&buffer, "PNG"))
        return {};
    return bytes;
}

void updatePngChunkCrc(QByteArray &png, qsizetype offset)
{
    const auto size = qFromBigEndian<quint32>(png.constData() + offset);
    quint32 crc = 0xffffffffU;
    for (qsizetype index = offset + 4; index < offset + 8 + size; ++index)
    {
        crc ^= static_cast<unsigned char>(png[index]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    qToBigEndian(crc ^ 0xffffffffU, png.data() + offset + 8 + size);
}

void TerminalEngineTests::decodesPngThroughKittyWithoutPremultiplyingAlpha()
{
    QVERIFY(ztermy::terminal::installTerminalPngDecoder());
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 10, .rows = 3, .cellWidthPixels = 8, .cellHeightPixels = 16});
    QVERIFY(created);
    const auto png = pngFixture();
    QVERIFY(!png.isEmpty());
    const QByteArray command = QByteArray("\x1b_Ga=T,f=100,i=74,c=2,r=1;") + png.toBase64() + "\x1b\\";
    auto &engine = **created;
    for (qsizetype offset = 0; offset < command.size(); offset += 7)
    {
        const auto fragment = QByteArrayView(command).sliced(offset, std::min(qsizetype{7}, command.size() - offset));
        QVERIFY(!engine.feed(std::as_bytes(std::span(fragment.data(), static_cast<std::size_t>(fragment.size())))));
    }
    const auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->images.size(), std::size_t{1});
    QCOMPARE(snapshot->images[0].image->width, std::uint32_t{2});
    QCOMPARE(snapshot->images[0].image->height, std::uint32_t{1});
    QCOMPARE(snapshot->images[0].image->format, ztermy::terminal::TerminalImageFormat::rgba);
    QCOMPARE(snapshot->images[0].image->pixels, std::vector<std::uint8_t>({240, 30, 80, 128, 10, 200, 50, 255}));
}

void TerminalEngineTests::rejectsOversizedAndMalformedPngBeforeRasterDecode()
{
    const auto valid = pngFixture();
    QVERIFY(!valid.isEmpty());
    const auto expected = ztermy::terminal::decodeTerminalPng(valid);
    QVERIFY(!expected.isNull());
    const auto encode = [](const QImage &image) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
            return QByteArray{};
        return bytes;
    };
    // These are valid images, not mismatched IHDR/IDAT fixtures. Removing the
    // admission limits would make decoding succeed, so failure is meaningful.
    QImage wide(8193, 1, QImage::Format_Grayscale8);
    wide.fill(128);
    const auto widePng = encode(wide);
    QVERIFY(!widePng.isEmpty());
    QVERIFY(ztermy::terminal::decodeTerminalPng(widePng).isNull());
    QImage large(8192, 1025, QImage::Format_Grayscale8);
    large.fill(128);
    const auto largePng = encode(large);
    QVERIFY(!largePng.isEmpty());
    QVERIFY(ztermy::terminal::decodeTerminalPng(largePng).isNull());
    auto oversized = valid;
    qToBigEndian(quint32{8193}, oversized.data() + 16);
    updatePngChunkCrc(oversized, 8);
    QVERIFY(ztermy::terminal::decodeTerminalPng(oversized).isNull());
    qToBigEndian(quint32{8192}, oversized.data() + 16);
    qToBigEndian(quint32{8192}, oversized.data() + 20);
    updatePngChunkCrc(oversized, 8);
    QVERIFY(ztermy::terminal::decodeTerminalPng(oversized).isNull());
    auto malformed = valid;
    qToBigEndian(quint32{0xffffffffU}, malformed.data() + 8);
    QVERIFY(ztermy::terminal::decodeTerminalPng(malformed).isNull());
    QVERIFY(ztermy::terminal::decodeTerminalPng(QByteArrayView(valid).first(valid.size() - 1)).isNull());

    // Valid compressed metadata can expand far beyond its chunk length; it is not image data.
    const QByteArray compressed = qCompress(QByteArray(qsizetype{2} * 1024 * 1024, 'x'), 9).mid(4);
    const QByteArray payload = QByteArray("Comment\0\0", 9) + compressed;
    QByteArray chunk(12 + payload.size(), '\0');
    qToBigEndian(static_cast<quint32>(payload.size()), chunk.data());
    chunk.replace(4, 4, "zTXt");
    chunk.replace(8, payload.size(), payload);
    updatePngChunkCrc(chunk, 0);
    auto withMetadata = valid;
    withMetadata.insert(33, chunk);
    QCOMPARE(ztermy::terminal::decodeTerminalPng(withMetadata), expected);
}

void TerminalEngineTests::retainsKittyPixelsAcrossReplacementAndDeletion()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 10, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 16});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](std::string_view value) {
        return !engine.feed(std::as_bytes(std::span(value)));
    };
    // A direct RGBA red pixel, split inside its base64 payload.
    QVERIFY(feed("\x1b_Ga=T,f=32,s=1,v=1,i=71,p=1,c=2,r=1;"));
    QVERIFY(engine.snapshot()->images.empty());
    QVERIFY(feed("/wAA"));
    QVERIFY(feed("/w==\x1b\\"));
    const auto original = engine.snapshot();
    QVERIFY(original);
    QCOMPARE(original->images.size(), std::size_t{1});
    QVERIFY(original->damage != ztermy::terminal::TerminalDamageKind::none);
    const auto red = original->images[0].image;
    QCOMPARE(red->pixels, std::vector<std::uint8_t>({255, 0, 0, 255}));
    QCOMPARE(original->images[0].width, std::uint32_t{16});
    QCOMPARE(original->images[0].height, std::uint32_t{16});
    const auto unchanged = engine.snapshot();
    QVERIFY(unchanged);
    QCOMPARE(unchanged->images[0].image.get(), red.get());
    // Same dimensions and ID, different pixels: size-only caches are wrong.
    QVERIFY(feed("\x1b_Ga=t,f=32,s=1,v=1,i=71;AAD//w==\x1b\\"));
    QVERIFY(feed("\x1b_Ga=p,i=71,p=1,c=2,r=1;\x1b\\"));
    const auto replaced = engine.snapshot();
    QVERIFY(replaced);
    QVERIFY(!replaced->images.empty());
    QVERIFY(replaced->damage != ztermy::terminal::TerminalDamageKind::none);
    QCOMPARE(replaced->images[0].image->pixels, std::vector<std::uint8_t>({0, 0, 255, 255}));
    QVERIFY(replaced->images[0].image->generation != red->generation);
    QCOMPARE(red->pixels, std::vector<std::uint8_t>({255, 0, 0, 255}));
    QVERIFY(feed("\x1b_Ga=d,d=A;\x1b\\"));
    const auto deleted = engine.snapshot();
    QVERIFY(deleted);
    QVERIFY(deleted->images.empty());
    QVERIFY(deleted->damage != ztermy::terminal::TerminalDamageKind::none);
    QCOMPARE(red->pixels, std::vector<std::uint8_t>({255, 0, 0, 255}));
}

void TerminalEngineTests::tracksKittyPlacementAcrossScreenAndScrollChanges()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 10, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 16});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](std::string_view value) {
        return !engine.feed(std::as_bytes(std::span(value)));
    };
    QVERIFY(feed("\x1b[2;3H\x1b_Ga=T,f=24,s=1,v=1,i=72,c=1,r=1,C=1;/wAA\x1b\\"));
    auto original = engine.snapshot();
    QVERIFY(original);
    QCOMPARE(original->images.size(), std::size_t{1});
    QCOMPARE(original->images[0].column, 2);
    QCOMPARE(original->images[0].row, 1);
    std::weak_ptr<const ztermy::terminal::TerminalImage> pixels = original->images[0].image;
    QVERIFY(feed("\x1b[?1049h"));
    QVERIFY(engine.snapshot()->images.empty());
    QVERIFY(feed("\x1b[?1049l"));
    QCOMPARE(engine.snapshot()->images[0].image.get(), original->images[0].image.get());
    QVERIFY(feed("\x1b[4;1H\n"));
    auto scrolled = engine.snapshot();
    QVERIFY(scrolled);
    QCOMPARE(scrolled->images.size(), std::size_t{1});
    QCOMPARE(scrolled->images[0].row, 0);
    QVERIFY(feed("\x1b_Ga=d,d=A;\x1b\\"));
    QVERIFY(engine.snapshot()->images.empty());
    original->images.clear();
    QVERIFY(!pixels.expired()); // The scrolled snapshot still owns its pixels.
    scrolled->images.clear();
    QVERIFY(pixels.expired()); // The bridge must not retain deleted image pixels.
}

void TerminalEngineTests::rendersUnicodeImageFragmentsWithInheritedCoordinates()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 8, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 8});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](QByteArrayView value) {
        return !engine.feed(std::as_bytes(std::span(value.data(), static_cast<std::size_t>(value.size()))));
    };
    // Independent protocol example: a 2x2 raster, a 2x2 virtual grid, then
    // row diacritics only. The second column must inherit the first cell's ID/row.
    const QByteArray pixels = QByteArray::fromHex("ff000000ff000000ffffffff");
    QVERIFY(feed(QByteArray("\x1b_Ga=T,f=24,s=2,v=2,i=42,U=1,c=2,r=2,q=2;") + pixels.toBase64() + "\x1b\\"));
    QVERIFY(engine.snapshot()->images.empty()); // A prototype has no screen location.
    QVERIFY(feed("\x1b[38;5;42m"));
    const auto text = QString::fromUcs4(U"\U0010eeee\u0305\U0010eeee\r\n\U0010eeee\u030d\U0010eeee").toUtf8();
    for (const char byte : text)
        QVERIFY(feed(QByteArrayView(&byte, 1))); // Includes split UTF-8 and combining marks.
    auto original = engine.snapshot();
    QVERIFY(original);
    QCOMPARE(original->images.size(), std::size_t{2});
    for (const auto &image : original->images)
    {
        QCOMPARE(image.imageId, std::uint32_t{42});
        QCOMPARE(image.column, 0);
        QVERIFY(image.row == 0 || image.row == 1);
        QCOMPARE(image.width, std::uint32_t{16});
        QCOMPARE(image.height, std::uint32_t{8});
        QCOMPARE(image.sourceX, std::uint32_t{0});
        QCOMPARE(image.sourceY, static_cast<std::uint32_t>(image.row));
        QCOMPARE(image.sourceWidth, std::uint32_t{2});
        QCOMPARE(image.sourceHeight, std::uint32_t{1});
        QCOMPARE(image.image->pixels.size(), std::size_t{12});
    }
    QCOMPARE(original->images[0].image.get(), original->images[1].image.get());
    QVERIFY(original->cell(0, 0).invisible);
    QVERIFY(original->cell(1, 1).invisible);
    // Erasing text, not deleting a graphics placement, must remove only that
    // fragment. The neighboring normal letter must not be covered by the image.
    QVERIFY(feed("\x1b[1;2HX"));
    const auto edited = engine.snapshot();
    QVERIFY(edited);
    QCOMPARE(edited->cell(1, 0).grapheme, std::u32string(U"X"));
    QVERIFY(!edited->cell(1, 0).invisible);
    const auto first = std::ranges::find_if(edited->images, [](const auto &image) {
        return image.row == 0;
    });
    QVERIFY(first != edited->images.end());
    QCOMPARE(first->width, std::uint32_t{8});
    QCOMPARE(first->sourceWidth, std::uint32_t{1});
    QVERIFY(!engine.resize({.columns = 8, .rows = 4, .cellWidthPixels = 10, .cellHeightPixels = 10}));
    const auto resized = engine.snapshot();
    QVERIFY(resized);
    const auto second = std::ranges::find_if(resized->images, [](const auto &image) {
        return image.row == 1;
    });
    QVERIFY(second != resized->images.end());
    QCOMPARE(second->width, std::uint32_t{20});
    QCOMPARE(second->height, std::uint32_t{10});
}

void TerminalEngineTests::boundsImageMetadataAndAllowsRecovery()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 8, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 8});
    QVERIFY(created);
    auto &engine = **created;
    const auto send = [&](const std::string &command) {
        const auto error = engine.feed(std::as_bytes(std::span(command)));
        const auto bytes = engine.takePtyWrite();
        return error ? std::string("feed failed")
                     : std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    };
    // Tiny rasters bypass byte-budget pressure. Limit storage, not merely the
    // number returned by a viewport snapshot; inspect the protocol response.
    for (int id = 1; id <= 4096; ++id)
        QVERIFY(send("\x1b_Ga=t,f=24,s=1,v=1,i=" + std::to_string(id) + ";/wAA\x1b\\").contains("OK"));
    QVERIFY(send("\x1b_Ga=t,f=24,s=1,v=1,i=5000;/wAA\x1b\\").contains("ENOMEM"));
    QVERIFY(send("\x1b_Ga=t,f=24,s=1,v=1,i=1;AP8A\x1b\\").contains("OK"));
    send("\x1b_Ga=d,d=I,i=4096;\x1b\\");
    QVERIFY(send("\x1b_Ga=t,f=24,s=1,v=1,i=5000;/wAA\x1b\\").contains("OK"));
    for (int id = 1; id <= 4096; ++id)
        QVERIFY(send("\x1b_Ga=p,i=1,C=1,p=" + std::to_string(id) + ";\x1b\\").contains("OK"));
    QVERIFY(send("\x1b_Ga=p,i=1,C=1,p=5000;\x1b\\").contains("ENOMEM"));
    QVERIFY(send("\x1b_Ga=p,i=1,C=1,p=1;\x1b\\").contains("OK"));
    send("\x1b_Ga=d,d=i,i=1,p=4096;\x1b\\");
    QVERIFY(send("\x1b_Ga=p,i=1,C=1,p=5000;\x1b\\").contains("OK"));
    send("\x1b_Ga=d,d=A;\x1b\\healthy");
    QCOMPARE(engine.snapshot()->cursor.column, std::uint16_t{7});
}

void TerminalEngineTests::enlargesSinglePixelUnicodeImageAcrossRows()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 8, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 8});
    QVERIFY(created);
    auto &engine = **created;
    const auto command = QByteArray("\x1b_Ga=T,f=24,s=1,v=1,i=992,U=1,c=4,r=4,q=2;AP8A\x1b\\\x1b[38;2;0;3;224m")
                         + QString::fromUcs4(U"\U0010eeee\u0305\U0010eeee\U0010eeee\U0010eeee\r\n"
                                             U"\U0010eeee\u030d\U0010eeee\U0010eeee\U0010eeee")
                               .toUtf8();
    QVERIFY(!engine.feed(std::as_bytes(std::span(command.data(), static_cast<std::size_t>(command.size())))));
    const auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    // Magnifying one pixel into four rows must not round each quarter-pixel
    // source slice down to zero. The two rows are adjacent portions of one image.
    QCOMPARE(snapshot->images.size(), std::size_t{2});
    for (const auto &placement : snapshot->images)
    {
        QCOMPARE(placement.width, 32.0);
        QCOMPARE(placement.height, 8.0);
        QCOMPARE(placement.sourceWidth, 1.0);
        QCOMPARE(placement.sourceHeight, 0.25);
        QCOMPARE(placement.sourceY, placement.row * 0.25);
    }
}

void TerminalEngineTests::resolvesUnicodeImageIdentityAndReleasesSnapshotPixels()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 8, .rows = 4, .cellWidthPixels = 8, .cellHeightPixels = 8});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](QByteArrayView value) {
        return !engine.feed(std::as_bytes(std::span(value.data(), static_cast<std::size_t>(value.size()))));
    };
    QVERIFY(feed("\x1b_Ga=T,f=24,s=1,v=1,i=33554474,p=1,U=1,c=2,r=1,q=2;/wAA\x1b\\"));
    QVERIFY(feed("\x1b_Ga=p,i=33554474,p=2,U=1,c=1,r=1,q=2\x1b\\\x1b[38;5;42;58;5;2m"));
    QVERIFY(feed(QString::fromUcs4(U"\U0010eeee\u0305\u0305\u030e").toUtf8()));
    auto shown = engine.snapshot();
    QVERIFY(shown);
    QCOMPARE(shown->images.size(), std::size_t{1});
    QCOMPARE(shown->images[0].imageId, std::uint32_t{33554474});
    QCOMPARE(shown->images[0].placementId, std::uint32_t{2});
    QCOMPARE(shown->images[0].width, std::uint32_t{8});
    QCOMPARE(shown->images[0].offsetX, std::uint32_t{0});
    std::weak_ptr<const ztermy::terminal::TerminalImage> pixels = shown->images[0].image;
    QVERIFY(feed("\x1b[4;1H\n\n\n\n"));
    QVERIFY(engine.snapshot()->images.empty());
    shown->images.clear();
    QVERIFY(pixels.expired()); // Off-screen snapshots cannot pin the CPU image copy.
    engine.scrollViewport(-4);
    auto history = engine.snapshot();
    QVERIFY(history);
    QCOMPARE(history->images.size(), std::size_t{1});
    QCOMPARE(history->images[0].image->pixels, std::vector<std::uint8_t>({255, 0, 0}));
    pixels = history->images[0].image;
    QVERIFY(feed("\x1b_Ga=d,d=I,i=33554474,q=2\x1b\\"));
    QVERIFY(engine.snapshot()->images.empty());
    history->images.clear();
    QVERIFY(pixels.expired());
}

void TerminalEngineTests::preservesBlinkAttributesWithoutChangingText()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 10, .rows = 3});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](std::string_view value) {
        return !engine.feed(std::as_bytes(std::span(value)));
    };
    QVERIFY(feed("A\x1b[5"));
    const auto partial = engine.snapshot();
    QVERIFY(partial);
    QVERIFY(!partial->cell(0, 0).blink);
    QCOMPARE(partial->cursor.column, std::uint16_t{1});
    QVERIFY(feed("mB\x1b[25mC\x1b[6mD\x1b[0mE"));
    const auto styled = engine.snapshot();
    QVERIFY(styled);
    for (std::uint16_t column = 0; column < 5; ++column)
    {
        QCOMPARE(styled->cell(column, 0).grapheme, std::u32string(1, U'A' + column));
        QCOMPARE(styled->cell(column, 0).blink, column == 1 || column == 3);
        QVERIFY(!styled->cell(column, 0).invisible);
        QCOMPARE(styled->cell(column, 0).displayWidth, std::uint8_t{1});
    }
    QCOMPARE(styled->cursor.column, std::uint16_t{5});
    // Replacing a blinking cell after reset must not retain its old attribute.
    QVERIFY(feed("\r\x1b[CX"));
    const auto replaced = engine.snapshot();
    QVERIFY(replaced);
    QCOMPARE(replaced->cell(1, 0).grapheme, std::u32string(U"X"));
    QVERIFY(!replaced->cell(1, 0).blink);
    QVERIFY(styled->cell(1, 0).blink);
}

void TerminalEngineTests::retainsProgressAcrossFragmentedReports()
{
    using ztermy::terminal::TerminalProgressState;
    auto created = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 10, .rows = 3});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](std::string_view value) {
        return !engine.feed(std::as_bytes(std::span(value)));
    };
    QVERIFY(feed("x\x1b]9;4;1;37\x07"));
    const auto initial = engine.snapshot();
    QVERIFY(initial);
    QCOMPARE(initial->progress.state, TerminalProgressState::active);
    QCOMPARE(initial->progress.percentage, 37);
    QVERIFY(feed("\x1b]9;4;4;6"));
    QCOMPARE(engine.snapshot()->progress, initial->progress);
    QVERIFY(feed("0\x1b\\"));
    QCOMPARE(engine.snapshot()->progress.state, TerminalProgressState::paused);
    QCOMPARE(engine.snapshot()->progress.percentage, 60);
    QVERIFY(feed("\x1b]9;4;2;60\x07"));
    QCOMPARE(engine.snapshot()->progress.state, TerminalProgressState::error);
    QVERIFY(feed("\x1b]9;4;3\x07"));
    QCOMPARE(engine.snapshot()->progress.state, TerminalProgressState::indeterminate);
    QVERIFY(feed("\x1b]9;4;0\x07"));
    const auto removed = engine.snapshot();
    QVERIFY(removed);
    QCOMPARE(removed->progress.state, TerminalProgressState::none);
    QCOMPARE(removed->progress.percentage, -1);
    QCOMPARE(removed->cell(0, 0).grapheme, std::u32string(U"x"));
    QCOMPARE(initial->progress.percentage, 37);
}

void TerminalEngineTests::boundsAndThrottlesProgramNotifications()
{
    for (const std::string_view prefix : {"\x1b]9;", "\x1b]777;notify;Job;"})
    {
        auto created = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 10, .rows = 3});
        QVERIFY(created);
        auto &engine = **created;
        const auto feed = [&](std::string_view value) {
            return !engine.feed(std::as_bytes(std::span(value)));
        };
        QVERIFY(feed(std::string(prefix) + std::string(4097, 'x') + '\x07'));
        QVERIFY(!engine.snapshot()->notification);
        QVERIFY(feed(prefix));
        QVERIFY(feed("Build complete"));
        QVERIFY(!engine.snapshot()->notification);
        QVERIFY(feed("\x07"));
        const auto first = engine.snapshot();
        QVERIFY(first && first->notification);
        QCOMPARE(first->notification->sequence, std::uint64_t{1});
        QCOMPARE(first->notification->body, std::string("Build complete"));
        for (int index = 0; index < 100; ++index)
            QVERIFY(feed(std::string(prefix) + "Flood\x07"));
        QCOMPARE(engine.snapshot()->notification, first->notification);
    }
}

void TerminalEngineTests::preservesTransientTitlesAcrossFragmentedOutput()
{
    auto created = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 10, .rows = 3});
    QVERIFY(created);
    auto &engine = **created;
    const auto feed = [&](std::string_view text) {
        return !engine.feed(std::as_bytes(std::span(text)));
    };
    QVERIFY(feed("x\x1b]0;first\x07"));
    const auto first = engine.snapshot();
    QVERIFY(first);
    QCOMPARE(first->windowTitle, std::string("first"));
    QVERIFY(feed("\x1b]2;second"));
    QCOMPARE(engine.snapshot()->windowTitle, std::string("first"));
    QVERIFY(feed("\x1b"));
    QCOMPARE(engine.snapshot()->windowTitle, std::string("first"));
    QVERIFY(feed("\\"));
    const auto second = engine.snapshot();
    QVERIFY(second);
    QCOMPARE(second->windowTitle, std::string("second"));
    QCOMPARE(second->cell(0, 0).grapheme, std::u32string(U"x"));
    QCOMPARE(first->windowTitle, std::string("first"));
    QVERIFY(feed("\x1b]2;\x07"));
    QVERIFY(engine.snapshot()->windowTitle.empty());
}

void TerminalEngineTests::tracksSynchronizedOutputMode()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 80, .rows = 24});
    QVERIFY(result);
    auto &engine = **result;
    QVERIFY(!engine.synchronizedOutput());
    constexpr std::string_view begin = "\x1b[?2026hworking";
    QVERIFY(!engine.feed(std::as_bytes(std::span(begin))));
    QVERIFY(engine.synchronizedOutput());
    QVERIFY(engine.snapshot());
    QVERIFY(engine.synchronizedOutput());
    constexpr std::string_view query = "\x1b[2;4H\x1b[6n";
    QVERIFY(!engine.feed(std::as_bytes(std::span(query))));
    const auto response = engine.takePtyWrite();
    QCOMPARE(std::string(reinterpret_cast<const char *>(response.data()), response.size()), std::string("\x1b[2;4R"));
    QVERIFY(engine.synchronizedOutput());
    constexpr std::string_view end = "done\x1b[?2026l";
    QVERIFY(!engine.feed(std::as_bytes(std::span(end))));
    QVERIFY(!engine.synchronizedOutput());
}

void TerminalEngineTests::doesNotScrollWhenNushellClearsToViewportBottom()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 10, .rows = 3});
    QVERIFY(result);
    auto &engine = **result;
    constexpr std::string_view first = "\x1b[1;1Hold\x1b[3;10H\x1b]133;";
    constexpr std::string_view second = "A;k=i\x1b\\\x1b[2;1Hnew";
    QVERIFY(!engine.feed(std::as_bytes(std::span(first))));
    QVERIFY(!engine.feed(std::as_bytes(std::span(second))));
    const auto snapshot = engine.snapshot();
    QCOMPARE(snapshot->cell(0, 0).grapheme, std::u32string{U"o"});
    QCOMPARE(snapshot->cell(0, 1).grapheme, std::u32string{U"n"});
    QCOMPARE(snapshot->scrollbar.total, snapshot->scrollbar.visible);
}

void TerminalEngineTests::quotesDroppedPathsForShellDialects()
{
    using ztermy::terminal::ShellDialect;
    QCOMPARE(ztermy::terminal::quoteShellPath(R"(C:\A B\o'ne.txt)", ShellDialect::PowerShell),
             std::string("'C:\\A B\\o''ne.txt'"));
    QCOMPARE(ztermy::terminal::quoteShellPath("/tmp/a'b", ShellDialect::Posix), std::string("'/tmp/a'\\''b'"));
    const std::array paths = {std::string{"C:\\one"}, std::string{"D:\\two words"}};
    QCOMPARE(ztermy::terminal::quoteShellPaths(paths, ShellDialect::Cmd), std::string("\"C:\\one\" \"D:\\two words\""));
}

void TerminalEngineTests::drainsBoundedOsc52ClipboardWrites()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 20, .rows = 3});
    QVERIFY(result.has_value());
    auto &engine = **result;

    constexpr std::string_view first = "\x1b]52;c;SGVs";
    constexpr std::string_view second = "bG8=\x07";
    QVERIFY(!engine.feed(std::as_bytes(std::span(first))));
    QVERIFY(!engine.takeClipboardWrite().has_value());
    QVERIFY(!engine.feed(std::as_bytes(std::span(second))));
    QCOMPARE(engine.takeClipboardWrite(), std::optional<std::string>{"Hello"});
    QVERIFY(!engine.takeClipboardWrite().has_value());

    constexpr std::string_view query = "\x1b]52;c;?\x07";
    QVERIFY(!engine.feed(std::as_bytes(std::span(query))));
    QVERIFY(!engine.takeClipboardWrite().has_value());

    constexpr std::string_view superseded = "\x1b]52;c;V29ybGQ=\x07\x1b]52;c;IQ==\x07";
    QVERIFY(!engine.feed(std::as_bytes(std::span(superseded))));
    QCOMPARE(engine.takeClipboardWrite(), std::optional<std::string>{"!"});
}

void TerminalEngineTests::returnsTerminalQueryResponses()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 20, .rows = 3});
    QVERIFY(result.has_value());
    auto &engine = **result;

    const auto initial = engine.snapshot();
    QVERIFY(initial.has_value());
    QCOMPARE(initial->cursor.style, ztermy::terminal::TerminalCursorStyle::bar);

    constexpr std::string_view query = "\x1b[2;4H\x1b[6n";
    QVERIFY(!engine.feed(std::as_bytes(std::span(query))));
    const auto response = engine.takePtyWrite();
    QCOMPARE(std::string(reinterpret_cast<const char *>(response.data()), response.size()), std::string("\x1b[2;4R"));
    QVERIFY(engine.takePtyWrite().empty());
}

void TerminalEngineTests::reportsCurrentLightOrDarkColorScheme()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 20, .rows = 3});
    QVERIFY(result.has_value());
    auto &engine = **result;
    constexpr std::string_view query = "\x1b[?996n";
    const auto response = [&engine] {
        return engine.takePtyWrite();
    };

    QVERIFY(!engine.feed(std::as_bytes(std::span(query))));
    const auto darkReply = response();
    QCOMPARE(std::string(reinterpret_cast<const char *>(darkReply.data()), darkReply.size()),
             std::string("\x1b[?997;1n"));

    ztermy::terminal::TerminalColorScheme light;
    light.background = {.red = 255, .green = 255, .blue = 255};
    QVERIFY(!engine.setColorScheme(light));
    QVERIFY(!engine.feed(std::as_bytes(std::span(query))));
    const auto lightReply = response();
    QCOMPARE(std::string(reinterpret_cast<const char *>(lightReply.data()), lightReply.size()),
             std::string("\x1b[?997;2n"));
}

void TerminalEngineTests::reportsConfiguredBackgroundColorToShell()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 20, .rows = 3});
    QVERIFY(result.has_value());
    auto &engine = **result;
    ztermy::terminal::TerminalColorScheme light;
    light.background = {.red = 255, .green = 255, .blue = 255};
    QVERIFY(!engine.setColorScheme(light));

    constexpr std::string_view query = "\x1b]11;?\x07";
    QVERIFY(!engine.feed(std::as_bytes(std::span(query))));
    const auto response = engine.takePtyWrite();
    QCOMPARE(std::string(reinterpret_cast<const char *>(response.data()), response.size()),
             std::string("\x1b]11;rgb:ffff/ffff/ffff\x07"));
}

void TerminalEngineTests::notifiesSubscribedShellWhenColorSchemeChanges()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 20, .rows = 3});
    QVERIFY(result.has_value());
    auto &engine = **result;
    constexpr std::string_view subscribe = "\x1b[?2031h";
    constexpr std::string_view unsubscribe = "\x1b[?2031l";
    QVERIFY(!engine.feed(std::as_bytes(std::span(subscribe))));
    static_cast<void>(engine.takePtyWrite());

    ztermy::terminal::TerminalColorScheme light;
    light.background = {.red = 255, .green = 255, .blue = 255};
    QVERIFY(!engine.setColorScheme(light));
    auto reply = engine.takePtyWrite();
    QCOMPARE(std::string(reinterpret_cast<const char *>(reply.data()), reply.size()), std::string("\x1b[?997;2n"));

    light.foreground = {.red = 25, .green = 25, .blue = 25};
    QVERIFY(!engine.setColorScheme(light));
    QVERIFY(engine.takePtyWrite().empty());

    QVERIFY(!engine.feed(std::as_bytes(std::span(unsubscribe))));
    ztermy::terminal::TerminalColorScheme dark;
    dark.background = {.red = 0, .green = 0, .blue = 0};
    QVERIFY(!engine.setColorScheme(dark));
    QVERIFY(engine.takePtyWrite().empty());
}

void TerminalEngineTests::reportsCurrentTerminalDimensions()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create(
        {.columns = 80, .rows = 24, .cellWidthPixels = 9, .cellHeightPixels = 18});
    QVERIFY(result.has_value());
    auto &engine = **result;
    const auto query = [&engine](const std::string_view request) {
        if (engine.feed(std::as_bytes(std::span(request))))
            return std::string{};
        const auto response = engine.takePtyWrite();
        return std::string(reinterpret_cast<const char *>(response.data()), response.size());
    };

    QCOMPARE(query("\x1b[14t"), std::string("\x1b[4;432;720t"));
    QCOMPARE(query("\x1b[16t"), std::string("\x1b[6;18;9t"));
    QCOMPARE(query("\x1b[18t"), std::string("\x1b[8;24;80t"));
    QVERIFY(!engine.resize({.columns = 100, .rows = 30, .cellWidthPixels = 10, .cellHeightPixels = 20}));
    QCOMPARE(query("\x1b[18t"), std::string("\x1b[8;30;100t"));
}

void TerminalEngineTests::exposesExplicitOsc8Hyperlinks()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 32, .rows = 3});
    QVERIFY(result.has_value());
    auto &engine = **result;

    constexpr std::string_view content = "\x1b]8;;https://target.example/path\x1b\\label\x1b]8;;\x1b\\";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    const auto snapshot = engine.snapshot();
    QVERIFY(snapshot.has_value());
    QVERIFY(snapshot->cell(0, 0).hyperlinkId != 0);
    const auto *link = snapshot->hyperlink(snapshot->cell(0, 0).hyperlinkId);
    QVERIFY(link != nullptr);
    QCOMPARE(link->uri, std::string("https://target.example/path"));
    QCOMPARE(link->kind, ztermy::terminal::TerminalHyperlinkKind::explicitOsc8);
    QCOMPARE(snapshot->cell(4, 0).hyperlinkId, snapshot->cell(0, 0).hyperlinkId);
    QCOMPARE(snapshot->cell(5, 0).hyperlinkId, std::uint32_t{0});
}

void TerminalEngineTests::detectsAutomaticHttpLinksWithoutOverridingOsc8()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 48, .rows = 3});
    QVERIFY(result.has_value());
    auto &engine = **result;

    constexpr std::string_view content =
        "See https://example.test/path.\r\n\x1b]8;;https://explicit.test\x1b\\https://shown.test\x1b]8;;\x1b\\";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    const auto snapshot = engine.snapshot();
    QVERIFY(snapshot.has_value());

    const auto automaticId = snapshot->cell(4, 0).hyperlinkId;
    QVERIFY(automaticId != 0);
    const auto *automatic = snapshot->hyperlink(automaticId);
    QVERIFY(automatic != nullptr);
    QCOMPARE(automatic->uri, std::string("https://example.test/path"));
    QCOMPARE(automatic->kind, ztermy::terminal::TerminalHyperlinkKind::automaticUrl);
    QCOMPARE(snapshot->cell(29, 0).hyperlinkId, std::uint32_t{0});

    const auto explicitId = snapshot->cell(0, 1).hyperlinkId;
    QVERIFY(explicitId != 0);
    const auto *explicitLink = snapshot->hyperlink(explicitId);
    QVERIFY(explicitLink != nullptr);
    QCOMPARE(explicitLink->uri, std::string("https://explicit.test"));
    QCOMPARE(explicitLink->kind, ztermy::terminal::TerminalHyperlinkKind::explicitOsc8);
}

void TerminalEngineTests::exposesShellWorkingDirectorySequences()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 20, .rows = 4});
    QVERIFY(result.has_value());
    auto &engine = **result;

    constexpr std::string_view osc7 = "\x1b]7;file://host/home/test/My%20Files\x07";
    QVERIFY(!engine.feed(std::as_bytes(std::span(osc7))));
    auto snapshot = engine.snapshot();
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->workingDirectory, std::string("file://host/home/test/My%20Files"));

    constexpr std::string_view osc1337 = "\x1b]1337;CurrentDir=/srv/project\x07";
    QVERIFY(!engine.feed(std::as_bytes(std::span(osc1337))));
    snapshot = engine.snapshot();
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->workingDirectory, std::string("/srv/project"));
}

void TerminalEngineTests::rejectsInvalidGeometry()
{
    const auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 0, .rows = 24});

    QVERIFY(!result);
    QCOMPARE(result.error(), std::make_error_code(std::errc::invalid_argument));
}

void TerminalEngineTests::parsesSplitVtSequences()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 80, .rows = 24});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view first = "\x1b[3";
    constexpr std::string_view second = "1mred\x1b[0m\r\nplain";
    QVERIFY(!engine.feed(std::as_bytes(std::span(first))));
    QVERIFY(!engine.feed(std::as_bytes(std::span(second))));

    const auto text = engine.plainText();
    if (!text)
    {
        QFAIL(text.error().message().c_str());
    }
    QCOMPARE(*text, "red\nplain");
}

void TerminalEngineTests::preservesContentAcrossResize()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 8, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "abcdefghij";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    QVERIFY(!engine.resize({.columns = 5, .rows = 3, .cellWidthPixels = 9, .cellHeightPixels = 18}));

    const auto text = engine.plainText();
    if (!text)
    {
        QFAIL(text.error().message().c_str());
    }
    QCOMPARE(*text, "abcdefghij");
}

void TerminalEngineTests::exposesImmutableStyledCells()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 2});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "\x1b[38;2;12;34;56mA\x1b[48;2;7;8;9mB\x1b[49;2mC\x1b[22mD"
                                         "\x1b[4:3m\x1b[58;2;220;30;50mE\x1b[4:2mF\x1b[0m";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    const auto snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }

    QCOMPARE(snapshot->columns, 12);
    QCOMPARE(snapshot->rows, 2);
    QCOMPARE(snapshot->cells.size(), std::size_t{24});
    QCOMPARE(snapshot->cell(0, 0).grapheme, std::u32string(U"A"));
    QCOMPARE(snapshot->cell(0, 0).foreground, (ztermy::terminal::TerminalColor{12, 34, 56}));
    QVERIFY(!snapshot->cell(0, 0).explicitBackground);
    QCOMPARE(snapshot->cell(1, 0).background, (ztermy::terminal::TerminalColor{7, 8, 9}));
    QVERIFY(snapshot->cell(1, 0).explicitBackground);
    QVERIFY(snapshot->cell(2, 0).faint);
    QVERIFY(!snapshot->cell(3, 0).faint);
    QCOMPARE(snapshot->cell(4, 0).underlineStyle, ztermy::terminal::TerminalUnderlineStyle::curly);
    QVERIFY(snapshot->cell(4, 0).underlineColor.has_value());
    QCOMPARE(*snapshot->cell(4, 0).underlineColor, (ztermy::terminal::TerminalColor{220, 30, 50}));
    QCOMPARE(snapshot->cell(5, 0).underlineStyle, ztermy::terminal::TerminalUnderlineStyle::doubleLine);
    QVERIFY(!snapshot->cell(2, 0).explicitBackground);
    QVERIFY(!snapshot->cell(11, 1).explicitBackground);
    QVERIFY(snapshot->cursor.visible);
    QCOMPARE(snapshot->cursor.column, 6);
    QCOMPARE(snapshot->cursor.row, 0);
}

void TerminalEngineTests::appliesColorSchemeToDefaultsAndPalette()
{
    using ztermy::terminal::TerminalColor;
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 2});
    QVERIFY(result.has_value());
    auto &engine = **result;

    constexpr std::string_view content = "[31mA[0mB";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    ztermy::terminal::TerminalColorScheme scheme;
    scheme.foreground = {.red = 1, .green = 2, .blue = 3};
    scheme.background = {.red = 4, .green = 5, .blue = 6};
    scheme.cursor = {.red = 7, .green = 8, .blue = 9};
    scheme.ansi[1] = {.red = 200, .green = 10, .blue = 20};
    QVERIFY(!engine.setColorScheme(scheme));

    const auto snapshot = engine.snapshot();
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->defaultForeground, scheme.foreground);
    QCOMPARE(snapshot->defaultBackground, scheme.background);
    QCOMPARE(snapshot->cursor.color, scheme.cursor);
    QCOMPARE(snapshot->cell(0, 0).foreground, scheme.ansi[1]);
    QCOMPARE(snapshot->cell(1, 0).foreground, scheme.foreground);
    QCOMPARE(snapshot->cell(1, 0).background, scheme.background);
}

void TerminalEngineTests::updatesPaletteUnderlineColorAfterThemeAndOscChange()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 2});
    QVERIFY(result.has_value());
    auto &engine = **result;
    constexpr std::string_view underlined = "\x1b[4:3;58;5;1mX";
    QVERIFY(!engine.feed(std::as_bytes(std::span(underlined))));

    ztermy::terminal::TerminalColorScheme scheme;
    scheme.ansi[1] = {.red = 210, .green = 20, .blue = 30};
    QVERIFY(!engine.setColorScheme(scheme));
    const auto themed = engine.snapshot();
    QVERIFY(themed.has_value());
    QVERIFY(themed->cell(0, 0).underlineColor.has_value());
    QCOMPARE(*themed->cell(0, 0).underlineColor, scheme.ansi[1]);

    constexpr std::string_view paletteOverride = "\x1b]4;1;rgb:00/cc/00\x07";
    QVERIFY(!engine.feed(std::as_bytes(std::span(paletteOverride))));
    const auto overridden = engine.snapshot();
    QVERIFY(overridden.has_value());
    QVERIFY(overridden->cell(0, 0).underlineColor.has_value());
    QCOMPARE(*overridden->cell(0, 0).underlineColor, (ztermy::terminal::TerminalColor{0, 204, 0}));
}

void TerminalEngineTests::exposesWideCellAndCursorWidth()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 2});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    const std::u8string content = u8"中\x1b[D";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    const auto snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }
    QCOMPARE(snapshot->cell(0, 0).grapheme, std::u32string(U"中"));
    QCOMPARE(snapshot->cell(0, 0).displayWidth, std::uint8_t{2});
    QCOMPARE(snapshot->cell(1, 0).displayWidth, std::uint8_t{0});
    QCOMPARE(snapshot->cursor.column, 0);
    QCOMPARE(snapshot->cursor.width, std::uint8_t{2});
}

void TerminalEngineTests::preservesPrimaryScreenAcrossAlternateScreen()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 16, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view primary = "primary";
    constexpr std::string_view enterAlternate = "\x1b[?1049h\x1b[H";
    constexpr std::string_view alternate = "alternate";
    constexpr std::string_view leaveAlternate = "\x1b[?1049l";
    QVERIFY(!engine.feed(std::as_bytes(std::span(primary))));
    const auto primarySnapshot = engine.snapshot();
    QVERIFY(primarySnapshot);
    QCOMPARE(primarySnapshot->cursor.column, std::uint16_t{7});
    QCOMPARE(primarySnapshot->cursor.row, std::uint16_t{0});
    QVERIFY(!engine.feed(std::as_bytes(std::span(enterAlternate))));
    QVERIFY(!engine.feed(std::as_bytes(std::span(alternate))));

    auto text = engine.plainText();
    if (!text)
    {
        QFAIL(text.error().message().c_str());
    }
    QCOMPARE(*text, "alternate");

    QVERIFY(!engine.feed(std::as_bytes(std::span(leaveAlternate))));
    text = engine.plainText();
    if (!text)
    {
        QFAIL(text.error().message().c_str());
    }
    QCOMPARE(*text, "primary");
    const auto restoredSnapshot = engine.snapshot();
    QVERIFY(restoredSnapshot);
    QCOMPARE(restoredSnapshot->cursor.column, primarySnapshot->cursor.column);
    QCOMPARE(restoredSnapshot->cursor.row, primarySnapshot->cursor.row);
}

void TerminalEngineTests::normalizesWideCellSelection()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 8, .rows = 2});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    const std::u8string content = u8"中";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    QVERIFY(!engine.setSelection(
        ztermy::terminal::TerminalSelection{.start = {.column = 1, .row = 0}, .end = {.column = 1, .row = 0}}));

    const auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->cell(0, 0).displayWidth, std::uint8_t{2});
    QCOMPARE(snapshot->cell(1, 0).displayWidth, std::uint8_t{0});
    QVERIFY(snapshot->cell(0, 0).selected);
    QVERIFY(snapshot->cell(1, 0).selected);
}

void TerminalEngineTests::handlesEraseAndCursorVisibility()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 16, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "first\r\nsecond";
    constexpr std::string_view hideCursor = "\x1b[?25l";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    QVERIFY(!engine.feed(std::as_bytes(std::span(hideCursor))));

    auto snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }
    QVERIFY(!snapshot->cursor.visible);

    constexpr std::string_view clearAndShowCursor = "\x1b[2J\x1b[H\x1b[?25h\x1b[6 q";
    QVERIFY(!engine.feed(std::as_bytes(std::span(clearAndShowCursor))));
    snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }
    QVERIFY(snapshot->cursor.visible);
    QCOMPARE(snapshot->cursor.column, std::uint16_t{0});
    QCOMPARE(snapshot->cursor.row, std::uint16_t{0});
    QCOMPARE(snapshot->cursor.style, ztermy::terminal::TerminalCursorStyle::bar);
    QVERIFY(!snapshot->cursor.blinking);

    const auto text = engine.plainText();
    if (!text)
    {
        QFAIL(text.error().message().c_str());
    }
    QCOMPARE(*text, "");
}

void TerminalEngineTests::exposesCombiningAndEmojiGraphemes()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 16, .rows = 2});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    const std::u8string content = u8"e\u0301中🙂";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    const auto snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }
    QCOMPARE(snapshot->cell(0, 0).grapheme, std::u32string(U"e\u0301"));
    QCOMPARE(snapshot->cell(0, 0).displayWidth, std::uint8_t{1});
    QCOMPARE(snapshot->cell(1, 0).grapheme, std::u32string(U"中"));
    QCOMPARE(snapshot->cell(1, 0).displayWidth, std::uint8_t{2});
    QCOMPARE(snapshot->cell(2, 0).displayWidth, std::uint8_t{0});
    QCOMPARE(snapshot->cell(3, 0).grapheme, std::u32string(U"🙂"));
    QCOMPARE(snapshot->cell(3, 0).displayWidth, std::uint8_t{2});
    QCOMPARE(snapshot->cell(4, 0).displayWidth, std::uint8_t{0});
    QCOMPARE(snapshot->cursor.column, std::uint16_t{5});
}

void TerminalEngineTests::reportsAndResetsRenderDamage()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    const auto initial = engine.snapshot();
    QVERIFY(initial);
    QCOMPARE(initial->damage, ztermy::terminal::TerminalDamageKind::full);

    const auto clean = engine.snapshot();
    QVERIFY(clean);
    QCOMPARE(clean->damage, ztermy::terminal::TerminalDamageKind::none);
    QVERIFY(clean->damagedRows.empty());

    constexpr std::string_view content = "changed";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    const auto changed = engine.snapshot();
    QVERIFY(changed);
    QVERIFY(changed->damage != ztermy::terminal::TerminalDamageKind::none);
    if (changed->damage == ztermy::terminal::TerminalDamageKind::partial)
    {
        QVERIFY(std::ranges::find(changed->damagedRows, std::uint16_t{0}) != changed->damagedRows.end());
    }

    QVERIFY(!engine.resize({.columns = 16, .rows = 4}));
    const auto resized = engine.snapshot();
    QVERIFY(resized);
    QCOMPARE(resized->damage, ztermy::terminal::TerminalDamageKind::full);
}

void TerminalEngineTests::selectsAndFormatsViewportText()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 16, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "alpha beta\r\nsecond";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    QVERIFY(!engine.setSelection(
        ztermy::terminal::TerminalSelection{.start = {.column = 0, .row = 0}, .end = {.column = 4, .row = 0}}));

    const auto selectedText = engine.selectedText();
    if (!selectedText)
    {
        QFAIL(selectedText.error().message().c_str());
    }
    QVERIFY(selectedText->has_value());
    QCOMPARE(**selectedText, "alpha");

    const auto snapshot = engine.snapshot();
    if (!snapshot)
    {
        QFAIL(snapshot.error().message().c_str());
    }
    for (std::uint16_t column = 0; column < 5; ++column)
    {
        QVERIFY(snapshot->cell(column, 0).selected);
    }
    QVERIFY(snapshot->selectionPresent);
    QVERIFY(!snapshot->cell(5, 0).selected);

    QVERIFY(!engine.setSelection(std::nullopt));
    const auto clearedText = engine.selectedText();
    QVERIFY(clearedText);
    QVERIFY(!clearedText->has_value());
}

void TerminalEngineTests::navigatesCopyModeWithTerminalOwnedSelection()
{
    using Action = ztermy::terminal::TerminalCopyModeAction;
    using ActionType = ztermy::terminal::TerminalCopyModeActionType;
    using Motion = ztermy::terminal::TerminalCopyModeMotion;

    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 20, .rows = 3});
    QVERIFY(result);
    auto &engine = **result;
    constexpr std::string_view content = "alpha beta";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    auto changed = engine.applyCopyModeAction(Action{.type = ActionType::begin});
    QVERIFY(changed);
    QVERIFY(*changed);
    changed = engine.applyCopyModeAction(Action{.type = ActionType::move, .motion = Motion::left});
    QVERIFY(changed);
    auto selected = engine.selectedText();
    QVERIFY(selected);
    QVERIFY(selected->has_value());
    QCOMPARE(**selected, "a");

    changed = engine.applyCopyModeAction(Action{.type = ActionType::selectCharacter});
    QVERIFY(changed);
    changed = engine.applyCopyModeAction(Action{.type = ActionType::move, .motion = Motion::wordLeft});
    QVERIFY(changed);
    selected = engine.selectedText();
    QVERIFY(selected);
    QVERIFY(selected->has_value());
    QCOMPARE(**selected, "beta");

    QVERIFY(!engine.resize({.columns = 10, .rows = 4}));
    selected = engine.selectedText();
    QVERIFY(selected);
    QVERIFY(selected->has_value());
    QCOMPARE(**selected, "beta");

    changed = engine.applyCopyModeAction(Action{.type = ActionType::cancel});
    QVERIFY(changed);
    selected = engine.selectedText();
    QVERIFY(selected);
    QVERIFY(!selected->has_value());
}

void TerminalEngineTests::autoscrollsTrackedSelectionWithoutDoubleScrolling()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    QVERIFY(result.has_value());
    auto &engine = **result;
    constexpr std::string_view content =
        "line0\r\nline1\r\nline2\r\nline3\r\nline4\r\nline5\r\nline6\r\nline7\r\nline8";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    ztermy::terminal::TerminalSelectionGesture press{
        .type = ztermy::terminal::TerminalSelectionGestureType::press,
        .point = {.column = 2, .row = 1},
        .positionX = 20.0,
        .positionY = 24.0,
        .eventTimeNanoseconds = 1'000'000'000,
        .repeatIntervalNanoseconds = 500'000'000,
        .repeatDistancePixels = 4.0,
    };
    auto changed = engine.applySelectionGesture(press);
    QVERIFY(changed);

    auto drag = press;
    drag.type = ztermy::terminal::TerminalSelectionGestureType::drag;
    drag.point = {.column = 2, .row = 0};
    drag.positionY = 0.0;
    drag.columns = 12;
    drag.cellWidthPixels = 8;
    drag.screenHeightPixels = 48;
    changed = engine.applySelectionGesture(drag);
    QVERIFY(changed);
    QVERIFY(*changed);

    const auto before = engine.snapshot();
    QVERIFY(before);
    QVERIFY(before->scrollbar.offset >= 3);

    auto tick = drag;
    tick.type = ztermy::terminal::TerminalSelectionGestureType::autoscrollTick;
    tick.scrollRows = -3;
    changed = engine.applySelectionGesture(tick);
    QVERIFY(changed);
    QVERIFY(*changed);

    const auto after = engine.snapshot();
    QVERIFY(after);
    QCOMPARE(before->scrollbar.offset - after->scrollbar.offset, std::uint64_t{3});
    QVERIFY(after->selectionPresent);

    tick.scrollRows = -64;
    changed = engine.applySelectionGesture(tick);
    QVERIFY(changed);
    const auto atTop = engine.snapshot();
    QVERIFY(atTop);
    QCOMPARE(atTop->scrollbar.offset, std::uint64_t{0});
    changed = engine.applySelectionGesture(tick);
    QVERIFY(changed);
    QVERIFY(!*changed);
}

void TerminalEngineTests::appliesTrackedWordSelectionGestures()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 40, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;
    constexpr std::string_view content = "user@host:/var/log/app.log ready";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    auto press = ztermy::terminal::TerminalSelectionGesture{
        .type = ztermy::terminal::TerminalSelectionGestureType::press,
        .point = {.column = 4, .row = 0},
        .positionX = 36.0,
        .positionY = 8.0,
        .eventTimeNanoseconds = 1'000'000'000,
        .repeatIntervalNanoseconds = 500'000'000,
        .repeatDistancePixels = 4.0,
        .wordBoundaryCodepoints = U" \t'\"│`|;,()[]{}<>$",
    };
    const auto firstPress = engine.applySelectionGesture(press);
    QVERIFY(firstPress);
    QVERIFY(*firstPress);
    auto release = press;
    release.type = ztermy::terminal::TerminalSelectionGestureType::release;
    const auto firstRelease = engine.applySelectionGesture(release);
    QVERIFY(firstRelease);
    QVERIFY(!*firstRelease);
    press.eventTimeNanoseconds += 100'000'000;
    const auto secondPress = engine.applySelectionGesture(press);
    QVERIFY(secondPress);
    QVERIFY(*secondPress);

    const auto selected = engine.selectedText();
    QVERIFY(selected);
    QVERIFY(selected->has_value());
    QCOMPARE(**selected, "user@host:/var/log/app.log");
}

void TerminalEngineTests::selectsCompleteScrollback()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;
    constexpr std::string_view content = "line0\r\nline1\r\nline2\r\nline3\r\nline4\r\nline5";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));
    QVERIFY(!engine.selectAll());

    const auto selected = engine.selectedText();
    QVERIFY(selected);
    if (!selected->has_value())
    {
        QFAIL("selectAll did not produce selected text");
    }
    const auto &selectedText = **selected;
    QVERIFY(selectedText.starts_with("line0"));
    QVERIFY(selectedText.ends_with("line5"));
}

void TerminalEngineTests::scrollsThroughHistory()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "line0\r\nline1\r\nline2\r\nline3\r\nline4\r\nline5";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    const auto bottom = engine.snapshot();
    if (!bottom)
    {
        QFAIL(bottom.error().message().c_str());
    }
    QVERIFY(bottom->scrollbar.total > bottom->scrollbar.visible);
    const std::uint64_t bottomOffset = bottom->scrollbar.offset;

    engine.scrollViewport(-2);
    const auto history = engine.snapshot();
    if (!history)
    {
        QFAIL(history.error().message().c_str());
    }
    QVERIFY(history->scrollbar.offset < bottomOffset);
    QVERIFY(!history->cursor.visible);

    engine.scrollToBottom();
    const auto restored = engine.snapshot();
    QVERIFY(restored);
    QCOMPARE(restored->scrollbar.offset, bottomOffset);
}

void TerminalEngineTests::searchesAcrossScrollbackAndWrappedLines()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 8, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    const std::u8string content = u8"Alpha one\r\nprefix alpha suffix\r\n中间\r\nlast ALPHA";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    auto search = engine.search("alpha", ztermy::terminal::TerminalSearchDirection::forward, false);
    if (!search)
    {
        QFAIL(search.error().message().c_str());
    }
    QCOMPARE(search->current, std::uint32_t{1});
    QCOMPARE(search->total, std::uint32_t{3});
    QVERIFY(!search->wrapped);

    const auto searchSnapshot = engine.snapshot();
    QVERIFY(searchSnapshot);
    QVERIFY(searchSnapshot->selectionPresent);
    QVERIFY(searchSnapshot->searchSelectionPresent);

    auto selected = engine.selectedText();
    QVERIFY(selected);
    QVERIFY(selected->has_value());
    QCOMPARE(**selected, "Alpha");

    search = engine.search("alpha", ztermy::terminal::TerminalSearchDirection::forward, false);
    QVERIFY(search);
    QCOMPARE(search->current, std::uint32_t{2});
    QCOMPARE(search->total, std::uint32_t{3});

    search = engine.search("alpha", ztermy::terminal::TerminalSearchDirection::backward, false);
    QVERIFY(search);
    QCOMPARE(search->current, std::uint32_t{1});

    const std::u8string_view unicodeQuery = u8"中间";
    search = engine.search(std::string_view(reinterpret_cast<const char *>(unicodeQuery.data()), unicodeQuery.size()),
                           ztermy::terminal::TerminalSearchDirection::forward, true);
    QVERIFY(search);
    QCOMPARE(search->current, std::uint32_t{1});
    QCOMPARE(search->total, std::uint32_t{1});

    QVERIFY(!engine.clearSearch());
    const auto clearedSnapshot = engine.snapshot();
    QVERIFY(clearedSnapshot);
    QVERIFY(!clearedSnapshot->searchSelectionPresent);
    selected = engine.selectedText();
    QVERIFY(selected);
    QVERIFY(!selected->has_value());
}

void TerminalEngineTests::encodesPasteForTerminalMode()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view text = "a\nb";
    auto encoded = engine.encodePaste(std::as_bytes(std::span(text)));
    if (!encoded)
    {
        QFAIL(encoded.error().message().c_str());
    }
    QCOMPARE(std::string(reinterpret_cast<const char *>(encoded->data()), encoded->size()), "a\rb");

    constexpr std::string_view enableBracketedPaste = "\x1b[?2004h";
    QVERIFY(!engine.feed(std::as_bytes(std::span(enableBracketedPaste))));
    encoded = engine.encodePaste(std::as_bytes(std::span(text)));
    if (!encoded)
    {
        QFAIL(encoded.error().message().c_str());
    }
    QCOMPARE(std::string(reinterpret_cast<const char *>(encoded->data()), encoded->size()), "\x1b[200~a\nb\x1b[201~");
}

void TerminalEngineTests::encodesKeysFromLiveTerminalModes()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    QVERIFY(result);
    auto &engine = **result;

    auto encoded = engine.encodeKey({.action = ztermy::terminal::TerminalKeyAction::press,
                                     .key = ztermy::terminal::TerminalKey::keyA,
                                     .text = "a",
                                     .unshiftedCodepoint = 'a'});
    QVERIFY(encoded);
    QCOMPARE(std::string(reinterpret_cast<const char *>(encoded->data()), encoded->size()), "a");

    encoded = engine.encodeKey(
        {.action = ztermy::terminal::TerminalKeyAction::press, .key = ztermy::terminal::TerminalKey::arrowUp});
    QVERIFY(encoded);
    QCOMPARE(std::string(reinterpret_cast<const char *>(encoded->data()), encoded->size()), "\x1b[A");

    constexpr std::string_view applicationCursor = "\x1b[?1h";
    QVERIFY(!engine.feed(std::as_bytes(std::span(applicationCursor))));
    encoded = engine.encodeKey(
        {.action = ztermy::terminal::TerminalKeyAction::press, .key = ztermy::terminal::TerminalKey::arrowUp});
    QVERIFY(encoded);
    QCOMPARE(std::string(reinterpret_cast<const char *>(encoded->data()), encoded->size()), "\x1bOA");
}

void TerminalEngineTests::encodesMouseAndFocusEventsFromLiveTerminalModes()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    QVERIFY(result);
    auto &engine = **result;

    const ztermy::terminal::TerminalMouseEvent mouse{
        .action = ztermy::terminal::TerminalMouseAction::press,
        .button = ztermy::terminal::TerminalMouseButton::left,
        .positionX = 0.0,
        .positionY = 0.0,
        .screenWidthPixels = 120,
        .screenHeightPixels = 60,
        .cellWidthPixels = 10,
        .cellHeightPixels = 20,
    };
    auto encoded = engine.encodeMouse(mouse);
    QVERIFY(encoded);
    QVERIFY(encoded->empty());

    constexpr std::string_view mouseModes = "\x1b[?1000h\x1b[?1006h";
    QVERIFY(!engine.feed(std::as_bytes(std::span(mouseModes))));
    encoded = engine.encodeMouse(mouse);
    QVERIFY(encoded);
    QCOMPARE(std::string(reinterpret_cast<const char *>(encoded->data()), encoded->size()), "\x1b[<0;1;1M");

    auto focus = engine.encodeFocus(true);
    QVERIFY(focus);
    QVERIFY(focus->empty());
    constexpr std::string_view focusMode = "\x1b[?1004h";
    QVERIFY(!engine.feed(std::as_bytes(std::span(focusMode))));
    focus = engine.encodeFocus(true);
    QVERIFY(focus);
    QCOMPARE(std::string(reinterpret_cast<const char *>(focus->data()), focus->size()), "\x1b[I");
    focus = engine.encodeFocus(false);
    QVERIFY(focus);
    QCOMPARE(std::string(reinterpret_cast<const char *>(focus->data()), focus->size()), "\x1b[O");
}

void TerminalEngineTests::reportsAlternateScrollMode()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 12, .rows = 3});
    QVERIFY(result);
    auto &engine = **result;

    auto snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QVERIFY(!snapshot->alternateScrollActive);
    constexpr std::string_view enable = "\x1b[?1007h\x1b[?1049h";
    QVERIFY(!engine.feed(std::as_bytes(std::span(enable))));
    snapshot = engine.snapshot();
    QVERIFY(snapshot);
    QVERIFY(snapshot->alternateScrollActive);
}

void TerminalEngineTests::pagesThroughScrollback()
{
    auto result = ztermy::terminal::GhosttyTerminalEngine::create({.columns = 16, .rows = 3});
    if (!result)
    {
        QFAIL(result.error().message().c_str());
    }
    auto &engine = **result;

    constexpr std::string_view content = "line-00\r\nline-11\r\nline-22\r\nline-33\r\nline-44\r\nline-55";
    QVERIFY(!engine.feed(std::as_bytes(std::span(content))));

    auto page = engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::head, .offset = 0, .lineCount = 10});
    if (!page)
    {
        QFAIL(page.error().message().c_str());
    }
    QCOMPARE(page->totalLines, std::size_t{6});
    QCOMPARE(page->scrollbackLines, std::size_t{3});
    QCOMPARE(page->lines.size(), std::size_t{6});
    QCOMPARE(page->lines.at(0), std::string("line-00"));
    QCOMPARE(page->lines.at(5), std::string("line-55"));

    // A bounded page from the middle.
    auto middle = engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::head, .offset = 2, .lineCount = 2});
    if (!middle)
    {
        QFAIL(middle.error().message().c_str());
    }
    QCOMPARE(middle->firstLine, std::size_t{2});
    QCOMPARE(middle->lines.size(), std::size_t{2});
    QCOMPARE(middle->lines.at(0), std::string("line-22"));
    QCOMPARE(middle->lines.at(1), std::string("line-33"));

    // Out-of-range requests return an empty page instead of failing.
    auto beyond = engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::head, .offset = 100, .lineCount = 4});
    if (!beyond)
    {
        QFAIL(beyond.error().message().c_str());
    }
    QVERIFY(beyond->lines.empty());
    QCOMPARE(beyond->firstLine, std::size_t{100});

    // Invalid arguments are rejected.
    QVERIFY(!engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::head, .offset = 0, .lineCount = 0}));

    auto tail = engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::tail, .offset = 0, .lineCount = 2});
    if (!tail)
    {
        QFAIL(tail.error().message().c_str());
    }
    QCOMPARE(tail->firstLine, std::size_t{4});
    QCOMPARE(tail->lines, std::vector<std::string>({"line-44", "line-55"}));

    auto olderTail = engine.scrollbackPage(
        {.anchor = ztermy::terminal::TerminalScrollbackAnchor::tail, .offset = 2, .lineCount = 2});
    if (!olderTail)
    {
        QFAIL(olderTail.error().message().c_str());
    }
    QCOMPARE(olderTail->firstLine, std::size_t{2});
    QCOMPARE(olderTail->lines, std::vector<std::string>({"line-22", "line-33"}));
}

} // namespace

QTEST_GUILESS_MAIN(TerminalEngineTests)

#include "terminal_engine_tests.moc"
