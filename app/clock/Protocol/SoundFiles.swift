import Foundation
import zlib

/// `storage tones` output (PROTOCOL.md §4 "Sound files"): the `=` pairs, never the `|` lines.
nonisolated struct ToneList: Sendable, Equatable {
    struct Tone: Identifiable, Sendable, Equatable {
        var id: String { name }
        let name: String
        let size: Int
        let durationMs: Int
        /// `ok` = plays; anything else says why not (`rate`, `channels`, …).
        let state: String

        var playable: Bool { state == ToneList.playableState }
    }

    static let playableState = "ok"

    var cardTotal: Int64?
    var cardFree: Int64?
    /// The alarm tone; nil = the built-in beep.
    var alarm: String?
    var tones: [Tone] = []

    init(pairs: [(key: String, value: String)]) {
        for (key, value) in pairs {
            switch key {
            case "card":
                let parts = value.split(separator: "/")
                if parts.count == 2 { cardTotal = Int64(parts[0]); cardFree = Int64(parts[1]) }
            case "alarm":
                alarm = value.isEmpty ? nil : value
            case "tone":
                if let t = Self.tone(value) { tones.append(t) }
            default:
                break  // a newer key: ignore
            }
        }
    }

    /// `<bytes>/<ms>/<state>/<name>` — the name is last and may itself contain `/`.
    static func tone(_ value: String) -> Tone? {
        let parts = value.split(separator: "/", maxSplits: 3, omittingEmptySubsequences: false)
        guard parts.count == 4, let size = Int(parts[0]), let ms = Int(parts[1]), !parts[3].isEmpty else { return nil }
        return Tone(name: String(parts[3]), size: size, durationMs: ms, state: String(parts[2]))
    }

    func tone(named name: String) -> Tone? { tones.first { $0.name == name } }
}

/// The fields of a RIFF/WAVE header the clock checks.
nonisolated struct WAVHeader: Sendable, Equatable {
    let audioFormat: Int
    let channels: Int
    let sampleRate: Int
    let bits: Int
    /// Size of the `data` chunk, nil when there is none.
    let dataBytes: Int?

    init?(_ data: Data) {
        let b = [UInt8](data.prefix(4096))
        func u16(_ i: Int) -> Int { Int(b[i]) | Int(b[i + 1]) << 8 }
        func u32(_ i: Int) -> Int { u16(i) | u16(i + 2) << 16 }
        func tag(_ i: Int) -> String { String(decoding: b[i..<i + 4], as: UTF8.self) }
        guard b.count >= 12, tag(0) == "RIFF", tag(8) == "WAVE" else { return nil }
        var fmt: (Int, Int, Int, Int)?
        var dataBytes: Int?
        var i = 12
        while i + 8 <= b.count {
            let size = u32(i + 4)
            switch tag(i) {
            case "fmt ":
                guard i + 8 + 16 <= b.count else { return nil }
                fmt = (u16(i + 8), u16(i + 10), u32(i + 12), u16(i + 22))
            case "data":
                dataBytes = size
            default:
                break
            }
            if dataBytes != nil { break }
            i += 8 + size + (size & 1)  // chunks are word-aligned
        }
        guard let fmt else { return nil }
        (audioFormat, channels, sampleRate, bits) = fmt
        self.dataBytes = dataBytes
    }

    var durationMs: Int? {
        guard let dataBytes, channels > 0, bits > 0, sampleRate > 0 else { return nil }
        return dataBytes * 1000 / (sampleRate * channels * bits / 8)
    }

    /// Why the clock would refuse this file, or nil when it matches the contract's format.
    func problem(against f: ProtocolSpec.AudioFormat) -> String? {
        if audioFormat != f.audioFormat { return "not PCM (format \(audioFormat))" }
        if sampleRate != f.sampleRate { return "\(sampleRate) Hz, needs \(f.sampleRate)" }
        if channels != f.channels { return "\(channels) channels, needs \(f.channels)" }
        if bits != f.bits { return "\(bits)-bit, needs \(f.bits)" }
        if (dataBytes ?? 0) == 0 { return "no audio data" }
        return nil
    }
}

nonisolated enum SoundUpload {
    /// zlib CRC-32 of the whole file, as `storage put` wants it.
    static func crc32(_ data: Data) -> UInt32 {
        data.withUnsafeBytes { raw in
            var crc = zlib.crc32(0, nil, 0)
            var rest = UnsafeRawBufferPointer(raw)
            // zlib takes a uInt length: feed large files in slices.
            while !rest.isEmpty {
                let n = min(rest.count, 1 << 30)
                crc = zlib.crc32(crc, rest.baseAddress?.assumingMemoryBound(to: Bytef.self), uInt(n))
                rest = UnsafeRawBufferPointer(rebasing: rest[n...])
            }
            return UInt32(crc)
        }
    }

    static func hex(_ crc: UInt32) -> String {
        let s = String(crc, radix: 16)
        return String(repeating: "0", count: 8 - s.count) + s
    }

    /// One `blob` write: the offset as a 4-byte little-endian uint32, then the bytes.
    static func blobValue(offset: Int, _ bytes: Data) -> Data {
        var out = Data(capacity: 4 + bytes.count)
        withUnsafeBytes(of: UInt32(offset).littleEndian) { out.append(contentsOf: $0) }
        out.append(bytes)
        return out
    }

    /// File bytes per `blob` write: the largest write less the 4-byte offset.
    static func chunkSize(maxWrite: Int, blobMaxLen: Int?) -> Int {
        max(1, min(maxWrite, blobMaxLen ?? maxWrite) - 4)
    }
}
