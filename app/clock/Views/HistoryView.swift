import SwiftUI
import Charts

/// The clock's history log, mirrored on the phone and plotted (PROTOCOL.md §4 "History").
///
/// Charts are looked up by column name; any column this layout doesn't list gets a plain chart
/// under "Other", so a new field in `protocol.json` shows up without touching this file.
struct HistoryView: View {
    @Environment(ClockLink.self) private var link
    @Environment(HistoryStore.self) private var store

    @State private var range: HistoryRange = .day
    @State private var showSettings = false
    @State private var csvURL: URL?
    @State private var exportError: String?

    fileprivate enum Scale { case linear, log, symLog }

    private static let charts: [(title: String, column: String, scale: Scale, peaks: String?)] = [
        ("Temperature", "temp", .linear, nil),
        ("Humidity", "rh", .linear, nil),
        ("Pressure", "press", .linear, nil),
        ("Gas resistance", "gas", .log, nil),
        ("Light", "lux", .symLog, "lux_max"),
        ("Battery", "vbat", .linear, nil),
        ("Charge", "soc", .linear, nil),
        ("Wi-Fi signal", "rssi", .linear, nil),
    ]
    /// Plotted inside another chart, or not worth a chart.
    private static let hidden: Set<String> = ["lux_max", "n"]

    var body: some View {
        NavigationStack {
            Group {
                if store.decoder == nil {
                    ContentUnavailableView("No history", systemImage: "chart.xyaxis.line",
                                           description: Text("This app's protocol.json has no `history` block."))
                } else {
                    content
                }
            }
            .navigationTitle("History")
            .toolbar {
                ToolbarItem { LinkStatusLabel() }
                ToolbarItem {
                    Button("Settings", systemImage: "slider.horizontal.3") { showSettings = true }
                        .disabled(link.phase != .ready || !store.supported)
                }
                ToolbarItem {
                    Button("Sync now", systemImage: "arrow.triangle.2.circlepath") { store.startSync() }
                        .disabled(link.phase != .ready || !store.supported || store.syncing)
                }
            }
            .sheet(isPresented: $showSettings) { HistorySettingsView() }
            .task(id: "\(range.rawValue)/\(store.archiveVersion)/\(store.clockID?.uuidString ?? "")") {
                await store.loadChart(range)
            }
        }
    }

