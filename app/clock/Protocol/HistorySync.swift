import Foundation

/// The pure half of PROTOCOL.md "History → Downloading": what to fetch, and reassembling `bulk`.
nonisolated enum HistorySync {
    /// One `log fetch <day> <from>` to run.
    struct Fetch: Sendable, Equatable {
        /// `yyyymmdd`, UTC.
        let day: String
        let from: Int
        /// The file's size on the clock when listed.
        let size: Int

        var bytes: Int { max(0, size - from) }
    }

    /// `log days` pairs: `day=<yyyymmdd>/<bytes>`. Anything else (newer keys, bad values) is skipped.
    static func days(_ pairs: [(key: String, value: String)]) -> [(day: String, bytes: Int)] {
        pairs.compactMap { key, value in
            guard key == "day" else { return nil }
            let parts = value.split(separator: "/")
            guard parts.count == 2, parts[0].count == 8, parts[0].allSatisfy(\.isNumber),
                  let n = Int(parts[1]), n >= 0 else { return nil }
            return (String(parts[0]), n)
        }
    }

    /// `FileSync.plan` by day, oldest first, so today comes last.
    static func plan(clock: [(day: String, bytes: Int)], local: [String: Int]) -> [Fetch] {
        FileSync.plan(clock: clock.map { ($0.day, $0.bytes) }, local: local)
            .map { Fetch(day: $0.name, from: $0.from, size: $0.size) }
            .sorted { $0.day < $1.day }
    }

    /// One `bulk` notification: 4-byte LE file offset, then data. Nil when shorter than the offset.
    static func packet(_ value: Data) -> (offset: Int, payload: Data)? {
        guard value.count >= 4 else { return nil }
        let b = [UInt8](value.prefix(4))
        let off = Int(b[0]) | Int(b[1]) << 8 | Int(b[2]) << 16 | Int(b[3]) << 24
        return (off, value.dropFirst(4))
    }
}

/// The mirror rule shared by history day files and journal files: what to fetch, by file name.
nonisolated enum FileSync {
    /// One fetch of `[from, size)` of a file.
    struct Fetch: Sendable, Equatable {
        let name: String
        let from: Int
        /// The file's size on the clock when listed.
        let size: Int

        var bytes: Int { max(0, size - from) }
    }

    /// Not on the phone → from 0; longer on the clock → from the local length; shorter on the
    /// clock (card replaced, counter restarted) → from 0, replacing. Files only on the phone are
    /// kept (never deleted). In the clock's order.
    static func plan(clock: [(name: String, bytes: Int)], local: [String: Int]) -> [Fetch] {
        clock.compactMap { name, bytes in
            switch local[name] {
            case nil: Fetch(name: name, from: 0, size: bytes)
            case let have? where have < bytes: Fetch(name: name, from: have, size: bytes)
            case let have? where have > bytes: Fetch(name: name, from: 0, size: bytes)
            default: nil
            }
        }
    }
}

/// Collects the `bulk` packets of one `log fetch` into `[from, size)`.
///
/// Notifications arrive in order and without loss on a live link, so the buffer only grows at
/// its end: a repeat is ignored, a jump past the end marks a gap and the fetch is retried.
nonisolated struct BulkAssembler: Sendable {
    let from: Int
    private(set) var data = Data()
    /// Known once `log fetch` answers `=size=`; packets may arrive before that.
    private(set) var size: Int?
    private(set) var gap = false

    init(from: Int) { self.from = from }

    /// File offset of the next byte expected.
    var next: Int { from + data.count }
    var isComplete: Bool { !gap && size.map { next >= $0 } == true }

    mutating func expect(size: Int) {
        self.size = size
        if next > size { data = data.prefix(max(0, size - from)) }
    }

    /// Adds one packet; returns the number of new bytes.
    @discardableResult
    mutating func add(offset: Int, _ payload: Data) -> Int {
        guard !gap else { return 0 }
        if offset > next { gap = true; return 0 }
        let skip = next - offset
        guard skip < payload.count else { return 0 }
        var fresh = payload.dropFirst(skip)
        if let size { fresh = fresh.prefix(max(0, size - next)) }
        data.append(fresh)
        return fresh.count
    }
}
