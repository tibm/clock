import Foundation
import Observation

/// How far back the History charts look.
nonisolated enum HistoryRange: String, CaseIterable, Identifiable, Sendable {
    case day = "24 h"
    case week = "7 d"
    case month = "30 d"
    case year = "1 y"
    case all = "All"

    var id: String { rawValue }

    /// nil = everything stored.
    var seconds: TimeInterval? {
        switch self {
        case .day: 86400
        case .week: 7 * 86400
        case .month: 30 * 86400
        case .year: 365 * 86400
        case .all: nil
        }
    }
}

/// What the charts draw: one point series per column, plus the events.
nonisolated struct HistoryChartData: Sendable {
    let columns: [HistoryColumn]
    /// `series[i]` belongs to `columns[i]`.
    let series: [[HistoryPoint]]
    let events: [HistoryEvent]
    let range: ClosedRange<Date>
    /// Seconds per bucket; 0 = every sample.
    let bucket: Int

    func points(_ name: String) -> [HistoryPoint] {
        columns.firstIndex { $0.name == name }.map { series[$0] } ?? []
    }

    var isEmpty: Bool { series.allSatisfy(\.isEmpty) && events.isEmpty }
}

/// The clock's history log (PROTOCOL.md §4 "History"): mirrors the day files onto the phone
/// (`log days` / `log fetch` + `bulk`), and turns the local copy into chart series.
@Observable
final class HistoryStore {
    let link: ClockLink
    /// nil when the bundled contract has no `history` block.
    let decoder: HistoryDecoder?

    struct Progress: Equatable {
        var total = 0
        var done = 0
        var day: String?
        var daysLeft = 0
        let started = Date.now

        var fraction: Double { total > 0 ? min(1, Double(done) / Double(total)) : 0 }
        var rate: Double {
            let t = Date.now.timeIntervalSince(started)
            return t > 0.5 ? Double(done) / t : 0
        }
    }

    private(set) var progress: Progress?
    /// `log days` answered `not-present`: no card in the clock.
    private(set) var noCard = false
    private(set) var message: (text: String, ok: Bool)?
    /// `log status` pairs, for the settings sheet.
    private(set) var status: [(key: String, value: String)] = []
    private(set) var chart: HistoryChartData?
    private(set) var loadingChart = false
    /// Bumped whenever the local archive changes, so the charts reload.
    private(set) var archiveVersion = 0

    /// Downloads need the `bulk` subscription and the contract's `history` block.
    var supported: Bool { isPreview || (decoder != nil && link.hasBulk) }
    var syncing: Bool { progress != nil }

    /// The clock whose archive is shown: the connected one, else the last one.
    var clockID: UUID? { link.connectedID ?? link.lastClockID ?? HistoryArchive.clocks().first }
    var archive: HistoryArchive? { clockID.map { HistoryArchive(clock: $0) } }

    var lastSync: Date? {
        access(keyPath: \.lastSync)
        guard let clockID else { return nil }
        return defaults.object(forKey: Self.lastSyncKey(clockID)) as? Date
    }

    @ObservationIgnored private let defaults = UserDefaults.standard
    @ObservationIgnored private var isPreview = false
    @ObservationIgnored private var syncTask: Task<Void, Never>?
    /// Per day file: its size, the bucket it was reduced with, and the result.
    @ObservationIgnored private var cache: [String: DayPoints] = [:]

    init(link: ClockLink) {
        self.link = link
        decoder = link.spec.history.map(HistoryDecoder.init)
        link.afterConnect = { [weak self] in
            guard let self else { return }
            self.startSync()
            await self.syncTask?.value
        }
    }

    private static func lastSyncKey(_ id: UUID) -> String { "historyLastSync.\(id.uuidString)" }

    // MARK: Sync

    func startSync() {
        guard syncTask == nil, supported, !isPreview, link.phase == .ready else { return }
        syncTask = Task { [weak self] in
            await self?.sync()
            self?.syncTask = nil
        }
    }

    func cancelSync() { syncTask?.cancel() }

    struct FetchError: LocalizedError {
        let errorDescription: String?
        init(_ text: String) { errorDescription = text }
    }