    private var content: some View {
        List {
            syncSection

            Section {
                Picker("Range", selection: $range) {
                    ForEach(HistoryRange.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
            } footer: {
                if let chart = store.chart, chart.bucket > 0 {
                    Text("Averaged per \(Duration.seconds(chart.bucket).formatted(.units(allowed: [.days, .hours, .minutes], width: .wide))) — the band is min … max.")
                }
            }

            if let chart = store.chart, !chart.isEmpty {
                // Exactly one Section per element (a ForEach row that is sometimes empty trips
                // the macOS List).
                ForEach(Self.panels(chart)) { p in
                    Section(p.title) {
                        HistoryChart(points: p.points, peaks: p.peaks, unit: p.unit, scale: p.scale,
                                     events: chart.events, range: chart.range)
                    }
                }
                if !chart.events.isEmpty {
                    Section("Events") {
                        ForEach(chart.events.reversed()) { EventRow(event: $0) }
                    }
                }
            } else if store.loadingChart {
                ProgressView()
            } else {
                ContentUnavailableView("Nothing recorded here yet", systemImage: "chart.xyaxis.line",
                                       description: Text("Sync with the clock, or pick a longer range."))
            }

            exportSection
        }
    }

    fileprivate struct Panel: Identifiable {
        let id: String
        let title: String
        let points: [HistoryPoint]
        let peaks: [HistoryPoint]
        let unit: String?
        let scale: Scale
    }

    /// The listed charts that have data, then one per unlisted column.
    private static func panels(_ chart: HistoryChartData) -> [Panel] {
        let unit = { (name: String) in chart.columns.first { $0.name == name }?.unit }
        var out = charts.map { c in
            Panel(id: c.column, title: c.title, points: chart.points(c.column),
                  peaks: c.peaks.map(chart.points) ?? [], unit: unit(c.column), scale: c.scale)
        }
        let listed = Set(charts.map(\.column)).union(hidden)
        out += chart.columns.filter { !listed.contains($0.name) }.map { col in
            Panel(id: col.name, title: "Other: \(col.name)", points: chart.points(col.name),
                  peaks: [], unit: col.unit, scale: .linear)
        }
        return out.filter { !$0.points.isEmpty }
    }

    // MARK: Sections

    private var syncSection: some View {
        Section {
            if let p = store.progress {
                ProgressView(value: p.fraction) {
                    Text("Syncing \(p.day ?? "…") · \(bytes(p.done)) / \(bytes(p.total))")
                } currentValueLabel: {
                    if p.rate > 0 { Text("\(bytes(Int(p.rate)))/s · \(p.daysLeft) day(s) left") }
                }
                Button("Cancel sync", systemImage: "xmark.circle", role: .destructive) { store.cancelSync() }
            }
            if let m = store.message {
                Label(m.text, systemImage: m.ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                    .foregroundStyle(m.ok ? .green : .orange)
            }
            if store.noCard {
                Label("The clock has no microSD card — nothing is being recorded.", systemImage: "sdcard")
                    .foregroundStyle(.orange)
            }
            LabeledContent("Last sync", value: store.lastSync?.formatted(.relative(presentation: .named)) ?? "never")
            if link.phase == .ready && !store.supported {
                Text("This clock's firmware has no `bulk` characteristic.").foregroundStyle(.secondary)
            }
        } footer: {
            Text("The phone keeps every day it has downloaded, also after the clock deletes it. Syncs run on every connect; keep the app open during the first one.")
        }
    }

    private var exportSection: some View {
        Section("Export") {
            let files = store.rawFiles
            if !files.isEmpty {
                ShareLink(items: files) { Label("Raw day files (\(files.count))", systemImage: "doc.zipper") }
            }
            if let csvURL {
                ShareLink(item: csvURL) { Label("Share CSV", systemImage: "tablecells") }
            } else {
                Button("Make CSV", systemImage: "tablecells") {
                    Task {
                        do { csvURL = try await store.exportCSV() }
                        catch { exportError = error.localizedDescription }
                    }
                }
                .disabled(files.isEmpty)
            }
            if let exportError { Text(exportError).foregroundStyle(.red) }
        }
        .onChange(of: store.archiveVersion) { csvURL = nil }
    }
}

// MARK: - One chart

private struct HistoryChart: View {
    let points: [HistoryPoint]
    let peaks: [HistoryPoint]
    let unit: String?
    let scale: HistoryView.Scale
    let events: [HistoryEvent]
    let range: ClosedRange<Date>

    var body: some View {
        let banded = points.contains { $0.max > $0.min }
        chart(banded: banded)
            .chartXScale(domain: range)
            .chartYAxisLabel(unit ?? "")
            .frame(height: 180)
    }

    @ViewBuilder
    private func chart(banded: Bool) -> some View {
        let base = Chart {
            ForEach(events) { e in
                RuleMark(x: .value("Time", e.t))
                    .foregroundStyle(.secondary.opacity(0.4))
                    .lineStyle(StrokeStyle(lineWidth: 1, dash: [3, 3]))
                    .annotation(position: .top, spacing: 0) {
                        Image(systemName: EventRow.symbol(e.code)).font(.caption2).foregroundStyle(.secondary)
                    }
            }
            if banded {
                ForEach(points) { p in
                    AreaMark(x: .value("Time", p.t), yStart: .value("Min", p.min), yEnd: .value("Max", p.max),
                             series: .value("Segment", "band \(p.segment)"))
                        .foregroundStyle(Color.accentColor.opacity(0.2))
                }
            }
            ForEach(points) { p in
                LineMark(x: .value("Time", p.t), y: .value("Value", p.mean),
                         series: .value("Segment", "line \(p.segment)"))
                    .foregroundStyle(Color.accentColor)
            }
            ForEach(peaks) { p in
                PointMark(x: .value("Time", p.t), y: .value("Peak", p.max))
                    .symbolSize(8)
                    .foregroundStyle(.orange)
            }
        }
        switch scale {
        case .linear: base.chartYScale(domain: .automatic(includesZero: false))
        case .log: base.chartYScale(type: .log)
        case .symLog: base.chartYScale(type: .symmetricLog)
        }
    }
}

// MARK: - Events

private struct EventRow: View {
    @Environment(HistoryStore.self) private var store
    let event: HistoryEvent

    static func symbol(_ code: String) -> String {
        switch code {
        case let c where c.hasPrefix("alarm"): "alarm"
        case "boot": "power"
        case let c where c.hasPrefix("wifi"): "wifi"
        case "sntp-sync", "time-set": "clock"
        case "log-config": "slider.horizontal.3"
        default: "circle"
        }
    }

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Image(systemName: Self.symbol(event.code)).foregroundStyle(.secondary)
            VStack(alignment: .leading, spacing: 2) {
                Text(event.code)
                if !event.args.isEmpty {
                    Text("args " + event.args.map(String.init).joined(separator: " "))
                        .font(.caption.monospaced()).foregroundStyle(.secondary)
                    // What the args mean, from `history.event_args` (display only).
                    if let meaning = store.decoder?.spec.eventArgs[event.code] {
                        Text(meaning).font(.caption2).foregroundStyle(.tertiary)
                    }
                }
            }
            Spacer()
            Text(event.t.formatted(date: .abbreviated, time: .shortened))
                .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
        }
    }
}

// MARK: - Settings

/// `log status` + the period / keep / cap / enable settings. The clock checks the budget and
/// answers `denied` with the numbers when a combination would pass the cap.
private struct HistorySettingsView: View {
    @Environment(HistoryStore.self) private var store
    @Environment(\.dismiss) private var dismiss

