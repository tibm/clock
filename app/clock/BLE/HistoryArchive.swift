import Foundation

/// The phone's copy of one clock's history: the day files byte for byte,
/// `Application Support/History/<clock identifier>/<yyyymmdd>.bin`.
///
/// The raw files are the source of truth; anything derived is rebuilt from them. Nothing here
/// is ever deleted because the clock deleted it — the phone is the long-term archive.
nonisolated struct HistoryArchive: Sendable {
    let dir: URL

    init(clock: UUID, root: URL = HistoryArchive.defaultRoot) {
        dir = root.appending(path: clock.uuidString, directoryHint: .isDirectory)
    }

    static var defaultRoot: URL {
        URL.applicationSupportDirectory.appending(path: "History", directoryHint: .isDirectory)
    }

    /// Clocks with a local archive.
    static func clocks(root: URL = defaultRoot) -> [UUID] {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: root.path(percentEncoded: false))) ?? []
        return names.compactMap(UUID.init)
    }

    func url(_ day: String) -> URL { dir.appending(path: "\(day).bin") }

    /// Every stored day → its size, in bytes.
    func sizes() -> [String: Int] {
        let files = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: [.fileSizeKey])) ?? []
        var out: [String: Int] = [:]
        for f in files where f.pathExtension == "bin" {
            let day = f.deletingPathExtension().lastPathComponent
            guard day.count == 8, day.allSatisfy(\.isNumber) else { continue }
            out[day] = (try? f.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
        }
        return out
    }

    /// Days stored, oldest first.
    func days() -> [String] { sizes().keys.sorted() }

    func read(_ day: String) -> Data? { try? Data(contentsOf: url(day)) }

    struct StoreError: LocalizedError {
        let errorDescription: String?
    }

    /// Writes `bytes` at `from`: from 0 replaces the file, otherwise it must continue the
    /// stored length. Atomic (temporary file + rename), so a crash never leaves a torn file.
    func store(_ day: String, from: Int, _ bytes: Data) throws {
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        var out = Data()
        if from > 0 {
            let have = read(day) ?? Data()
            guard have.count >= from else {
                throw StoreError(errorDescription: "\(day): have \(have.count) B, fetched from \(from)")
            }
            out = have.prefix(from)
        }
        out.append(bytes)
        try out.write(to: url(day), options: .atomic)
    }

    /// Every sample as CSV, oldest first: `time_utc,<column>…,flags`. Days that don't decode are skipped.
    func csv(decoder: HistoryDecoder) -> String {
        let cols = decoder.columns
        var out = "time_utc," + cols.map { c in c.unit.map { "\(c.name) (\($0))" } ?? c.name }.joined(separator: ",") + ",flags\n"
        let iso = ISO8601DateFormatter()
        for day in days() {
            guard let data = read(day), let file = try? decoder.decode(data) else { continue }
            for s in file.samples.sorted(by: { $0.t < $1.t }) {
                out += iso.string(from: s.t)
                for i in cols.indices { out += "," + (s.value(i).map { String(format: "%g", $0) } ?? "") }
                out += "," + decoder.flagNames(s.flags).joined(separator: " ") + "\n"
            }
        }
        return out
    }
}
