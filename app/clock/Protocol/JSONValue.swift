import Foundation

/// Any JSON value. Used for the parts of `protocol.json` whose shape is open-ended
/// (the golden vector's `decoded` map), so a new key in the contract never breaks loading.
nonisolated enum JSONValue: Decodable, Sendable, Equatable {
    case null
    case bool(Bool)
    case number(Double)
    case string(String)
    case array([JSONValue])
    case object([String: JSONValue])

    init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer()
        if c.decodeNil() { self = .null }
        else if let b = try? c.decode(Bool.self) { self = .bool(b) }
        else if let n = try? c.decode(Double.self) { self = .number(n) }
        else if let s = try? c.decode(String.self) { self = .string(s) }
        else if let a = try? c.decode([JSONValue].self) { self = .array(a) }
        else { self = .object(try c.decode([String: JSONValue].self)) }
    }

    var number: Double? { if case .number(let n) = self { n } else { nil } }
    var string: String? { if case .string(let s) = self { s } else { nil } }
    var array: [JSONValue]? { if case .array(let a) = self { a } else { nil } }
}
