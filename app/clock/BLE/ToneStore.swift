import Foundation
import Observation

/// A WAV shipped in the app bundle (the `clock/Tones/` folder), ready to upload.
struct BundledTone: Identifiable, Equatable {
    var id: String { name }
    let name: String
    let url: URL
    let size: Int
    let header: WAVHeader?
    /// Why the clock would refuse it, nil when it is uploadable.
    let problem: String?

    /// Every `.wav` in the bundle, checked against the contract's format.
    static func loadAll(spec: ProtocolSpec, bundle: Bundle = .main) -> [BundledTone] {
        let urls = bundle.urls(forResourcesWithExtension: "wav", subdirectory: nil) ?? []
        return urls.map { url in
            let size = (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
            let head = (try? FileHandle(forReadingFrom: url)).flatMap { h in
                defer { try? h.close() }
                return try? h.read(upToCount: 4096)
            }
            let header = head.flatMap(WAVHeader.init)
            var problem: String?
            if let sf = spec.soundFiles {
                if header == nil { problem = "not a WAV" }
                else if let p = header?.problem(against: sf.format) { problem = p }
                else if size > sf.maxUploadBytes { problem = "larger than \(sf.maxUploadBytes >> 20) MB" }
            }
            return BundledTone(name: url.lastPathComponent, url: url, size: size, header: header, problem: problem)
        }
        .sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }
}

/// The clock's `/sd/tones`: list, delete, upload (PROTOCOL.md §4 "Sound files").
@Observable
final class ToneStore {
    let link: ClockLink
    let bundled: [BundledTone]

    struct Upload: Equatable {
        let name: String
        let total: Int
        var sent = 0
        var resumedAt = 0
        let started = Date.now
        var phase = "starting"

        var fraction: Double { total > 0 ? Double(sent) / Double(total) : 0 }
        /// Bytes per second since this session started (resumed bytes excluded).
        var rate: Double {
            let t = Date.now.timeIntervalSince(started)
            return t > 0.5 ? Double(sent - resumedAt) / t : 0
        }
    }

    private(set) var list: ToneList?
    /// `storage tones` answered `not-present`: no card → hide the feature.
    private(set) var noCard = false
    private(set) var listError: String?
    private(set) var loading = false
    private(set) var upload: Upload?
    /// Outcome of the last upload / delete, for the user.
    private(set) var message: (text: String, ok: Bool)?

    @ObservationIgnored private var uploadTask: Task<Void, Never>?

    /// Sound files need both the `blob` characteristic and the contract's `sound_files`.
    var supported: Bool { isPreview || (link.hasBlob && link.spec.soundFiles != nil) }
    @ObservationIgnored private var isPreview = false

    init(link: ClockLink, bundled: [BundledTone]? = nil) {
        self.link = link
        self.bundled = bundled ?? BundledTone.loadAll(spec: link.spec)
    }

    /// Same name and size on the card: already uploaded.
    func isOnClock(_ tone: BundledTone) -> Bool { isOnClock(name: tone.name, size: tone.size) }

    // MARK: List / delete / use

    func refresh() async {
        guard link.phase == .ready else { return }
        loading = true
        defer { loading = false }
        let r = await link.send("storage tones", echo: false)
        switch r.outcome {
        case .status(.ok):
            list = ToneList(pairs: r.pairs)
            noCard = false
            listError = nil
        case .status(.notPresent):
            list = nil
            noCard = true
            listError = nil
        default:
            listError = r.outcome.label
        }
    }

    func delete(_ name: String) async {
        let r = await link.send("storage rm \(name)")
        message = r.outcome.isOK ? ("Deleted \(name)", true) : ("Delete \(name): \(r.outcome.label)", false)
        await refresh()
    }

    /// `nil` = the built-in beep.
    func setAlarm(_ name: String?) async {
        let r = await link.send("chrono alarm tone \(name ?? "none")")
        if !r.outcome.isOK { message = ("Alarm tone: \(r.outcome.label)", false) }
        await refresh()
    }

    func play(_ name: String) async { await link.send("audio play \(name)") }
    func stop() async { await link.send("audio stop") }

    // MARK: Upload

