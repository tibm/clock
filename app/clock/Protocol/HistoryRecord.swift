import Foundation

/// A quantity recorded in every sample (`temp`, `gas`, …), in record order.
nonisolated struct HistoryColumn: Sendable, Identifiable, Equatable {
    var id: String { name }
    let name: String
    let unit: String?
    let note: String?
}

/// One sample record (PROTOCOL.md "History"): the averages over one period.
nonisolated struct HistorySample: Sendable {
    /// Start of the period.
    let t: Date
    /// One value per `HistoryDecoder.columns`; NaN = its validity flag is clear (or a sentinel).
    let values: [Float]
    let flags: UInt64

    func value(_ column: Int) -> Double? {
        guard column >= 0, column < values.count, !values[column].isNaN else { return nil }
        return Double(values[column])
    }
}

/// One event record: something that happened at `t`.
nonisolated struct HistoryEvent: Sendable, Identifiable, Equatable {
    var id: String { "\(t.timeIntervalSince1970)-\(code)-\(args)" }
    let t: Date
    /// `event_code` name, `unknown(n)` for codes newer than this spec.
    let code: String
    /// The arg bytes, trailing zeros dropped.
    let args: [UInt8]
}

/// A day file's 32-byte header.
nonisolated struct HistoryHeader: Sendable {
    let values: [String: FieldValue]

    var periodS: Int? { values["period_s"]?.integer.map(Int.init) }
    /// 00:00 UTC of the file's day.
    var day: Date? { values["day"]?.integer.map { Date(timeIntervalSince1970: Double($0)) } }
    var fwID: String? { values["fw_id"]?.integer.map { String(format: "%08x", UInt32(truncatingIfNeeded: $0)) } }
}

nonisolated struct HistoryFile: Sendable {
    let header: HistoryHeader
    var samples: [HistorySample] = []
    var events: [HistoryEvent] = []
    /// Records skipped for a bad CRC-8 (torn writes).
    var badCRC = 0
    /// Records of a kind this spec doesn't know (newer firmware).
    var unknownKind = 0
}

