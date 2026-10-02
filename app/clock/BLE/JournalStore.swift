import Foundation
import Observation

/// The clock's debug journal (PROTOCOL.md "Debug journal"): mirrors the per-boot log files onto
/// the phone (`sys journal files` / `sys journal fetch` + `bulk`) and parses them for the viewer.
@Observable
final class JournalStore {
    let link: ClockLink
    /// Nil when the bundled contract has no `journal` block.
    let format: JournalFormat?

    struct Progress: Equatable {
        var total = 0
        var done = 0
        var file: String?
        var filesLeft = 0
        let started = Date.now

        var fraction: Double { total > 0 ? min(1, Double(done) / Double(total)) : 0 }
        var rate: Double {
            let t = Date.now.timeIntervalSince(started)
            return t > 0.5 ? Double(done) / t : 0
        }
    }

    private(set) var progress: Progress?
    /// `sys journal files` answered `not-present`: no card in the clock.
    private(set) var noCard = false
    private(set) var message: (text: String, ok: Bool)?
    /// `sys journal` pairs, for the header.
    private(set) var status: [(key: String, value: String)] = []
    /// The stored boots, newest first.
    private(set) var boots: [JournalBoot] = []
    /// Bumped whenever a file changes, so the lists and the viewer reload.
    private(set) var archiveVersion = 0
    /// Re-sync every `followInterval` while the tab is visible.
    var follow = false

    static let followInterval: Duration = .seconds(5)
    /// Lines the viewer renders before "Load all".
    static let tailLines = 5000

    /// Downloads need the `bulk` subscription and the contract's `journal` block.
    var supported: Bool { isPreview || (format != nil && link.hasBulk) }
    var syncing: Bool { syncTask != nil }

    /// The clock whose archive is shown: the connected one, else the last one.
    var clockID: UUID? { link.connectedID ?? link.lastClockID }
    var archive: JournalArchive? { clockID.map { JournalArchive(clock: $0) } }

    func statusValue(_ key: String) -> String? { status.first { $0.key == key }?.value }

    @ObservationIgnored private var isPreview = false
    private var syncTask: Task<Void, Never>?
    /// The clock's `current` file, from the last listing.
    @ObservationIgnored private var current: String?
    /// Per file: size, reset reason, rescued section (for the boot list).
    @ObservationIgnored private var summaries: [String: (size: Int, reset: String?, rescued: Bool)] = [:]
    /// Per file of the boot being viewed: size and its lines.
    @ObservationIgnored private var parsed: [String: (size: Int, lines: [JournalLine])] = [:]
    @ObservationIgnored private var previewLines: [JournalLine] = []

    init(link: ClockLink) {
        self.link = link
        format = link.spec.journal.flatMap(JournalFormat.init)
    }

    // MARK: Sync

    /// Syncs once (or joins the sync running) and returns when it is done. `quiet`: no
    /// "Up to date." message (Follow).
    func refresh(quiet: Bool = false) async {
        if syncTask == nil, supported, !isPreview, link.phase == .ready {
            syncTask = Task { [weak self] in
                await self?.sync(quiet: quiet)
                self?.syncTask = nil
            }
        }
        await syncTask?.value
    }

    func cancelSync() { syncTask?.cancel() }

    private func sync(quiet: Bool) async {
        guard let format, let id = link.connectedID else { return }
        // History has the one download slot: skip this round, Follow / Refresh tries again.
        guard link.beginDownload("fetching logs…") else { return }
        let archive = JournalArchive(clock: id)
        defer {
            link.endDownload()
            progress = nil
        }

        let r = await send("sys journal files")
        switch r.outcome {
        case .status(.ok): noCard = false
        case .status(.notPresent):
            noCard = true
            return
        default:
            message = ("sys journal files: \(r.summary)", false)
            return
        }
        var listing = JournalSync.listing(r.pairs)
        // Only names that are journal file names become paths on the phone.
        listing.files = listing.files.filter { format.file($0.name) != nil }
        current = listing.current
        let plan = JournalSync.plan(listing, local: archive.sizes())

        var failed: [String] = []
        if !plan.isEmpty {
            progress = Progress(total: plan.reduce(0) { $0 + $1.bytes }, filesLeft: plan.count)
        }
        for f in plan {
            if Task.isCancelled { break }
            progress?.file = f.name
            let before = progress?.done ?? 0
            var ok = false
            var gone = false
            for attempt in 0..<2 where !ok && !gone && !Task.isCancelled && link.phase == .ready {
                progress?.done = before
                do {
                    let (_, data) = try await link.fetchBulk("sys journal fetch \(f.name) \(f.from)", from: f.from,
                                                             stop: "sys journal fetch stop") { [weak self] n in
                        self?.progress?.done += n
                    }
                    try archive.store(f.name, from: f.from, data)
                    ok = true
                    archiveVersion += 1
                } catch {
                    // `failed` = the clock pruned it since the listing: nothing to retry.
                    if let e = error as? ClockLink.FetchError, e.result?.outcome == .status(.failed) { gone = true }
                    link.note("journal \(f.name) from \(f.from)\(attempt > 0 ? " (retry)" : ""): \(error.localizedDescription)")
                }
            }
            if !ok && !gone { failed.append(f.name) }
            progress?.done = before + f.bytes
            progress?.filesLeft -= 1
        }

        if Task.isCancelled {
            _ = await send("sys journal fetch stop")
            message = ("Cancelled — it resumes next time.", false)
        } else if link.phase != .ready {
            message = ("The link dropped — Refresh after reconnecting.", false)
        } else if !failed.isEmpty {
            message = ("\(failed.count) file(s) failed (\(failed.joined(separator: ", "))) — retried next time.", false)
        } else if !quiet || message?.ok == false {
            let total = plan.reduce(0) { $0 + $1.bytes }
            message = (plan.isEmpty ? "Up to date." : "Fetched \(plan.count) file(s), \(total.formatted(.byteCount(style: .file))).", true)
        }

        let s = await send("sys journal")
        if s.outcome.isOK { status = s.pairs }
        if !quiet || !plan.isEmpty { link.note("journal sync: \(message?.text ?? "")") }
    }

