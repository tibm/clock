import Foundation

/// A raw field value, read with the type the spec gives it.
nonisolated enum FieldValue: Sendable, Equatable {
    case int(Int64)
    case uint(UInt64)
    case float(Double)
    case bytes([UInt8])

    /// Numeric value before scaling; nil for byte arrays.
    var number: Double? {
        switch self {
        case .int(let v): Double(v)
        case .uint(let v): Double(v)
        case .float(let v): v
        case .bytes: nil
        }
    }

    /// Integer value, for enums / bitfields / sentinels; nil for floats and byte arrays.
    var integer: Int64? {
        switch self {
        case .int(let v): v
        case .uint(let v): Int64(truncatingIfNeeded: v)
        case .float, .bytes: nil
        }
    }
}

/// One snapshot field, decoded and interpreted per its spec entry.
nonisolated struct DecodedField: Sendable, Identifiable {
    let spec: ProtocolSpec.Field
    let raw: FieldValue
    /// False when the field's `valid_if` flag is clear: the value is garbage, show "—".
    let isValid: Bool
    /// The sentinel meaning, when the raw value is one (e.g. `soc_pct == 255`).
    let sentinel: String?
    /// The enum label (`unknown(n)` for values newer than this spec).
    let enumLabel: String?

    var id: String { spec.name }
    var name: String { spec.name }

    /// Scaled numeric value; nil when invalid, a sentinel, or not numeric.
    var value: Double? {
        guard isValid, sentinel == nil, let n = raw.number else { return nil }
        return n * (spec.scale ?? 1)
    }

    /// Human text for the value, including its unit. "—" when invalid.
    var display: String {
        guard isValid else { return "—" }
        if let sentinel { return sentinel }
        if let enumLabel { return enumLabel }
        switch raw {
        case .bytes(let b):
            return b.map { String(format: "%02x", $0) }.joined(separator: " ")
        case .float(let f):
            return Self.withUnit(String(format: "%.2f", f * (spec.scale ?? 1)), spec.unit)
        case .int, .uint:
            guard let n = raw.number else { return "?" }
            if let scale = spec.scale {
                return Self.withUnit(String(format: "%.\(Self.decimals(for: scale))f", n * scale), spec.unit)
            }
            return Self.withUnit(String(Int64(n)), spec.unit)
        }
    }

    /// Enough decimals to show one step of `scale` (0.01 → 2, 0.1 → 1), capped at 4.
    private static func decimals(for scale: Double) -> Int {
        guard scale > 0, scale < 1 else { return 0 }
        return min(4, Int((-log10(scale)).rounded(.up)))
    }

    private static func withUnit(_ s: String, _ unit: String?) -> String {
        guard let unit, !unit.isEmpty else { return s }
        return "\(s) \(unit)"
    }
}

/// One decoded `status` record (PROTOCOL.md §5).
nonisolated struct Snapshot: Sendable {
    /// Every field this app's spec knows, in wire order.
    let fields: [DecodedField]
    /// Raw `flags` word.
    let flags: UInt64
    /// Names of the set flags that the spec knows, in bit order.
    let flagsSet: [String]
    /// Set bits the spec has no name for (newer firmware).
    let unknownFlagBits: [Int]
    let raw: Data
    let receivedAt: Date

    private let index: [String: Int]

    init(fields: [DecodedField], flags: UInt64, flagsSet: [String], unknownFlagBits: [Int],
         raw: Data, receivedAt: Date) {
        self.fields = fields
        self.flags = flags
        self.flagsSet = flagsSet
        self.unknownFlagBits = unknownFlagBits
        self.raw = raw
        self.receivedAt = receivedAt
        self.index = Dictionary(fields.enumerated().map { ($1.name, $0) }, uniquingKeysWith: { a, _ in a })
    }

    /// A field by protocol name; nil if this snapshot/spec doesn't have it.
    subscript(name: String) -> DecodedField? { index[name].map { fields[$0] } }

    func has(_ flag: String) -> Bool { flagsSet.contains(flag) }

    /// Display string for a field by name ("—" when missing or invalid).
    func text(_ name: String) -> String { self[name]?.display ?? "—" }

    /// Integer raw value by name, only when valid.
    func int(_ name: String) -> Int? {
        guard let f = self[name], f.isValid, let v = f.raw.integer else { return nil }
        return Int(v)
    }

    /// Local time per PROTOCOL.md §5 "Time": `epoch_ms + tz_off_min × 60 000`, to be formatted
    /// as UTC. `hasDate` mirrors `date_valid`: when false only the time of day means anything.
    var localTime: (date: Date, hasDate: Bool)? {
        guard let epoch = self["epoch_ms"], epoch.isValid, let ms = epoch.raw.integer else { return nil }
        // tz_off_min's valid_if is tz_set; with no offset given the firmware sends 0 anyway.
        let off = Int64(self["tz_off_min"]?.raw.integer ?? 0) * 60_000
        return (Date(timeIntervalSince1970: Double(ms + off) / 1000), has("date_valid"))
    }

    /// The seven LEDs as RGBW quadruples, in chain order.
    var pixels: [[UInt8]] {
        guard case .bytes(let b)? = self["pixels"]?.raw else { return [] }
        return stride(from: 0, to: b.count - 3, by: 4).map { Array(b[$0..<$0 + 4]) }
    }
}
