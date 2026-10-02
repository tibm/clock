import Foundation

/// One line of a journal file (PROTOCOL.md "Debug journal → Lines").
nonisolated struct JournalLine: Sendable, Identifiable, Equatable {
    enum Kind: Sendable, Equatable {
        /// Matches `journal.line_regex`.
        case log
        /// Anything else: the IDF boot banner, a panic backtrace.
        case plain
        /// Starts with `journal.header_prefix`.
        case header
        /// Starts with `journal.rescued_prefix`: the "before the reset" section follows.
        case rescued
    }

    /// Position in the boot (parts concatenated).
    var id: Int
    let kind: Kind
    /// The whole line.
    let raw: String
    var level: Character?
    var ms: Int?
    var tag: String?
    /// The message for a `.log` line, else the whole line.
    var text: String
    /// Inside the section the clock rescued from RAM after a reset.
    var beforeReset = false
}

nonisolated extension JournalFormat {
    /// Splits a file (or a boot's parts, concatenated) into lines. UTF-8, lossy.
    func lines(_ data: Data, firstID: Int = 0) -> [JournalLine] {
        var rows = data.split(separator: 0x0A, omittingEmptySubsequences: false)
        if rows.last?.isEmpty == true { rows.removeLast() }
        var out: [JournalLine] = []
        out.reserveCapacity(rows.count)
        var before = false
        for (i, row) in rows.enumerated() {
            var s = String(decoding: row, as: UTF8.self)
            if s.hasSuffix("\r") { s.removeLast() }
            let id = firstID + i
            if s.hasPrefix(spec.headerPrefix) {
                before = false
                out.append(JournalLine(id: id, kind: .header, raw: s, text: s))
            } else if s.hasPrefix(spec.rescuedPrefix) {
                before = true
                out.append(JournalLine(id: id, kind: .rescued, raw: s, text: s, beforeReset: true))
            } else if let m = lineRE.firstMatch(in: s, range: NSRange(s.startIndex..., in: s)),
                      let level = Self.group(m, 1, in: s)?.first,
                      let ms = Self.group(m, 2, in: s).flatMap({ Int($0) }),
                      let tag = Self.group(m, 3, in: s), let text = Self.group(m, 4, in: s) {
                out.append(JournalLine(id: id, kind: .log, raw: s, level: level, ms: ms, tag: String(tag),
                                       text: String(text), beforeReset: before))
            } else {
                out.append(JournalLine(id: id, kind: .plain, raw: s, text: s, beforeReset: before))
            }
        }
        return out
    }

    /// The reason after `reset: ` in a header line: the longest of `journal.reset_reasons` it
    /// starts with, else the text up to the next double space.
    func resetReason(_ header: String) -> String? {
        guard header.hasPrefix(spec.headerPrefix), let r = header.range(of: "reset: ") else { return nil }
        let rest = header[r.upperBound...]
        if let known = (spec.resetReasons ?? []).filter({ rest.hasPrefix($0) }).max(by: { $0.count < $1.count }) {
            return known
        }
        let word = String(rest).components(separatedBy: "  ").first ?? ""
        return word.isEmpty ? nil : word
    }

    /// A file's boot-list facts: the reset reason from its header, and whether it holds rescued lines.
    func summary(_ data: Data) -> (reset: String?, rescued: Bool) {
        let first = data.prefix(1024).split(separator: 0x0A, maxSplits: 1).first.map { String(decoding: $0, as: UTF8.self) }
        return (first.flatMap(resetReason), data.range(of: Data(spec.rescuedPrefix.utf8)) != nil)
    }

    /// `+h:mm:ss.mmm` since boot.
    static func elapsed(ms: Int) -> String {
        String(format: "+%d:%02d:%02d.%03d", ms / 3_600_000, ms / 60_000 % 60, ms / 1000 % 60, ms % 1000)
    }

    /// The viewer's filter. Headers and the rescued marker always stay; plain lines (backtraces)
    /// stay unless a tag is picked; the level filter applies to `.log` lines.
    static func filter(_ lines: [JournalLine], levels: Set<Character>, tag: String?, search: String) -> [JournalLine] {
        let q = search.trimmingCharacters(in: .whitespaces)
        return lines.filter { l in
            switch l.kind {
            case .header, .rescued: return true
            case .log:
                guard let lv = l.level, levels.contains(lv) else { return false }
                if let tag, l.tag != tag { return false }
            case .plain:
                if tag != nil { return false }
            }
            return q.isEmpty || l.raw.localizedCaseInsensitiveContains(q)
        }
    }
}
