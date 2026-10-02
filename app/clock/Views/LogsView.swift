import SwiftUI

/// The clock's debug journal, mirrored on the phone (PROTOCOL.md "Debug journal"): one entry per
/// boot, and a viewer for its lines.
struct LogsView: View {
    @Environment(ClockLink.self) private var link
    @Environment(JournalStore.self) private var store

    var body: some View {
        @Bindable var store = store
        NavigationStack {
            Group {
                if store.format == nil {
                    ContentUnavailableView("No logs", systemImage: "doc.text.magnifyingglass",
                                           description: Text("This app's protocol.json has no `journal` block."))
                } else {
                    content
                }
            }
            .navigationTitle("Logs")
            .navigationDestination(for: Int.self) { LogViewer(bootNumber: $0) }
            .toolbar {
                ToolbarItem { LinkStatusLabel() }
                ToolbarItem {
                    Toggle("Follow", systemImage: "dot.radiowaves.forward", isOn: $store.follow)
                        .disabled(!canSync)
                }
                ToolbarItem {
                    Button("Refresh", systemImage: "arrow.clockwise") { Task { await store.refresh() } }
                        .disabled(!canSync || store.syncing)
                }
            }
            .task(id: "\(store.archiveVersion)/\(store.clockID?.uuidString ?? "")/\(link.phase == .ready)") {
                await store.loadBoots()
            }
        }
        // On the stack, not its root: keeps following while a boot is open; cancelled with the tab.
        .task(id: "\(store.follow)/\(link.phase == .ready)") {
            await store.refresh()
            while store.follow && !Task.isCancelled {
                try? await Task.sleep(for: JournalStore.followInterval)
                if Task.isCancelled { break }
                await store.refresh(quiet: true)
            }
        }
    }

    private var canSync: Bool { link.phase == .ready && store.supported && !store.noCard }

    private var content: some View {
        List {
            syncSection
            Section("Boots") {
                if store.boots.isEmpty {
                    Text(link.phase == .ready ? "Nothing downloaded yet." : "Connect to download the clock's logs.")
                        .foregroundStyle(.secondary)
                }
                ForEach(store.boots) { b in
                    NavigationLink(value: b.boot) { BootRow(boot: b) }
                }
            }
        }
    }

    private var syncSection: some View {
        Section {
            if let p = store.progress, p.total > 32 * 1024 {
                ProgressView(value: p.fraction) {
                    Text("Fetching \(p.file ?? "…") · \(bytes(p.done)) / \(bytes(p.total))")
                } currentValueLabel: {
                    if p.rate > 0 { Text("\(bytes(Int(p.rate)))/s · \(p.filesLeft) file(s) left") }
                }
                Button("Cancel", systemImage: "xmark.circle", role: .destructive) { store.cancelSync() }
            }
            if let m = store.message {
                Label(m.text, systemImage: m.ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                    .foregroundStyle(m.ok ? .green : .orange)
            }
            if store.noCard {
                Label("The clock has no microSD card — nothing is being logged to it.", systemImage: "sdcard")
                    .foregroundStyle(.orange)
            } else if link.phase == .ready && !store.supported {
                Text("This clock's firmware has no `bulk` characteristic (see README → Troubleshooting: usually a stale GATT cache).")
                    .foregroundStyle(.secondary)
            }
            if !store.status.isEmpty {
                LabeledContent("Boot", value: store.statusValue("boot") ?? "—")
                LabeledContent("Card", value: store.statusValue("card") ?? "—")
                if let lost = store.statusValue("lost") {
                    LabeledContent("Lines lost (ring full)", value: lost)
                        .foregroundStyle(lost == "0" ? Color.primary : .orange)
                }
            }
        } footer: {
            Text("Every log line, one file per boot. The phone keeps every file it has downloaded, also after the clock deletes it.")
        }
    }
}

/// Reset reasons worth a red badge: the boot before it crashed or hung.
private let badResets: Set<String> = ["panic", "task wdt", "int wdt", "wdt", "brown-out"]

private struct BootRow: View {
    let boot: JournalBoot

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("Boot \(boot.boot)").font(.headline)
                if boot.current { Badge(text: "current", color: .green) }
                if boot.rescued { Badge(text: "rescued lines", color: .purple) }
                Spacer()
                Text(bytes(boot.bytes)).font(.caption.monospacedDigit()).foregroundStyle(.secondary)
            }
            HStack {
                if let r = boot.reset {
                    Badge(text: "reset: \(r)", color: badResets.contains(r) ? .red : .secondary)
                }
                if let e = boot.endedBy, badResets.contains(e) { Badge(text: "ended: \(e)", color: .red) }
                if boot.files.count > 1 {
                    Text("\(boot.files.count) parts").font(.caption).foregroundStyle(.secondary)
                }
            }
        }
    }
}

// MARK: - Viewer

private struct LogViewer: View {
    @Environment(JournalStore.self) private var store
    let bootNumber: Int

    @State private var lines: [JournalLine] = []
    @State private var shown: [JournalLine] = []
    @State private var matched = 0
    @State private var shownVersion = 0
    @State private var loading = true
    @State private var levels: Set<Character> = ["E", "W", "I", "D", "V"]
    @State private var tag: String?
    @State private var search = ""
    @State private var loadAll = false
    @State private var jumpTo: Int?
    @State private var scrolled = false

    private static let levelNames: [(Character, String)] = [("E", "Error"), ("W", "Warn"), ("I", "Info"), ("D", "Debug"), ("V", "Verbose")]

    private var boot: JournalBoot? { store.boots.first { $0.boot == bootNumber } }
    private var tags: [String] { Array(Set(lines.compactMap(\.tag))).sorted() }
    private var marker: JournalLine? { lines.first { $0.kind == .rescued } }