    private static let periods = [10, 60, 300, 600, 900, 1800, 3600]
    private static let keeps = [7, 30, 90, 365, 731, 1826, 3650]
    private static let caps = [10, 50, 100, 200, 500, 1000, 2000]

    var body: some View {
        NavigationStack {
            Form {
                if let m = store.message {
                    Label(m.text, systemImage: m.ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                        .foregroundStyle(m.ok ? .green : .orange)
                }
                Section("Recording") {
                    Toggle("Record", isOn: Binding(
                        get: { store.statusValue("on") == "1" },
                        set: { on in Task { await store.set("log enable \(on ? "on" : "off")") } }))
                    setting("Period", key: "period", command: "log period", options: Self.periods) {
                        Duration.seconds($0).formatted(.units(allowed: [.hours, .minutes, .seconds], width: .abbreviated))
                    }
                    setting("Keep", key: "keep", command: "log keep", options: Self.keeps) { "\($0) days" }
                    setting("Card limit", key: "cap", command: "log cap", options: Self.caps) { "\($0) MB" }
                }
                Section("On the card") {
                    LabeledContent("Used", value: byteValue("used"))
                    LabeledContent("Projected at these settings", value: byteValue("projected"))
                    LabeledContent("Day files", value: store.statusValue("days") ?? "—")
                    LabeledContent("Records waiting in RAM", value: store.statusValue("ram") ?? "—")
                    LabeledContent("Last sync", value: store.lastSync?.formatted(date: .abbreviated, time: .shortened) ?? "never")
                }
            }
            .formStyle(.grouped)
            .navigationTitle("History settings")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } }
            }
            .task { await store.refreshStatus() }
        }
        #if os(macOS)
        .frame(minWidth: 420, minHeight: 440)
        #endif
    }

    /// A picker over `options` (plus the clock's current value if it isn't one of them).
    private func setting(_ title: String, key: String, command: String, options: [Int],
                         label: @escaping (Int) -> String) -> some View {
        let current = store.statusValue(key).flatMap(Int.init)
        let all = Array(Set(options + [current].compactMap { $0 })).sorted()
        return Picker(title, selection: Binding(
            get: { current ?? -1 },
            set: { v in Task { await store.set("\(command) \(v)") } })) {
            if current == nil { Text("—").tag(-1) }
            ForEach(all, id: \.self) { Text(label($0)).tag($0) }
        }
        .disabled(store.syncing || current == nil)
    }

    private func byteValue(_ key: String) -> String {
        store.statusValue(key).flatMap(Int64.init).map(bytes) ?? "—"
    }
}

private func bytes(_ n: some BinaryInteger) -> String {
    Int64(n).formatted(.byteCount(style: .file))
}

#Preview {
    let store = HistoryStore.preview()
    HistoryView().environment(store.link).environment(store)
}