    private func send(_ line: String) async -> CommandResult {
        await link.send(line, echo: false, duringDownload: true)
    }

    /// `sys debug <tag> <level>`: the card only has what is logged.
    func setLevel(_ line: String) async {
        let r = await link.send(line)
        message = r.outcome.isOK ? ("\(line): ok", true) : ("\(line): \(r.summary)", false)
    }

    // MARK: Boot list and viewer

    /// Rebuilds `boots` from the local archive (off the main actor).
    func loadBoots() async {
        guard !isPreview else { return }
        guard let format, let archive else { boots = []; return }
        let cur = link.phase == .ready ? current : nil
        let (b, s) = await Self.summarise(format, archive, current: cur, cache: summaries)
        summaries = s
        boots = b
    }

    @concurrent
    nonisolated private static func summarise(_ format: JournalFormat, _ archive: JournalArchive, current: String?,
                                              cache: [String: (size: Int, reset: String?, rescued: Bool)])
        async -> ([JournalBoot], [String: (size: Int, reset: String?, rescued: Bool)]) {
        let sizes = archive.sizes()
        var cache = cache.filter { sizes[$0.key] == $0.value.size }
        var boots = format.boots(sizes: sizes, current: current)
        for i in boots.indices {
            for f in boots[i].files where cache[f.name] == nil {
                let s = archive.read(f.name).map(format.summary) ?? (reset: nil, rescued: false)
                cache[f.name] = (sizes[f.name] ?? 0, s.reset, s.rescued)
            }
            boots[i].reset = boots[i].files.first.flatMap { cache[$0.name]?.reset }
            boots[i].rescued = boots[i].files.contains { cache[$0.name]?.rescued == true }
        }
        // Newest first: the boot after `i` is `i - 1`.
        for i in boots.indices.dropFirst() where boots[i - 1].boot == boots[i].boot + 1 {
            boots[i].endedBy = boots[i - 1].reset
        }
        return (boots, cache)
    }

    /// A boot's parts concatenated, parsed (off the main actor, cached per file + size).
    func lines(for boot: JournalBoot) async -> [JournalLine] {
        if isPreview { return previewLines }
        guard let format, let archive else { return [] }
        let names = Set(boot.files.map(\.name))
        let (lines, cache) = await Self.parse(format, archive, boot.files, cache: parsed.filter { names.contains($0.key) })
        parsed = cache
        return lines
    }

    @concurrent
    nonisolated private static func parse(_ format: JournalFormat, _ archive: JournalArchive, _ files: [JournalFile],
                                          cache: [String: (size: Int, lines: [JournalLine])])
        async -> ([JournalLine], [String: (size: Int, lines: [JournalLine])]) {
        var cache = cache
        let sizes = archive.sizes()
        var out: [JournalLine] = []
        for f in files {
            let size = sizes[f.name] ?? 0
            if cache[f.name]?.size != size {
                cache[f.name] = (size, archive.read(f.name).map { format.lines($0) } ?? [])
            }
            let base = out.count
            out += (cache[f.name]?.lines ?? []).map { var l = $0; l.id += base; return l }
        }
        return (out, cache)
    }

    /// The boot's raw files, for Share.
    func urls(_ boot: JournalBoot) -> [URL] {
        archive.map { a in boot.files.map { a.url($0.name) } } ?? []
    }
}

// MARK: - Previews

extension JournalStore {
    static let previewText = """
        === boot 123  reset: power-on  journal: cold, 0 byte(s) of boot 122 rescued, 0 lost ===
        ESP-ROM:esp32s3-20210327
        I (312) app: clock 0.1.0 ed6214f profile=dev
        I (845) net: ble advertising
        W (1203) storage: card slow to mount (410 ms)
        D (5021) motion: home: sensor edge at 11 342
        E (61234) supervisor: motion stuck 182 s
        I (62000) wifi: connected, rssi -54
        --- the last lines before the reset: kept in RAM, written by boot 124 ---
        E (3600123) task_wdt: Task watchdog got triggered
        Backtrace: 0x40375a2e:0x3fc9b6f0 0x4037c1d5:0x3fc9b710
        """

    /// Three boots and a short excerpt, no radio.
    static func preview(link: ClockLink = .preview()) -> JournalStore {
        let store = JournalStore(link: link)
        store.isPreview = true
        guard let format = store.format else { return store }
        store.previewLines = format.lines(Data(previewText.utf8))
        let f = { (boot: Int, part: Int) in JournalFile(name: String(format: "%06d", boot) + (part > 0 ? "-\(part)" : "") + ".log",
                                                        boot: boot, part: part, old: false) }
        store.boots = [
            JournalBoot(boot: 124, files: [f(124, 0)], bytes: 48_211, current: true, reset: "task wdt"),
            JournalBoot(boot: 123, files: [f(123, 0), f(123, 1)], bytes: 5_204_331, current: false,
                        reset: "power-on", rescued: true, endedBy: "task wdt"),
            JournalBoot(boot: 122, files: [f(122, 0)], bytes: 912_004, current: false, reset: "sw", endedBy: "power-on"),
        ]
        store.status = [("boot", "124"), ("file", "000124.log"), ("bytes", "48211"), ("ram", "312"),
                        ("lost", "0"), ("files", "4"), ("dir_bytes", "6164546"), ("card", "ok")]
        return store
    }
}
