import Foundation

/// The pure half of PROTOCOL.md "Debug journal → Downloading": the clock's file list, what to
/// fetch, and how a boot's files fit together.
nonisolated enum JournalSync {
    /// `sys journal files`.
    struct Listing: Sendable {
        /// Oldest first, as the clock lists them.
        var files: [(name: String, bytes: Int)] = []
        /// This boot's number.
        var boot: Int?
        /// The one file still growing.
        var current: String?
    }

    /// `file=<name>/<bytes>` (split on the last `/`), `boot=<n>`, `current=<name>`. Anything
    /// else (newer keys, bad values) is skipped.
    static func listing(_ pairs: [(key: String, value: String)]) -> Listing {
        var l = Listing()
        for (key, value) in pairs {
            switch key {
            case "file":
                guard let slash = value.lastIndex(of: "/"), slash > value.startIndex,
                      let n = Int(value[value.index(after: slash)...]), n >= 0 else { continue }
                l.files.append((String(value[..<slash]), n))
            case "boot": l.boot = Int(value)
            case "current": l.current = value.isEmpty ? nil : value
            default: break
            }
        }
        return l
    }

    /// `FileSync.plan` in the clock's order (oldest first, so `current` comes last).
    static func plan(_ listing: Listing, local: [String: Int]) -> [FileSync.Fetch] {
        FileSync.plan(clock: listing.files, local: local)
    }
}

/// One journal file name, parsed with `journal.name_regex`.
nonisolated struct JournalFile: Sendable, Hashable, Comparable {
    let name: String
    let boot: Int
    /// 0 = the boot's first file.
    let part: Int
    /// `<boot>.old`: written by firmware before 2026-10-01.
    let old: Bool

    /// Boot, then `.old`, then `.log`, then part (`journal.name_parts`).
    static func < (a: JournalFile, b: JournalFile) -> Bool {
        (a.boot, a.old ? 0 : 1, a.part, a.name) < (b.boot, b.old ? 0 : 1, b.part, b.name)
    }
}

/// One boot on the phone: its files in order.
nonisolated struct JournalBoot: Sendable, Identifiable, Equatable {
    let boot: Int
    var id: Int { boot }
    let files: [JournalFile]
    let bytes: Int
    /// Holds the clock's `current` file.
    let current: Bool
    /// From the header of its first file: why this boot started.
    var reset: String?
    /// One of its files has the "before the reset" section.
    var rescued = false
    /// The reset reason of the boot after it: how this one ended.
    var endedBy: String?
}

/// The contract's `journal` block, with its regexes compiled.
nonisolated struct JournalFormat: Sendable {
    let spec: ProtocolSpec.Journal
    let nameRE: NSRegularExpression
    let lineRE: NSRegularExpression

    /// Nil when a regex doesn't compile.
    init?(_ spec: ProtocolSpec.Journal) {
        guard let n = try? NSRegularExpression(pattern: spec.nameRegex),
              let l = try? NSRegularExpression(pattern: spec.lineRegex) else { return nil }
        self.spec = spec
        nameRE = n
        lineRE = l
    }

    /// Nil when it isn't a journal file name (also keeps clock-supplied names safe as paths).
    func file(_ name: String) -> JournalFile? {
        let all = NSRange(name.startIndex..., in: name)
        guard let m = nameRE.firstMatch(in: name, range: all), m.range == all,
              let boot = Self.group(m, 1, in: name).flatMap({ Int($0) }) else { return nil }
        return JournalFile(name: name, boot: boot,
                           part: Self.group(m, 2, in: name).flatMap { Int($0) } ?? 0,
                           old: Self.group(m, 3, in: name) == "old")
    }

    /// The stored files grouped by boot, each boot's files in order; newest boot first.
    func boots(sizes: [String: Int], current: String?) -> [JournalBoot] {
        let files = sizes.keys.compactMap(file)
        return Dictionary(grouping: files, by: \.boot).map { boot, fs in
            let fs = fs.sorted()
            return JournalBoot(boot: boot, files: fs, bytes: fs.reduce(0) { $0 + (sizes[$1.name] ?? 0) },
                               current: fs.contains { $0.name == current })
        }
        .sorted { $0.boot > $1.boot }
    }

    static func group(_ m: NSTextCheckingResult, _ i: Int, in s: String) -> Substring? {
        guard i < m.numberOfRanges, let r = Range(m.range(at: i), in: s) else { return nil }
        return s[r]
    }
}