    private func sync() async {
        guard let id = link.connectedID else { return }
        let archive = HistoryArchive(clock: id)
        // One download at a time: wait for a journal fetch to finish.
        while link.downloading {
            if Task.isCancelled || link.phase != .ready { return }
            try? await Task.sleep(for: .milliseconds(250))
        }
        guard link.beginDownload("syncing history…") else { return }
        progress = Progress()
        message = nil
        defer {
            link.endDownload()
            progress = nil
            archiveVersion += 1
        }

        let r = await send("log days")
        switch r.outcome {
        case .status(.ok): noCard = false
        case .status(.notPresent):
            noCard = true
            return
        default:
            message = ("log days: \(r.summary)", false)
            return
        }
        let plan = HistorySync.plan(clock: HistorySync.days(r.pairs), local: archive.sizes())
        progress?.total = plan.reduce(0) { $0 + $1.bytes }
        progress?.daysLeft = plan.count

        var failed: [String] = []
        for f in plan {
            if Task.isCancelled { break }
            progress?.day = f.day
            let before = progress?.done ?? 0
            var ok = false
            for attempt in 0..<2 where !ok && !Task.isCancelled && link.phase == .ready {
                progress?.done = before
                do {
                    try await fetch(f, into: archive)
                    ok = true
                } catch {
                    link.note("history \(f.day) from \(f.from)\(attempt > 0 ? " (retry)" : ""): \(error.localizedDescription)")
                }
            }
            if !ok { failed.append(f.day) }
            progress?.done = before + f.bytes
            progress?.daysLeft -= 1
        }

        if Task.isCancelled {
            _ = await send("log fetch stop")
            message = ("Sync cancelled — it resumes next time.", false)
        } else if link.phase != .ready {
            message = ("The link dropped — the sync resumes on the next connect.", false)
        } else if failed.isEmpty {
            withMutation(keyPath: \.lastSync) { defaults.set(Date.now, forKey: Self.lastSyncKey(id)) }
            let total = progress?.total ?? 0
            message = (plan.isEmpty ? "Up to date." : "Synced \(plan.count) day(s), \(total.formatted(.byteCount(style: .file))).", true)
        } else {
            message = ("\(failed.count) day(s) failed (\(failed.joined(separator: ", "))) — retried next sync.", false)
        }
        link.note("history sync: \(message?.text ?? "")")
    }

    /// `log fetch <day> <from>` → `bulk` → CRC-32 → append to the local file.
    private func fetch(_ f: HistorySync.Fetch, into archive: HistoryArchive) async throws {
        let (_, data) = try await link.fetchBulk("log fetch \(f.day) \(f.from)", from: f.from,
                                                 stop: "log fetch stop") { [weak self] n in
            self?.progress?.done += n
        }
        try archive.store(f.day, from: f.from, data)
    }

    private func send(_ line: String) async -> CommandResult {
        await link.send(line, echo: false, duringDownload: true)
    }

    // MARK: Settings (`log status` / `log period|keep|cap|enable`)

    func refreshStatus() async {
        let r = await link.send("log status", echo: false)
        if r.outcome.isOK { status = r.pairs }
        else if r.outcome == .status(.notPresent) { noCard = true }
    }

    func statusValue(_ key: String) -> String? { status.first { $0.key == key }?.value }

    /// Sends one setting; on `denied` the clock's `|` line gives the budget numbers.
    func set(_ line: String) async {
        let r = await link.send(line)
        message = r.outcome.isOK ? ("\(line): ok", true) : ("\(line): \(r.summary)", false)
        await refreshStatus()
    }

    /// Period used for gaps and bucket estimates: the clock's setting, else the contract default.
    var periodS: Int {
        statusValue("period").flatMap(Int.init) ?? link.spec.history?.defaults?.periodS ?? 300
    }

    // MARK: Charts

    nonisolated struct DayPoints: Sendable {
        let size: Int
        let bucket: Int
        let period: Int?
        let series: [[HistoryPoint]]
        let events: [HistoryEvent]
        let samples: Int
    }

    /// Rebuilds `chart` from the local archive (off the main actor).
    func loadChart(_ range: HistoryRange) async {
        guard !isPreview else { return }
        guard let decoder, let archive else { chart = nil; return }
        loadingChart = true
        defer { loadingChart = false }
        let (data, cache) = await Self.compute(archive: archive, decoder: decoder, range: range,
                                               period: periodS, cache: cache)
        self.cache = cache
        chart = data
    }

