#include "platform/windows/WindowsTerminalTextCodec.h"

#include <QTest>

#include <array>
#include <span>

namespace
{

[[nodiscard]] std::span<const std::byte> bytesOf(const QByteArray &bytes)
{
    return std::as_bytes(std::span(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

} // namespace

class TerminalTextCodecTests final : public QObject
{
    Q_OBJECT

private slots:
    void acceptsCanonicalTokens();
    void roundTripsUtf8AndGb18030();
    void preservesSplitMultibyteSequences();
    void replacesMalformedRemoteBytes();
    void rejectsMalformedUtf8Input();
};

void TerminalTextCodecTests::acceptsCanonicalTokens()
{
    using ztermy::terminal::TerminalEncoding;
    QCOMPARE(ztermy::terminal::terminalEncodingFromToken(QStringLiteral("UTF-8")).value(), TerminalEncoding::Utf8);
    QCOMPARE(ztermy::terminal::terminalEncodingFromToken(QStringLiteral("gb18030")).value(), TerminalEncoding::Gb18030);
    QVERIFY(!ztermy::terminal::terminalEncodingFromToken(QStringLiteral("shift-jis")));
}

void TerminalTextCodecTests::roundTripsUtf8AndGb18030()
{
    ztermy::terminal::WindowsTerminalTextCodec codec;
    const QByteArray source = QStringLiteral("ASCII 你好，终端 𠀀").toUtf8();
    QCOMPARE(codec.encodeRemote(source).value(), source);

    codec.setEncoding(ztermy::terminal::TerminalEncoding::Gb18030);
    const auto encoded = codec.encodeRemote(source);
    QVERIFY(encoded);
    QVERIFY(*encoded != source);
    QCOMPARE(codec.decodeRemote(bytesOf(*encoded)), source);
}

void TerminalTextCodecTests::preservesSplitMultibyteSequences()
{
    ztermy::terminal::WindowsTerminalTextCodec codec;
    codec.setEncoding(ztermy::terminal::TerminalEncoding::Gb18030);
    const QByteArray source = QStringLiteral("你𠀀好").toUtf8();
    const QByteArray encoded = codec.encodeRemote(source).value();

    for (qsizetype split = 1; split < encoded.size(); ++split)
    {
        codec.reset();
        const QByteArray first = codec.decodeRemote(bytesOf(encoded.first(split)));
        const QByteArray second = codec.decodeRemote(bytesOf(encoded.sliced(split)));
        QCOMPARE(first + second, source);
    }
}

void TerminalTextCodecTests::replacesMalformedRemoteBytes()
{
    ztermy::terminal::WindowsTerminalTextCodec codec;
    codec.setEncoding(ztermy::terminal::TerminalEncoding::Gb18030);
    const QByteArray malformed = QByteArray::fromHex("ff41");
    QCOMPARE(QString::fromUtf8(codec.decodeRemote(bytesOf(malformed))), QString(QChar::ReplacementCharacter) + u'A');
}

void TerminalTextCodecTests::rejectsMalformedUtf8Input()
{
    ztermy::terminal::WindowsTerminalTextCodec codec;
    codec.setEncoding(ztermy::terminal::TerminalEncoding::Gb18030);
    QVERIFY(!codec.encodeRemote(QByteArray::fromHex("c328")));
}

QTEST_MAIN(TerminalTextCodecTests)

#include "terminal_text_codec_tests.moc"
