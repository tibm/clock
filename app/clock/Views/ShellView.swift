import SwiftUI

/// A terminal onto the clock's CLI over BLE, with autocomplete from the command catalog.
struct ShellView: View {
    @Environment(ClockLink.self) private var link
    @State private var input = ""
    @State private var history = ShellHistory()
    @FocusState private var focused: Bool

    var body: some View {
        @Bindable var link = link
        NavigationStack {
            VStack(spacing: 0) {
                transcript
                Divider()
                completionBar
                inputRow
            }
            .navigationTitle("Shell")
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem { LinkStatusLabel() }
                ToolbarItem {
                    Menu("More", systemImage: "ellipsis.circle") {
                        Toggle("Show raw frames", isOn: $link.showRawFrames)
                        Button("Copy transcript", systemImage: "doc.on.doc") {
                            Pasteboard.copy(link.transcript.map { Self.plain($0) }.joined(separator: "\n"))
                        }
                        Button("Clear", systemImage: "trash", role: .destructive) { link.clearTranscript() }
                    }
                }
            }
        }
    }

    // MARK: Transcript

    private var transcript: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    ForEach(link.transcript) { line in
                        Text(Self.attributed(line))
                            .font(.system(.caption, design: .monospaced))
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .textSelection(.enabled)
                            .id(line.id)
                    }
                }
                .padding(8)
            }
            .defaultScrollAnchor(.bottom)
            // The keyboard covers the tab bar: dragging or tapping the transcript puts it away.
            .scrollDismissesKeyboard(.interactively)
            .onTapGesture { focused = false }
            .onChange(of: link.transcript.last?.id) { _, id in
                if let id { withAnimation(.linear(duration: 0.1)) { proxy.scrollTo(id, anchor: .bottom) } }
            }
            .overlay {
                if link.transcript.isEmpty {
                    ContentUnavailableView("Type `help`", systemImage: "terminal",
                                           description: Text("Commands go to the clock exactly as on its USB console."))
                }
            }
        }
    }

    private static func attributed(_ line: ShellLine) -> AttributedString {
        var s = AttributedString(prefix(line) + line.text)
        switch line.kind {
        case .sent: s.foregroundColor = .accentColor
        case .text: break
        case .pair: s.foregroundColor = .secondary
        case .status(let o): s.foregroundColor = o.color
        case .note: s.foregroundColor = .gray
        case .raw: s.foregroundColor = .purple
        }
        return s
    }

    private static func prefix(_ line: ShellLine) -> String {
        switch line.kind {
        case .sent: "> "
        case .text: "  "
        case .pair: "  = "
        case .status: "  $"
        case .note: "# "
        case .raw: "~ "
        }
    }

    static func plain(_ line: ShellLine) -> String { prefix(line) + line.text }

    // MARK: Input

    private var completions: [Completion] { link.catalog.completions(for: input) }

    private var completionBar: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                ForEach(completions) { c in
                    if let insert = c.insert {
                        Button(c.label) { input = insert; focused = true }
                            .buttonStyle(.bordered)
                            .font(.caption.monospaced())
                            .help(c.detail ?? "")
                    } else {
                        Text(c.label)
                            .font(.caption.monospaced())
                            .foregroundStyle(.secondary)
                            .padding(.horizontal, 6)
                    }
                }
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
        }
        .frame(minHeight: 34)
    }

    private var inputRow: some View {
        HStack(spacing: 6) {
            Button { recall(-1) } label: { Image(systemName: "chevron.up") }
                .disabled(history.isEmpty)
            Button { recall(1) } label: { Image(systemName: "chevron.down") }
                .disabled(history.isEmpty)
            TextField("command", text: $input)
                .font(.body.monospaced())
                .textFieldStyle(.roundedBorder)
                .cliInput()
                .focused($focused)
                .submitLabel(.send)
                .onSubmit(submit)
                .onKeyPress(.tab) { completeCommonPrefix(); return .handled }
                .onKeyPress(.upArrow) { recall(-1); return .handled }
                .onKeyPress(.downArrow) { recall(1); return .handled }
            Button("Send", systemImage: "paperplane.fill", action: submit)
                .labelStyle(.iconOnly)
                .disabled(input.trimmingCharacters(in: .whitespaces).isEmpty)
            #if os(iOS)
            if focused {
                Button("Hide keyboard", systemImage: "keyboard.chevron.compact.down") { focused = false }
                    .labelStyle(.iconOnly)
            }
            #endif
        }
        .padding(8)
    }

    private func submit() {
        let line = input.trimmingCharacters(in: .whitespaces)
        guard !line.isEmpty else { return }
        history.add(line)
        input = ""
        focused = true
        Task { await link.send(line) }
    }

    private func recall(_ step: Int) {
        if let line = history.step(step, current: input) { input = line }
    }

    /// Tab: take the only completion, or extend to the longest common prefix.
    private func completeCommonPrefix() {
        let inserts = completions.compactMap(\.insert)
        guard let first = inserts.first else { return }
        if inserts.count == 1 { input = first; return }
        var common = first
        for s in inserts.dropFirst() { common = String(common.commonPrefix(with: s)) }
        if common.count > input.count { input = common }
    }
}

/// Shell history, newest last, persisted (last 100).
struct ShellHistory {
    private static let key = "shellHistory"
    private static let limit = 100
    private(set) var lines: [String] = UserDefaults.standard.stringArray(forKey: key) ?? []
    private var cursor: Int?
    private var draft = ""

    var isEmpty: Bool { lines.isEmpty }

    mutating func add(_ line: String) {
        if lines.last != line { lines.append(line) }
        if lines.count > Self.limit { lines.removeFirst(lines.count - Self.limit) }
        UserDefaults.standard.set(lines, forKey: Self.key)
        cursor = nil
    }

    /// -1 = older, +1 = newer. Returns the line to show.
    mutating func step(_ dir: Int, current: String) -> String? {
        guard !lines.isEmpty else { return nil }
        if cursor == nil {
            guard dir < 0 else { return nil }
            draft = current
            cursor = lines.count
        }
        let next = (cursor ?? lines.count) + dir
        if next >= lines.count { cursor = nil; return draft }
        cursor = max(0, next)
        return lines[cursor!]
    }
}

#Preview {
    ShellView().environment(ClockLink.preview())
}