    var body: some View {
        ScrollViewReader { proxy in
            // ScrollView, not List: tens of thousands of rows, and the macOS List trips on the diffs.
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    if matched > shown.count {
                        Button("Load all (\(matched - shown.count) earlier lines)") { loadAll = true }
                            .padding(.vertical, 6)
                    }
                    ForEach(shown) { LineRow(line: $0) }
                }
                .padding(.horizontal, 8)
            }
            .overlay {
                if loading && lines.isEmpty { ProgressView() }
                else if !loading && lines.isEmpty { ContentUnavailableView("Empty", systemImage: "doc.text") }
            }
            .onChange(of: shownVersion) {
                if let id = jumpTo, shown.contains(where: { $0.id == id }) {
                    proxy.scrollTo(id, anchor: .top)
                    jumpTo = nil
                } else if jumpTo == nil, !scrolled || store.follow, let last = shown.last {
                    proxy.scrollTo(last.id, anchor: .bottom)
                    scrolled = true
                }
            }
        }
        .safeAreaInset(edge: .top) { filterBar }
        .searchable(text: $search, prompt: "Search lines")
        .navigationTitle("Boot \(bootNumber)")
        .toolbar {
            if marker != nil {
                ToolbarItem {
                    Button("Jump to before the reset", systemImage: "arrow.uturn.down") { jump() }
                }
            }
            ToolbarItem {
                Menu("Log levels", systemImage: "ladybug") {
                    ForEach(tags, id: \.self) { t in
                        Button("\(t) → debug") { Task { await store.setLevel("sys debug \(t) debug") } }
                    }
                    Divider()
                    Button("Everything back to info") { Task { await store.setLevel("sys debug all info") } }
                }
            }
            if let boot {
                ToolbarItem { ShareLink(items: store.urls(boot)) { Label("Share", systemImage: "square.and.arrow.up") } }
            }
        }
        .task(id: boot.map { "\($0.bytes)/\($0.files.count)" }) {
            guard let boot else { return }
            lines = await store.lines(for: boot)
            loading = false
            await refilter()
        }
        .task(id: "\(levels.sorted())/\(tag ?? "")/\(search)/\(loadAll)") { await refilter() }
    }

    private var filterBar: some View {
        HStack(spacing: 6) {
            ForEach(Self.levelNames, id: \.0) { lv, name in
                let on = levels.contains(lv)
                Button {
                    if on { levels.remove(lv) } else { levels.insert(lv) }
                } label: {
                    Text(String(lv)).bold().strikethrough(!on)
                        .frame(minWidth: 24)
                        .padding(.vertical, 4)
                        .foregroundStyle(on ? LineRow.color(lv) : .secondary)
                        .background((on ? Color.accentColor.opacity(0.18) : .clear), in: Capsule())
                        .overlay(Capsule().stroke(.secondary.opacity(on ? 0 : 0.4)))
                }
                .buttonStyle(.plain)
                .accessibilityLabel("\(name) \(on ? "shown" : "hidden")")
            }
            Spacer()
            Menu(tag ?? "All tags") {
                Button("All tags") { tag = nil }
                ForEach(tags, id: \.self) { t in Button(t) { tag = t } }
            }
            .fixedSize()
        }
        .font(.caption.monospaced())
        .padding(.horizontal)
        .padding(.vertical, 6)
        .background(.bar)
    }

    private func refilter() async {
        let (all, l, t, s) = (lines, levels, tag, search)
        let filtered = await Self.filtered(all, l, t, s)
        matched = filtered.count
        shown = loadAll ? filtered : Array(filtered.suffix(JournalStore.tailLines))
        shownVersion += 1
    }

    @concurrent
    nonisolated private static func filtered(_ lines: [JournalLine], _ levels: Set<Character>, _ tag: String?,
                                             _ search: String) async -> [JournalLine] {
        JournalFormat.filter(lines, levels: levels, tag: tag, search: search)
    }

    /// Scrolls to the rescued marker (always kept by the filter); loads all lines if it is older
    /// than the rendered tail.
    private func jump() {
        guard let m = marker else { return }
        jumpTo = m.id
        if shown.contains(where: { $0.id == m.id }) { shownVersion += 1 } else { loadAll = true }
    }
}

private struct LineRow: View {
    let line: JournalLine

    static func color(_ level: Character?) -> Color {
        switch level {
        case "E": .red
        case "W": .orange
        case "D", "V": .secondary
        default: .primary
        }
    }

    var body: some View {
        Group {
            switch line.kind {
            case .log:
                let time = Text(JournalFormat.elapsed(ms: line.ms ?? 0)).foregroundStyle(.secondary)
                let head = Text("\(String(line.level ?? " ")) \(line.tag ?? ""):").bold()
                Text("\(time) \(head) \(line.text)").foregroundStyle(Self.color(line.level))
            case .plain:
                Text(line.text).foregroundStyle(.secondary)
            case .header:
                Text(line.text).bold().foregroundStyle(.blue)
            case .rescued:
                Text(line.text).bold().foregroundStyle(.purple)
            }
        }
        .font(.caption.monospaced())
        .textSelection(.enabled)
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.leading, line.beforeReset ? 4 : 0)
        .background(line.beforeReset ? Color.purple.opacity(0.08) : .clear)
    }
}

private func bytes(_ n: some BinaryInteger) -> String {
    Int64(n).formatted(.byteCount(style: .file))
}

#Preview {
    let store = JournalStore.preview()
    LogsView().environment(store.link).environment(store)
}

#Preview("Viewer") {
    let store = JournalStore.preview()
    NavigationStack { LogViewer(bootNumber: 123) }.environment(store.link).environment(store)
}
