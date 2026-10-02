import Foundation

/// The phone's copy of one clock's debug journal: the files byte for byte,
/// `Application Support/Journal/<clock identifier>/<name>`.
///
/// Nothing here is deleted because the clock deleted it. Names come from the clock and are
/// checked against `journal.name_regex` before they get here.
nonisolated struct JournalArchive: Sendable {
    let dir: URL

    init(clock: UUID, root: URL = JournalArchive.defaultRoot) {
        dir = root.appending(path: clock.uuidString, directoryHint: .isDirectory)
    }

    static var defaultRoot: URL {
        URL.applicationSupportDirectory.appending(path: "Journal", directoryHint: .isDirectory)
    }

    func url(_ name: String) -> URL { dir.appending(path: name) }

    /// Every stored file → its size, in bytes.
    func sizes() -> [String: Int] {
        let files = (try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: [.fileSizeKey],
                                                                  options: .skipsHiddenFiles)) ?? []
        var out: [String: Int] = [:]
        for f in files {
            out[f.lastPathComponent] = (try? f.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
        }
        return out
    }

    func read(_ name: String) -> Data? { try? Data(contentsOf: url(name)) }

    struct StoreError: LocalizedError {
        let errorDescription: String?
    }

    /// Writes `bytes` at `from`: from 0 replaces the file, otherwise it must continue the
    /// stored length. Atomic (temporary file + rename), so a crash never leaves a torn file.
    func store(_ name: String, from: Int, _ bytes: Data) throws {
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        var out = Data()
        if from > 0 {
            let have = read(name) ?? Data()
            guard have.count >= from else {
                throw StoreError(errorDescription: "\(name): have \(have.count) B, fetched from \(from)")
            }
            out = have.prefix(from)
        }
        out.append(bytes)
        try out.write(to: url(name), options: .atomic)
    }
}
