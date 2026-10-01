import Foundation

/// Decodes a `status` record using only the field table in `protocol.json`.
///
/// Rules from PROTOCOL.md §5: reject a wrong `schema` or a short record; decode the prefix
/// this spec knows and ignore trailing bytes; validity comes from `flags` via `valid_if`;
/// unknown flag bits and enum values are tolerated.
nonisolated struct SnapshotDecoder: Sendable {
    enum DecodeError: Error, Equatable, CustomStringConvertible {
        case tooShort(got: Int, need: Int)
        case wrongSchema(got: Int, want: Int)
        case declaredSizeTooSmall(Int)

        var description: String {
            switch self {
            case .tooShort(let got, let need): "snapshot too short: \(got) B, need \(need)"
            case .wrongSchema(let got, let want): "snapshot schema \(got), this app reads \(want) — update the app"
            case .declaredSizeTooSmall(let n): "snapshot declares size \(n), below this schema's minimum"
            }
        }
    }

    let spec: ProtocolSpec

    func decode(_ data: Data, receivedAt: Date = .now) throws -> Snapshot {
        let bytes = [UInt8](data)
        // An older firmware's shorter record is fine: fields past its end are simply absent.
        let need = spec.snapshot.requiredSize
        guard bytes.count >= need else { throw DecodeError.tooShort(got: bytes.count, need: need) }

        let fieldSpecs = spec.snapshot.fields
        if let f = fieldSpecs.first(where: { $0.name == "schema" }),
           let v = Self.read(bytes, f.type, at: f.off)?.integer, Int(v) != spec.snapshotSchema {
            throw DecodeError.wrongSchema(got: Int(v), want: spec.snapshotSchema)
        }
        if let f = fieldSpecs.first(where: { $0.name == "size" }),
           let v = Self.read(bytes, f.type, at: f.off)?.integer, Int(v) < need {
            throw DecodeError.declaredSizeTooSmall(Int(v))
        }

        // Flags first: every other field's validity depends on them.
        var flagWord: UInt64 = 0
        if let f = fieldSpecs.first(where: { $0.bitfield != nil }),
           let v = Self.read(bytes, f.type, at: f.off)?.integer {
            flagWord = UInt64(truncatingIfNeeded: v)
        }
        let names = spec.snapshot.flags
        var set: [String] = []
        var unknown: [Int] = []
        for bit in 0..<64 where flagWord & (1 << UInt64(bit)) != 0 {
            if bit < names.count { set.append(names[bit]) } else { unknown.append(bit) }
        }
        let setNames = Set(set)
        let knownNames = Set(names)

        var out: [DecodedField] = []
        out.reserveCapacity(fieldSpecs.count)
        for f in fieldSpecs {
            guard let raw = Self.read(bytes, f.type, at: f.off) else { continue }
            // A `valid_if` naming a flag this spec doesn't list can't be checked: trust the value.
            let valid = f.validIf.map { setNames.contains($0) || !knownNames.contains($0) } ?? true
            out.append(DecodedField(spec: f, raw: raw, isValid: valid,
                                    sentinel: Self.sentinel(f, raw),
                                    enumLabel: enumLabel(f, raw)))
        }
        return Snapshot(fields: out, flags: flagWord, flagsSet: set, unknownFlagBits: unknown,
                        raw: data, receivedAt: receivedAt)
    }

    private func enumLabel(_ f: ProtocolSpec.Field, _ raw: FieldValue) -> String? {
        guard let name = f.enumName, let v = raw.integer else { return nil }
        let labels = spec.snapshot.enums[name] ?? []
        return v >= 0 && v < labels.count ? labels[Int(v)] : "unknown(\(v))"
    }

    private static func sentinel(_ f: ProtocolSpec.Field, _ raw: FieldValue) -> String? {
        guard let map = f.sentinel, let n = raw.number else { return nil }
        return map.first { Double($0.key) == n }?.value
    }

    // MARK: Little-endian reads

    /// Byte width of a spec type (`u8`, `i16`, `f32`, `u8[28]`, …); nil if unknown.
    static func width(of type: String) -> Int? {
        if let open = type.firstIndex(of: "["), type.hasSuffix("]") {
            guard let elem = width(of: String(type[..<open])),
                  let n = Int(type[type.index(after: open)..<type.index(before: type.endIndex)])
            else { return nil }
            return elem * n
        }
        switch type {
        case "u8", "i8": return 1
        case "u16", "i16": return 2
        case "u32", "i32", "f32": return 4
        case "u64", "i64", "f64": return 8
        default: return nil
        }
    }

    /// Reads one value; nil when the type is unknown or the field runs past the data.
    static func read(_ b: [UInt8], _ type: String, at off: Int) -> FieldValue? {
        guard let w = width(of: type), off >= 0, off + w <= b.count else { return nil }
        if let open = type.firstIndex(of: "[") {
            // `u8[N]` stays raw bytes (pixels, …); wider elements are a list of numbers.
            let elem = String(type[..<open])
            guard elem != "u8", let ew = width(of: elem) else { return .bytes(Array(b[off..<off + w])) }
            return .list(stride(from: off, to: off + w, by: ew).compactMap { read(b, elem, at: $0) })
        }
        var u: UInt64 = 0
        for i in 0..<w { u |= UInt64(b[off + i]) << (8 * UInt64(i)) }
        switch type {
        case "f32": return .float(Double(Float(bitPattern: UInt32(u))))
        case "f64": return .float(Double(bitPattern: u))
        case let t where t.hasPrefix("i"):
            let shift = UInt64(64 - 8 * w)  // sign-extend
            return .int(Int64(bitPattern: u << shift) >> Int64(shift))
        default: return .uint(u)
        }
    }
}

nonisolated extension Data {
    /// Parses a hex string (whitespace ignored); nil on odd length or a bad digit.
    init?(hex: String) {
        let digits = hex.filter { !$0.isWhitespace }
        guard digits.count % 2 == 0 else { return nil }
        var out = Data(capacity: digits.count / 2)
        var it = digits.makeIterator()
        while let hi = it.next(), let lo = it.next() {
            guard let byte = UInt8(String([hi, lo]), radix: 16) else { return nil }
            out.append(byte)
        }
        self = out
    }
}
