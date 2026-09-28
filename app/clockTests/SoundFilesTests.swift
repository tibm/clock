import Foundation
import Testing
@testable import clock

struct SoundFilesTests {
    let spec: ProtocolSpec

    init() throws { spec = try ProtocolSpec.loadBundled() }

    /// A minimal WAV: 44-byte header + `samples` 16-bit frames.
    static func wav(rate: Int = 48000, channels: Int = 1, bits: Int = 16, format: Int = 1, samples: Int = 480) -> Data {
        var d = Data()
        func u16(_ v: Int) { withUnsafeBytes(of: UInt16(v).littleEndian) { d.append(contentsOf: $0) } }
        func u32(_ v: Int) { withUnsafeBytes(of: UInt32(v).littleEndian) { d.append(contentsOf: $0) } }
        let dataBytes = samples * channels * bits / 8
        d.append(contentsOf: Array("RIFF".utf8)); u32(36 + dataBytes); d.append(contentsOf: Array("WAVE".utf8))
        d.append(contentsOf: Array("fmt ".utf8)); u32(16); u16(format); u16(channels); u32(rate)
        u32(rate * channels * bits / 8); u16(channels * bits / 8); u16(bits)
        d.append(contentsOf: Array("data".utf8)); u32(dataBytes)
        d.append(Data(count: dataBytes))
        return d
    }

    @Test func contractHasSoundFiles() throws {
        let sf = try #require(spec.soundFiles)
        #expect(sf.format.sampleRate == 48000 && sf.format.channels == 1 && sf.format.bits == 16)
        #expect(spec.characteristicUUID("blob") != nil)
        #expect(spec.blobErrorName(0x80) == "busy")
        #expect(spec.blobErrorName(0x81) == "bad-offset")
        #expect(spec.blobErrorName(0x82) == "no-upload")
        #expect(spec.blobErrorName(0x83) == "failed")
        #expect(spec.blobErrorName(0x01) == nil)
        #expect(sf.toneStates.first == ToneList.playableState)
    }

    /// PROTOCOL.md: the CRC-32 check value.
    @Test func crcCheckValue() {
        #expect(SoundUpload.hex(SoundUpload.crc32(Data("123456789".utf8))) == "cbf43926")
        #expect(SoundUpload.hex(SoundUpload.crc32(Data())) == "00000000")
    }

    @Test func blobValueIsOffsetLEThenBytes() {
        let v = SoundUpload.blobValue(offset: 0x0102_0304, Data([0xAA, 0xBB]))
        #expect([UInt8](v) == [0x04, 0x03, 0x02, 0x01, 0xAA, 0xBB])
        #expect(SoundUpload.chunkSize(maxWrite: 512, blobMaxLen: 512) == 508)
        #expect(SoundUpload.chunkSize(maxWrite: 244, blobMaxLen: 512) == 240)
    }

    /// The example from PROTOCOL.md "Sound files".
    @Test func parsesToneList() {
        let list = ToneList(pairs: [
            ("card", "31914983424/31900000000"),
            ("alarm", "birds.wav"),
            ("tone", "19280/200/ok/birds.wav"),
            ("tone", "88244/1000/rate/cd.wav"),
            ("tone", "10/1/ok/a/b.wav"),
            ("tone", "garbage"),
            ("future", "x"),
        ])
        #expect(list.cardTotal == 31_914_983_424 && list.cardFree == 31_900_000_000)
        #expect(list.alarm == "birds.wav")
        #expect(list.tones.map(\.name) == ["birds.wav", "cd.wav", "a/b.wav"])
        #expect(list.tones[0] == .init(name: "birds.wav", size: 19280, durationMs: 200, state: "ok"))
        #expect(list.tones[0].playable && !list.tones[1].playable)
        #expect(ToneList(pairs: [("alarm", "")]).alarm == nil)
    }

    @Test func wavHeaderChecks() throws {
        let fmt = try #require(spec.soundFiles?.format)
        let good = try #require(WAVHeader(Self.wav()))
        #expect(good.problem(against: fmt) == nil)
        #expect(good.durationMs == 10)
        #expect(WAVHeader(Self.wav(rate: 44100))?.problem(against: fmt) != nil)
        #expect(WAVHeader(Self.wav(channels: 2))?.problem(against: fmt) != nil)
        #expect(WAVHeader(Self.wav(bits: 24))?.problem(against: fmt) != nil)
        #expect(WAVHeader(Self.wav(format: 3))?.problem(against: fmt) != nil)
        #expect(WAVHeader(Self.wav(samples: 0))?.problem(against: fmt) != nil)
        #expect(WAVHeader(Data("not a wav at all".utf8)) == nil)
    }
}
