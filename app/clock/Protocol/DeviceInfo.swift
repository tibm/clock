import Foundation

/// The `info` characteristic (PROTOCOL.md §6): space-separated `key=value`, unknown keys kept.
nonisolated struct DeviceInfo: Sendable, Equatable {
    /// Every pair, in wire order.
    let pairs: [(key: String, value: String)]

    init(_ text: String) {
        pairs = text.split(whereSeparator: \.isWhitespace).map { token in
            guard let eq = token.firstIndex(of: "=") else { return (String(token), "") }
            return (String(token[..<eq]), String(token[token.index(after: eq)...]))
        }
    }

    subscript(key: String) -> String? { pairs.first { $0.key == key }?.value }

    var firmware: String? { self["fw"] }
    var proto: Int? { self["proto"].flatMap(Int.init) }
    var schema: Int? { self["schema"].flatMap(Int.init) }

    enum Compatibility: Equatable {
        case match
        /// The clock speaks a newer protocol: "update the app".
        case appTooOld(clock: Int, app: Int)
        /// The clock is older: hide features marked newer.
        case firmwareOlder(clock: Int, app: Int)
        case unknown
    }

    func compatibility(appProto: Int) -> Compatibility {
        guard let proto else { return .unknown }
        if proto > appProto { return .appTooOld(clock: proto, app: appProto) }
        if proto < appProto { return .firmwareOlder(clock: proto, app: appProto) }
        return .match
    }

    static func == (a: DeviceInfo, b: DeviceInfo) -> Bool {
        a.pairs.map { "\($0.key)=\($0.value)" } == b.pairs.map { "\($0.key)=\($0.value)" }
    }
}
