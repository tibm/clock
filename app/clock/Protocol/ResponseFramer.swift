import Foundation

/// A request's terminal status (`<id>$<status>`). Open-ended: statuses this app doesn't
/// know yet still round-trip as their raw text.
nonisolated struct CommandStatus: RawRepresentable, Hashable, Sendable, CustomStringConvertible {
    let rawValue: String
    init(rawValue: String) { self.rawValue = rawValue }

    static let ok = CommandStatus(rawValue: "ok")
    static let badArg = CommandStatus(rawValue: "bad-arg")
    static let denied = CommandStatus(rawValue: "denied")
    static let busy = CommandStatus(rawValue: "busy")
    static let notReady = CommandStatus(rawValue: "not-ready")
    static let failed = CommandStatus(rawValue: "failed")
    static let notPresent = CommandStatus(rawValue: "not-present")

    var description: String { rawValue }
}

/// One reassembled record from the `rsp` characteristic (PROTOCOL.md §3).
nonisolated enum ResponseEvent: Sendable, Equatable {
    /// `|` — human text, display only.
    case line(id: UInt16, text: String)
    /// `=` — machine-readable `key=value`.
    case pair(id: UInt16, key: String, value: String)
    /// `$` — the request is finished.
    case terminal(id: UInt16, status: CommandStatus)
    /// A frame that doesn't follow the grammar; shown raw in the shell.
    case malformed(String)

    var id: UInt16? {
        switch self {
        case .line(let id, _), .pair(let id, _, _), .terminal(let id, _): id
        case .malformed: nil
        }
    }
}

/// Turns `rsp` notifications into records: `<id>` then one of `| + = $`, then text.
/// `+` fragments are appended per id until a `|` or `=` frame completes the record.
nonisolated struct ResponseFramer: Sendable {
    private var partial: [UInt16: String] = [:]

    mutating func feed(_ data: Data) -> [ResponseEvent] {
        feed(String(decoding: data, as: UTF8.self))
    }

    mutating func feed(_ frame: String) -> [ResponseEvent] {
        let digits = frame.prefix { $0.isASCII && $0.isNumber }
        guard !digits.isEmpty, let id = UInt16(digits),
              let kind = frame.dropFirst(digits.count).first else {
            return [.malformed(frame)]
        }
        let body = String(frame.dropFirst(digits.count + 1))
        switch kind {
        case "+":
            partial[id, default: ""] += body
            return []
        case "|":
            return [.line(id: id, text: take(id) + Self.trimEOL(body))]
        case "=":
            let record = take(id) + Self.trimEOL(body)
            guard let eq = record.firstIndex(of: "=") else { return [.pair(id: id, key: record, value: "")] }
            return [.pair(id: id, key: String(record[..<eq]), value: String(record[record.index(after: eq)...]))]
        case "$":
            // A dangling fragment (link hiccup) is still worth showing before the status.
            var out: [ResponseEvent] = []
            if let rest = partial.removeValue(forKey: id), !rest.isEmpty { out.append(.line(id: id, text: rest)) }
            out.append(.terminal(id: id, status: CommandStatus(rawValue: Self.trimEOL(body))))
            return out
        default:
            return [.malformed(frame)]
        }
    }

    /// Drops half-received records (call on disconnect).
    mutating func reset() { partial.removeAll() }

    private mutating func take(_ id: UInt16) -> String { partial.removeValue(forKey: id) ?? "" }

    private static func trimEOL(_ s: String) -> String {
        var s = Substring(s)
        while let last = s.last, last == "\n" || last == "\r" { s = s.dropLast() }
        return String(s)
    }
}
