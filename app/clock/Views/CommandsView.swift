import SwiftUI

/// Every known command as a table: JSON commands plus what the firmware's `help` reported.
/// Tap one to fill its arguments and send it.
struct CommandsView: View {
    @Environment(ClockLink.self) private var link
    @State private var filter = ""
    @State private var selected: CommandEntry?
    @State private var quickResult: CommandResult?

    var body: some View {
        NavigationStack {
            List {
                if filter.isEmpty { quickActions }
                ForEach(groups, id: \.group) { group in
                    Section(group.group) {
                        ForEach(group.entries) { entry in
                            Button { selected = entry } label: { CommandRow(entry: entry) }
                                .buttonStyle(.plain)
                        }
                    }
                }
            }
            .searchable(text: $filter, prompt: "Filter commands")
            .navigationTitle("Commands")
            .toolbar {
                ToolbarItem { LinkStatusLabel() }
                ToolbarItem {
                    Button("Reload from clock", systemImage: "arrow.triangle.2.circlepath") {
                        Task { await link.refreshCatalog() }
                    }
                    .disabled(link.phase != .ready)
                }
            }
            .sheet(item: $selected) { entry in
                CommandSheet(entry: entry)
                    .presentationDetents([.medium, .large])
            }
        }
    }

    private var groups: [(group: String, entries: [CommandEntry])] {
        let q = filter.lowercased()
        guard !q.isEmpty else { return link.catalog.grouped }
        return link.catalog.grouped.compactMap { g in
            let e = g.entries.filter { $0.grammar.lowercased().contains(q) || $0.help.lowercased().contains(q) }
            return e.isEmpty ? nil : (g.group, e)
        }
    }

    private var quickActions: some View {
        Section {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack {
                    quick("Sync time", "clock.arrow.2.circlepath") { await link.syncTime(echo: true) }
                    quick("sys ver", "info.circle") { await link.send("sys ver") }
                    quick("Tone", "speaker.wave.2") { await link.send("audio tone") }
                    quick("Stop", "speaker.slash") { await link.send("audio stop") }
                    quick("unsafe on", "lock.open") { await link.send("unsafe on") }
                    quick("Close pairing", "lock") { await link.send("net ble pair off") }
                }
                .padding(.vertical, 2)
            }
            if let r = quickResult {
                ResultView(result: r)
            }
        } header: {
            Text("Quick actions")
        }
    }

    private func quick(_ title: String, _ icon: String, _ action: @escaping () async -> CommandResult) -> some View {
        Button {
            Task { quickResult = await action() }
        } label: {
            Label(title, systemImage: icon).font(.callout)
        }
        .buttonStyle(.bordered)
        .disabled(link.phase != .ready)
    }
}

private struct CommandRow: View {
    let entry: CommandEntry

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(alignment: .firstTextBaseline) {
                Text(entry.grammar).font(.callout.monospaced())
                Spacer(minLength: 4)
                if entry.unsafe { Badge(text: "unsafe", color: .red) }
                if entry.isPlanned { Badge(text: "planned", color: .purple) }
                if entry.sources == [.device] { Badge(text: "device", color: .blue) }
            }
            if !entry.help.isEmpty {
                Text(entry.help).font(.caption).foregroundStyle(.secondary)
            }
            let ranges = entry.args.sorted { $0.key < $1.key }.map { "\($0.key): \($0.value.text)" }
            if !ranges.isEmpty {
                Text(ranges.joined(separator: " · ")).font(.caption2.monospaced()).foregroundStyle(.tertiary)
            }
        }
        .contentShape(Rectangle())
    }
}

/// Fill a command's arguments, send it, show the answer.
struct CommandSheet: View {
    @Environment(ClockLink.self) private var link
    @Environment(\.dismiss) private var dismiss
    let entry: CommandEntry