    @concurrent
    nonisolated private static func compute(archive: HistoryArchive, decoder: HistoryDecoder,
                                            range: HistoryRange, period: Int,
                                            cache: [String: DayPoints]) async -> (HistoryChartData, [String: DayPoints]) {
        let sizes = archive.sizes()
        let days = sizes.keys.sorted()
        let now = Date.now
        let start = range.seconds.map { now.addingTimeInterval(-$0) }
            ?? days.first.flatMap(HistorySeries.dayStart) ?? now.addingTimeInterval(-86400)
        let span = now.timeIntervalSince(start)
        let bucket = HistorySeries.bucket(span: span, samples: Int(span) / max(1, period))
        let window = start...now

        var cache = cache
        var series = Array(repeating: [HistoryPoint](), count: decoder.columns.count)
        var events: [HistoryEvent] = []
        var maxPeriod = period
        for day in days {
            guard let dayStart = HistorySeries.dayStart(day),
                  dayStart.addingTimeInterval(86400) > start, dayStart <= now, let size = sizes[day] else { continue }
            var dp = cache[day]
            if dp == nil || dp?.size != size || dp?.bucket != bucket {
                guard let data = archive.read(day), let file = try? decoder.decode(data) else { continue }
                dp = DayPoints(size: size, bucket: bucket, period: file.header.periodS,
                               series: decoder.columns.indices.map {
                                   HistorySeries.points(file.samples, column: $0, bucket: bucket)
                               },
                               events: file.events, samples: file.samples.count)
                cache[day] = dp
            }
            guard let dp else { continue }
            if let p = dp.period { maxPeriod = max(maxPeriod, p) }
            for i in series.indices where i < dp.series.count {
                series[i] += dp.series[i].filter { window.contains($0.t) }
            }
            events += dp.events.filter { window.contains($0.t) }
        }
        let gap = 2 * Double(max(bucket, maxPeriod))
        let data = HistoryChartData(columns: decoder.columns,
                                    series: series.map { HistorySeries.segmented($0.sorted { $0.t < $1.t }, maxGap: gap) },
                                    events: events.sorted { $0.t < $1.t }, range: window, bucket: bucket)
        return (data, cache)
    }

    // MARK: Export

    /// The stored `.bin` files, oldest first.
    var rawFiles: [URL] { archive.map { a in a.days().map(a.url) } ?? [] }

    /// A CSV of every stored sample, written to a temporary file.
    func exportCSV() async throws -> URL {
        guard let decoder, let archive else { throw FetchError("no history") }
        let url = URL.temporaryDirectory.appending(path: "clock-history.csv")
        let text = await Self.csv(archive, decoder)
        try Data(text.utf8).write(to: url, options: .atomic)
        return url
    }

    @concurrent
    nonisolated private static func csv(_ archive: HistoryArchive, _ decoder: HistoryDecoder) async -> String {
        archive.csv(decoder: decoder)
    }
}

// MARK: - Previews

extension HistoryStore {
    /// A day of made-up samples, no radio.
    static func preview(link: ClockLink = .preview()) -> HistoryStore {
        let store = HistoryStore(link: link)
        store.isPreview = true
        guard let decoder = store.decoder else { return store }
        let now = Date.now
        let start = now.addingTimeInterval(-86400)
        let wave: [String: (Double, Double)] = [
            "temp": (21, 1.5), "rh": (42, 6), "press": (1013, 3), "gas": (60000, 20000),
            "lux": (150, 149), "lux_max": (600, 590), "vbat": (4000, 40), "soc": (85, 5), "rssi": (-55, 6), "n": (30, 0),
        ]
        let series = decoder.columns.map { c -> [HistoryPoint] in
            guard let (mid, amp) = wave[c.name] else { return [] }
            return stride(from: 0.0, to: 86400, by: 300).compactMap { dt in
                if (30000..<32000).contains(dt) { return nil }  // a gap
                let v = mid + amp * sin(dt / 86400 * 2 * .pi)
                return HistoryPoint(t: start.addingTimeInterval(dt), mean: v, min: v, max: v)
            }
        }
        store.chart = HistoryChartData(
            columns: decoder.columns,
            series: series.map { HistorySeries.segmented($0, maxGap: 600) },
            events: [HistoryEvent(t: start.addingTimeInterval(25000), code: "alarm-fire", args: [6, 45]),
                     HistoryEvent(t: start.addingTimeInterval(60000), code: "wifi-online", args: [200])],
            range: start...now, bucket: 0)
        store.status = [("on", "1"), ("period", "300"), ("keep", "731"), ("cap", "200"),
                        ("projected", "24117248"), ("used", "1212416"), ("days", "12"), ("ram", "3")]
        return store
    }
}
