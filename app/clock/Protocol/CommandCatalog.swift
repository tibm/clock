import Foundation

/// One command the clock accepts, from `protocol.json`, from the firmware's own `help`, or both.
nonisolated struct CommandEntry: Identifiable, Hashable, Sendable {
    enum Token: Hashable, Sendable {
        case word(String)
        /// `<name>`, `[<name>]`, `<on|off>`, `[--hex]`…
        case arg(name: String, optional: Bool, choices: [String])
    }

    enum Source: String, Hashable, Sendable { case spec, device }

    /// Grammar as written, e.g. `audio tone [<hz>] [<ms>]`.
    let grammar: String
    let tokens: [Token]
    var help: String
    /// `implemented`, `planned` (from the JSON), or `device` (only the firmware's `help` knows it).
    var status: String
    var unsafe: Bool
    var args: [String: ProtocolSpec.ArgSpec]
    var sources: Set<Source>

    var id: String { grammar }
    /// First word, used to group the table.
    var group: String { if case .word(let w)? = tokens.first { w } else { "?" } }
    var isPlanned: Bool { status == "planned" }
    var hasArgs: Bool { tokens.contains { if case .arg = $0 { true } else { false } } }

    /// Grammar with every placeholder collapsed, so `audio vol <pct>` and `audio vol <0-100>`
    /// are recognised as the same command.
    var shapeKey: String {
        tokens.map { if case .word(let w) = $0 { w } else { "<>" } }.joined(separator: " ")
    }

    init(grammar: String, help: String, status: String, unsafe: Bool,
         args: [String: ProtocolSpec.ArgSpec] = [:], source: Source) {
        self.grammar = grammar
        self.tokens = Self.tokenize(grammar)
        self.help = help
        self.status = status
        self.unsafe = unsafe
        self.args = args
        self.sources = [source]
    }

    /// The constraint text for one argument, if the spec gives one.
    func constraint(for name: String) -> String? { args[name]?.text }

    static func tokenize(_ grammar: String) -> [Token] {
        grammar.split(separator: " ").map { raw in
            let t = String(raw)
            let isArg = t.contains("<") || t.contains("[") || t.contains("|")
            guard isArg else { return .word(t) }
            let optional = t.hasPrefix("[") || t.hasSuffix("]")
            // Strip the outer [ ] and < > only; keep inner brackets like `hh:mm[:ss]`.
            var inner = Substring(t)
            if inner.hasPrefix("[") { inner = inner.dropFirst() }
            if inner.hasSuffix("]"), !inner.contains("<") || inner.hasSuffix(">]") { inner = inner.dropLast() }
            let placeholder = inner.hasPrefix("<") && inner.hasSuffix(">")
            if placeholder { inner = inner.dropFirst().dropLast() }
            let parts = inner.split(separator: "|").map(String.init)
            // `<on|off>` / `[--hex]`: literal choices. `<pct>` / `<hh:mm[:ss]>`: a free value.
            if parts.count > 1 || !placeholder {
                return .arg(name: parts.joined(separator: "|"), optional: optional, choices: parts)
            }
            return .arg(name: String(inner), optional: optional, choices: [])
        }
    }
}

/// A suggestion for the shell. `insert == nil` means a hint only (e.g. `<pct 0–100>`).
nonisolated struct Completion: Hashable, Sendable, Identifiable {
    let label: String
    let insert: String?
    let detail: String?
    var id: String { label + (insert ?? "") }
}