    func startUpload(_ tone: BundledTone) {
        guard upload == nil else { return }
        upload = Upload(name: tone.name, total: tone.size)
        message = nil
        uploadTask = Task { [weak self] in
            guard let self else { return }
            let result: (String, Bool)
            do {
                let data = try Data(contentsOf: tone.url, options: .mappedIfSafe)
                try await self.put(name: tone.name, data: data)
                result = ("Uploaded \(tone.name)", true)
            } catch is CancellationError {
                _ = await self.link.send("storage put abort", echo: false)
                result = ("Upload of \(tone.name) cancelled", false)
            } catch {
                result = ("Upload of \(tone.name) failed: \(error.localizedDescription)", false)
            }
            self.link.note(result.0)
            self.message = result
            self.upload = nil
            self.uploadTask = nil
            await self.refresh()
        }
    }

    func cancelUpload() { uploadTask?.cancel() }

    struct UploadError: LocalizedError {
        let errorDescription: String?
        init(_ text: String) { errorDescription = text }
    }

    /// `storage put` → `blob` writes → `storage put end`, with the recovery table of PROTOCOL.md.
    private func put(name: String, data: Data) async throws {
        let crc = SoundUpload.hex(SoundUpload.crc32(data))
        // Open (or resume — same name/size/crc) and return the clock's `next`.
        func begin() async throws -> Int {
            let r = await link.send("storage put \(name) \(data.count) \(crc)", echo: false)
            guard r.outcome.isOK, let next = r.pair("next").flatMap(Int.init) else {
                throw UploadError(Self.describe(r))
            }
            return next
        }
        guard let chunk = link.blobChunkSize else { throw UploadError("not connected") }

        var off = try await begin()
        upload?.sent = off
        upload?.resumedAt = off
        var restarts = 0
        var ends = 0
        while true {
            upload?.phase = "sending"
            while off < data.count {
                try Task.checkCancellation()
                let end = min(off + chunk, data.count)
                let r = await link.writeBlob(SoundUpload.blobValue(offset: off, data.subdata(in: off..<end)))
                switch r {
                case .ok:
                    off = end
                    upload?.sent = off
                case .att(let code):
                    switch link.spec.blobErrorName(code) {
                    case "busy":
                        try await Task.sleep(for: .milliseconds(50))
                    case "bad-offset", "no-upload":
                        restarts += 1
                        guard restarts <= 5 else { throw UploadError("the clock keeps refusing the offset") }
                        off = try await begin()
                        upload?.sent = off
                    case let n:
                        _ = await link.send("storage put abort", echo: false)
                        throw UploadError("the clock refused a write (\(n ?? String(format: "ATT 0x%02x", code)))")
                    }
                case .failed(let why): throw UploadError(why)
                case .linkLost, .notConnected: throw UploadError("the link dropped — upload again to resume")
                }
            }
            upload?.phase = "checking"
            let r = await link.send("storage put end", echo: false)
            switch r.outcome {
            case .status(.ok):
                return
            case .status(.notReady):
                // Bytes missing: continue from the clock's `next`.
                ends += 1
                guard ends <= 3, let next = r.pair("next").flatMap(Int.init) else { throw UploadError(Self.describe(r)) }
                off = next
            case .timeout:
                // The check may simply be slow: no open upload + the file listed = done.
                let q = await link.send("storage put", echo: false)
                await refresh()
                if q.outcome == .status(.notReady), isOnClock(name: name, size: data.count) { return }
                throw UploadError("no answer to `storage put end`")
            default:
                throw UploadError(Self.describe(r))
            }
        }
    }

    private func isOnClock(name: String, size: Int) -> Bool { list?.tone(named: name)?.size == size }

    /// The status, plus the clock's explanation (display only).
    private static func describe(_ r: CommandResult) -> String {
        r.lines.isEmpty ? r.outcome.label : "\(r.outcome.label) — \(r.lines.joined(separator: " "))"
    }
}

// MARK: - Previews

extension ToneStore {
    static func preview() -> ToneStore {
        let link = ClockLink.preview()
        let store = ToneStore(link: link, bundled: [
            BundledTone(name: "birds.wav", url: URL(filePath: "/dev/null"), size: 19280, header: nil, problem: nil),
            BundledTone(name: "chimes.wav", url: URL(filePath: "/dev/null"), size: 960_044, header: nil, problem: nil),
            BundledTone(name: "cd.wav", url: URL(filePath: "/dev/null"), size: 88244, header: nil, problem: "44100 Hz, needs 48000"),
        ])
        store.isPreview = true
        store.list = ToneList(pairs: [
            ("card", "31914983424/31900000000"),
            ("alarm", "birds.wav"),
            ("tone", "19280/200/ok/birds.wav"),
            ("tone", "88244/1000/rate/old.wav"),
        ])
        store.upload = Upload(name: "chimes.wav", total: 960_044, sent: 312_000, phase: "sending")
        return store
    }
}