/// Decodes history day files using only `protocol.json` → `history` (PROTOCOL.md "Decoding a file").
nonisolated struct HistoryDecoder: Sendable {
    enum DecodeError: Error, Equatable, CustomStringConvertible {
        case tooShort(Int)
        case badHeader(field: String, got: String, want: String)

        var description: String {
            switch self {
            case .tooShort(let n): "history file too short: \(n) B"
            case .badHeader(let f, let got, let want): "history header \(f) = \(got), this app reads \(want)"
            }
        }
    }

    let spec: ProtocolSpec.History
    /// The plottable sample fields, in record order.
    let columns: [HistoryColumn]

    private let columnFields: [ProtocolSpec.HistoryField]
    private let encodings: [String: Encoding]
    private let sampleKind: Int?
    private let eventKind: Int?

    /// `10^(v / div) − sub`, the one shape the contract's log encodings take.
    struct Encoding: Sendable, Equatable {
        let div: Double
        let sub: Double

        func apply(_ v: Double) -> Double { pow(10, v / div) - sub }

        /// Parses e.g. `lux = 10^(v / 12000) - 1`; nil for any other shape (the field then stays empty).
        init?(_ formula: String) {
            guard let rhs = formula.split(separator: "=").last,
                  let m = rhs.firstMatch(of: /10\^\(\s*v\s*\/\s*([0-9.]+)\s*\)\s*(?:-\s*([0-9.]+))?/),
                  let div = Double(m.1), div != 0 else { return nil }
            self.div = div
            self.sub = m.2.flatMap { Double($0) } ?? 0
        }
    }

    init(spec: ProtocolSpec.History) {
        self.spec = spec
        let crcOff = spec.record.size - 1
        // Every numeric sample field except the time, the kind byte, the flag word and the CRC.
        columnFields = spec.record.sample.filter { f in
            f.name != "t" && f.value == nil && f.bitfield == nil && f.off != crcOff
                && !f.type.contains("[") && SnapshotDecoder.width(of: f.type) != nil
        }
        columns = columnFields.map { HistoryColumn(name: $0.name, unit: $0.unit, note: $0.note) }
        encodings = spec.encodings.compactMapValues(Encoding.init)
        sampleKind = spec.record.kinds.first { $0.value == "sample" }.flatMap { Int($0.key) }
        eventKind = spec.record.kinds.first { $0.value == "event" }.flatMap { Int($0.key) }
    }

    func column(_ name: String) -> Int? { columns.firstIndex { $0.name == name } }

    /// Names of the sample flags set in `word`, in bit order.
    func flagNames(_ word: UInt64) -> [String] {
        let names = sampleFlagNames
        return (0..<64).compactMap { bit in
            guard word & (1 << UInt64(bit)) != 0 else { return nil }
            return bit < names.count ? names[bit] : "bit \(bit)"
        }
    }

    private var sampleFlagNames: [String] {
        spec.record.sample.first { $0.bitfield != nil }.flatMap { spec.lists[$0.bitfield!] } ?? []
    }

    // MARK: Files

    /// Header, then every record; bad-CRC and unknown records are skipped, a torn tail ignored.
    func decode(_ data: Data) throws -> HistoryFile {
        let b = [UInt8](data)
        var file = HistoryFile(header: try header(b))
        let size = spec.record.size
        var off = spec.header.size
        while off + size <= b.count {
            defer { off += size }
            guard Self.crc8(b[off..<off + size - 1]) == b[off + size - 1] else { file.badCRC += 1; continue }
            let kind = Int(b[off])
            if kind == sampleKind, let s = sample(b, at: off) {
                file.samples.append(s)
            } else if kind == eventKind, let e = event(b, at: off) {
                file.events.append(e)
            } else {
                file.unknownKind += 1
            }
        }
        return file
    }

    func header(_ b: [UInt8]) throws -> HistoryHeader {
        guard b.count >= spec.header.size else { throw DecodeError.tooShort(b.count) }
        var values: [String: FieldValue] = [:]
        for f in spec.header.fields {
            if f.type.hasPrefix("char["), let w = SnapshotDecoder.width(of: "u8" + f.type.dropFirst(4)) {
                guard f.off + w <= b.count else { continue }
                let text = String(decoding: b[f.off..<f.off + w], as: UTF8.self)
                if let want = f.value?.string, text != want {
                    throw DecodeError.badHeader(field: f.name, got: text, want: want)
                }
                values[f.name] = .bytes(Array(b[f.off..<f.off + w]))
                continue
            }
            guard let v = SnapshotDecoder.read(b, f.type, at: f.off) else { continue }
            if let want = f.value?.number, v.number != want {
                throw DecodeError.badHeader(field: f.name, got: v.number.map { String(Int64($0)) } ?? "?",
                                            want: String(Int64(want)))
            }
            values[f.name] = v
        }
        return HistoryHeader(values: values)
    }

    // MARK: Records

    /// One sample record starting at `base` (CRC already checked).
    func sample(_ b: [UInt8], at base: Int = 0) -> HistorySample? {
        guard let t = time(b, base, spec.record.sample) else { return nil }
        var flags: UInt64 = 0
        if let f = spec.record.sample.first(where: { $0.bitfield != nil }),
           let v = SnapshotDecoder.read(b, f.type, at: base + f.off)?.integer {
            flags = UInt64(truncatingIfNeeded: v)
        }
        let flagNames = sampleFlagNames
        let values = columnFields.map { f -> Float in
            // A `valid_if` naming a flag this spec doesn't list can't be checked: trust the value.
            if let need = f.validIf, let bit = flagNames.firstIndex(of: need), flags & (1 << UInt64(bit)) == 0 {
                return .nan
            }
            guard let raw = SnapshotDecoder.read(b, f.type, at: base + f.off)?.number else { return .nan }
            if let s = f.sentinel, s.keys.contains(where: { Double($0) == raw }) { return .nan }
            if let name = f.encoding {
                guard let e = encodings[name] else { return .nan }
                return Float(e.apply(raw))
            }
            return Float(raw * (f.scale ?? 1))
        }
        return HistorySample(t: t, values: values, flags: flags)
    }

    /// One event record starting at `base` (CRC already checked).
    func event(_ b: [UInt8], at base: Int = 0) -> HistoryEvent? {
        let fields = spec.record.event
        guard let t = time(b, base, fields) else { return nil }
        var code = "?"
        if let f = fields.first(where: { $0.enumName != nil }),
           let v = SnapshotDecoder.read(b, f.type, at: base + f.off)?.integer {
            let labels = f.enumName.flatMap { spec.lists[$0] } ?? []
            code = v >= 0 && v < labels.count ? labels[Int(v)] : "unknown(\(v))"
        }
        var args: [UInt8] = []
        if let f = fields.first(where: { $0.name == "args" }),
           case .bytes(let bytes)? = SnapshotDecoder.read(b, f.type, at: base + f.off) {
            args = Array(bytes.reversed().drop { $0 == 0 }.reversed())
        }
        return HistoryEvent(t: t, code: code, args: args)
    }

    private func time(_ b: [UInt8], _ base: Int, _ fields: [ProtocolSpec.HistoryField]) -> Date? {
        guard let f = fields.first(where: { $0.name == "t" }),
              let v = SnapshotDecoder.read(b, f.type, at: base + f.off)?.integer else { return nil }
        return Date(timeIntervalSince1970: Double(v))
    }

    /// CRC-8/SMBUS: poly 0x07, init 0, not reflected (PROTOCOL.md "Decoding a file").
    static func crc8(_ bytes: some Sequence<UInt8>) -> UInt8 {
        var crc: UInt8 = 0
        for byte in bytes {
            crc ^= byte
            for _ in 0..<8 { crc = crc & 0x80 != 0 ? (crc << 1) ^ 0x07 : crc << 1 }
        }
        return crc
    }
}