/// Every command the app knows, for the command table and the shell's autocomplete.
///
/// Seeded from `protocol.json` (stable, documented); `merge(helpRows:)` adds what the connected
/// firmware reports via `help <group>`. That output is human text, so parsing is best-effort:
/// rows that don't parse are dropped and the JSON entries always stay.
nonisolated struct CommandCatalog: Sendable {
    private(set) var entries: [CommandEntry]

    init(spec: ProtocolSpec) {
        entries = spec.commands.map {
            CommandEntry(grammar: $0.line, help: $0.use ?? "", status: $0.status,
                         unsafe: $0.unsafe ?? false, args: $0.args ?? [:], source: .spec)
        }
    }

    /// Entries grouped by first word, groups and entries sorted.
    var grouped: [(group: String, entries: [CommandEntry])] {
        Dictionary(grouping: entries, by: \.group)
            .map { ($0.key, $0.value.sorted { $0.grammar < $1.grammar }) }
            .sorted { $0.0 < $1.0 }
    }

    // MARK: Merging the firmware's `help`

    /// Groups from the first line of plain `help`: `groups  sys  help  unsafe  …`.
    static func parseGroups(_ lines: [String]) -> [String] {
        guard let line = lines.first(where: { $0.hasPrefix("groups") }) else { return [] }
        return line.split(separator: " ").dropFirst().map(String.init)
    }

    /// One `help <group>` row: `group [object ]verb args` padded to a column, the help text,
    /// optionally `   [unsafe]`. Returns nil for anything else.
    static func parseHelpRow(_ row: String, group: String) -> (grammar: String, help: String, unsafe: Bool)? {
        var text = row.trimmingCharacters(in: .whitespaces)
        guard text.hasPrefix(group + " ") || text == group else { return nil }
        var unsafe = false
        if text.hasSuffix("[unsafe]") {
            unsafe = true
            text = String(text.dropLast("[unsafe]".count)).trimmingCharacters(in: .whitespaces)
        }
        // Normal case: two or more spaces separate the grammar from the help text.
        if let gap = text.range(of: #"\s{2,}"#, options: .regularExpression) {
            return (String(text[..<gap.lowerBound]), String(text[gap.upperBound...]), unsafe)
        }
        // Long grammar leaves a single space: end the grammar at the last placeholder token.
        let tokens = text.split(separator: " ").map(String.init)
        if let last = tokens.lastIndex(where: { $0.contains("<") || $0.contains("[") }) {
            return (tokens[...last].joined(separator: " "), tokens[(last + 1)...].joined(separator: " "), unsafe)
        }
        return (tokens.prefix(2).joined(separator: " "), tokens.dropFirst(2).joined(separator: " "), unsafe)
    }

    /// Adds the rows of `help <group>`; returns how many were new.
    @discardableResult
    mutating func merge(helpRows: [String], group: String) -> Int {
        var added = 0
        for row in helpRows {
            guard let r = Self.parseHelpRow(row, group: group) else { continue }
            let entry = CommandEntry(grammar: r.grammar, help: r.help, status: "device",
                                     unsafe: r.unsafe, source: .device)
            if let i = entries.firstIndex(where: { $0.shapeKey == entry.shapeKey }) {
                entries[i].sources.insert(.device)
                if entries[i].help.isEmpty { entries[i].help = r.help }
                entries[i].unsafe = entries[i].unsafe || r.unsafe
            } else {
                entries.append(entry)
                added += 1
            }
        }
        return added
    }

    // MARK: Completion

    /// Suggestions for the next token of `input` (prefix match, one token at a time).
    func completions(for input: String, limit: Int = 16) -> [Completion] {
        let parts = input.split(separator: " ", omittingEmptySubsequences: true).map(String.init)
        let endsWithSpace = input.isEmpty || input.last == " "
        let done = endsWithSpace ? parts : Array(parts.dropLast())
        let partial = endsWithSpace ? "" : (parts.last ?? "")
        let prefix = done.isEmpty ? "" : done.joined(separator: " ") + " "

        var seen = Set<String>()
        var words: [Completion] = []
        var hints: [Completion] = []
        for e in entries where done.count < e.tokens.count && Self.matches(done, e.tokens) {
            let isLast = done.count == e.tokens.count - 1
            switch e.tokens[done.count] {
            case .word(let w) where w.lowercased().hasPrefix(partial.lowercased()):
                guard seen.insert("w:" + w).inserted else { continue }
                words.append(Completion(label: w, insert: prefix + w + " ", detail: isLast ? e.help : nil))
            case .arg(_, _, let choices) where !choices.isEmpty:
                for c in choices where c.hasPrefix(partial) && seen.insert("w:" + c).inserted {
                    words.append(Completion(label: c, insert: prefix + c + " ", detail: nil))
                }
            case .arg(let name, let optional, _):
                let range = e.constraint(for: name).map { " " + $0 } ?? ""
                let label = optional ? "[<\(name)\(range)>]" : "<\(name)\(range)>"
                if seen.insert("h:" + label).inserted {
                    hints.append(Completion(label: label, insert: nil, detail: e.help))
                }
            default:
                continue
            }
        }
        return Array((words.sorted { $0.label < $1.label } + hints).prefix(limit))
    }

    /// Entries whose grammar is consistent with what has been typed so far (for filtering).
    func matching(_ input: String) -> [CommandEntry] {
        let parts = input.split(separator: " ").map(String.init)
        guard !parts.isEmpty else { return entries }
        return entries.filter { e in
            guard parts.count <= e.tokens.count else { return false }
            let done = Array(parts.dropLast())
            guard Self.matches(done, e.tokens) else { return false }
            if case .word(let w) = e.tokens[done.count] {
                return w.lowercased().hasPrefix(parts.last!.lowercased())
            }
            return true
        }
    }

    /// Typed tokens against the start of a grammar: words must equal, placeholders take anything.
    private static func matches(_ typed: [String], _ tokens: [CommandEntry.Token]) -> Bool {
        for (t, g) in zip(typed, tokens) {
            if case .word(let w) = g, w.lowercased() != t.lowercased() { return false }
        }
        return true
    }
}