    @State private var values: [Int: String] = [:]
    @State private var result: CommandResult?
    @State private var sending = false
    @State private var confirmUnsafe = false

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Text(entry.grammar).font(.body.monospaced())
                    if !entry.help.isEmpty { Text(entry.help).font(.callout).foregroundStyle(.secondary) }
                }
                let args = argTokens
                if !args.isEmpty {
                    Section("Arguments") {
                        ForEach(args, id: \.index) { a in argField(a) }
                    }
                }
                Section("Will send") {
                    Text(line ?? "(fill the required arguments)")
                        .font(.body.monospaced())
                        .foregroundStyle(line == nil ? .secondary : .primary)
                }
                if entry.isPlanned {
                    Text("Planned: this firmware may not have it yet (answers bad-arg).")
                        .font(.caption).foregroundStyle(.purple)
                }
                if let result { Section("Answer") { ResultView(result: result) } }
            }
            .navigationTitle(entry.group)
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Close") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Send") { entry.unsafe ? (confirmUnsafe = true) : send(unlockFirst: false) }
                        .disabled(line == nil || sending || link.phase != .ready)
                }
            }
            .confirmationDialog("Hardware-moving command", isPresented: $confirmUnsafe) {
                Button("Send `unsafe on` first, then this") { send(unlockFirst: true) }
                Button("Send as is") { send(unlockFirst: false) }
            } message: {
                Text("\(entry.grammar) needs `unsafe on` within the last 60 s.")
            }
        }
    }

    private struct ArgToken { let index: Int; let name: String; let optional: Bool; let choices: [String] }

    private var argTokens: [ArgToken] {
        entry.tokens.enumerated().compactMap { i, t in
            if case .arg(let name, let optional, let choices) = t {
                return ArgToken(index: i, name: name, optional: optional, choices: choices)
            }
            return nil
        }
    }

    @ViewBuilder private func argField(_ a: ArgToken) -> some View {
        let binding = Binding(get: { values[a.index] ?? "" }, set: { values[a.index] = $0 })
        if !a.choices.isEmpty {
            Picker(a.optional ? "\(a.name) (optional)" : a.name, selection: binding) {
                if a.optional { Text("—").tag("") }
                ForEach(a.choices, id: \.self) { Text($0).tag($0) }
            }
        } else {
            VStack(alignment: .leading, spacing: 2) {
                TextField(a.optional ? "\(a.name) (optional)" : a.name, text: binding)
                    .font(.body.monospaced())
                    .cliInput()
                if let c = entry.constraint(for: a.name) {
                    Text(c).font(.caption2).foregroundStyle(.secondary)
                }
                if let err = rangeError(a, binding.wrappedValue) {
                    Text(err).font(.caption2).foregroundStyle(.orange)
                }
            }
        }
    }

    /// Out-of-range numbers are flagged but still sendable: the firmware has the last word.
    private func rangeError(_ a: ArgToken, _ v: String) -> String? {
        guard case .range(let lo, let hi)? = entry.args[a.name], let n = Double(v) else { return nil }
        return (lo...hi).contains(n) ? nil : "outside \(entry.args[a.name]!.text)"
    }

    /// The line to send, or nil while a required argument is empty.
    private var line: String? {
        var parts: [String] = []
        for (i, t) in entry.tokens.enumerated() {
            switch t {
            case .word(let w): parts.append(w)
            case .arg(_, let optional, _):
                let v = (values[i] ?? "").trimmingCharacters(in: .whitespaces)
                if v.isEmpty { if optional { continue } else { return nil } }
                parts.append(v.contains(" ") ? "\"\(v)\"" : v)
            }
        }
        return parts.joined(separator: " ")
    }

    private func send(unlockFirst: Bool) {
        guard let line else { return }
        sending = true
        Task {
            if unlockFirst { await link.send("unsafe on") }
            result = await link.send(line)
            sending = false
        }
    }
}

struct ResultView: View {
    let result: CommandResult

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(result.line).font(.caption.monospaced()).foregroundStyle(.secondary)
                Spacer()
                Badge(text: result.outcome.label, color: result.outcome.color)
            }
            if !result.lines.isEmpty {
                Text(result.text).font(.caption.monospaced()).textSelection(.enabled)
            }
            ForEach(result.pairs, id: \.key) { p in
                Text("\(p.key) = \(p.value)").font(.caption.monospaced()).foregroundStyle(.secondary)
            }
        }
    }
}

#Preview {
    CommandsView().environment(ClockLink.preview())
}
