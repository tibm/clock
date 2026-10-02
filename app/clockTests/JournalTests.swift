import Foundation
import Testing
@testable import clock

struct JournalTests {
    let spec: ProtocolSpec
    let format: JournalFormat

    init() throws {
        spec = try ProtocolSpec.loadBundled()
        let journal = try #require(spec.journal)
        format = try #require(JournalFormat(journal))
    }

    // MARK: Contract

    @Test func contractHasJournal() {
        #expect(spec.commands.contains { $0.line.hasPrefix("sys journal files") })
        #expect(spec.commands.contains { $0.line.hasPrefix("sys journal fetch <name>") })
        #expect(format.spec.resetReasons?.contains("task wdt") == true)
        #expect(format.spec.partBytes == 4 * 1024 * 1024)
    }

    // MARK: Listing + plan

    @Test func parsesListing() {
        let l = JournalSync.listing([("file", "000122.log/912004"), ("file", "000123-1.log/100"), ("file", "bad"),
                                     ("file", "/5"), ("x", "1"), ("boot", "124"), ("current", "000124.log")])
        #expect(l.files.map(\.name) == ["000122.log", "000123-1.log"])
        #expect(l.files.map(\.bytes) == [912_004, 100])
        #expect(l.boot == 124 && l.current == "000124.log")
    }

    @Test func planNewGrownShrunkRescuedAndPruned() {
        let listing = JournalSync.listing([
            ("file", "000121.log/500"),      // shrunk: another card → replace
            ("file", "000123.log/4194304"),  // unchanged
            ("file", "000123-1.log/9000"),   // previous boot grew once after the reset (rescued lines)
            ("file", "000124.log/300"),      // new: current
            ("boot", "124"), ("current", "000124.log"),
        ])
        let plan = JournalSync.plan(listing, local: [
            "000120.log": 7000,              // pruned on the clock: kept, not fetched
            "000121.log": 2000,
            "000123.log": 4_194_304,
            "000123-1.log": 8000,
        ])
        #expect(plan == [
            .init(name: "000121.log", from: 0, size: 500),
            .init(name: "000123-1.log", from: 8000, size: 9000),
            .init(name: "000124.log", from: 0, size: 300),
        ])
        #expect(plan.last?.name == listing.current)
    }

    // MARK: Names + boots

    @Test func namesAndPartOrder() throws {
        #expect(format.file("000123.log") == JournalFile(name: "000123.log", boot: 123, part: 0, old: false))
        #expect(format.file("000123-12.log")?.part == 12)
        #expect(format.file("000123.old")?.old == true)
        for bad in ["123.log", "000123-0.log", "../000123.log", "000123.txt", "000123.log.tmp"] {
            #expect(format.file(bad) == nil, "\(bad)")
        }
        let boots = format.boots(sizes: ["000123-10.log": 1, "000123-2.log": 1, "000123.log": 4, "000123.old": 2,
                                         "000123-1.log": 1, "000124.log": 7, "notes.txt": 9],
                                 current: "000124.log")
        #expect(boots.map(\.boot) == [124, 123])
        #expect(boots[0].current && !boots[1].current)
        #expect(boots[1].files.map(\.name) == ["000123.old", "000123.log", "000123-1.log", "000123-2.log", "000123-10.log"])
        #expect(boots[1].bytes == 9)
    }

    @Test func partsConcatenatedInOrder() {
        let a = Data("=== boot 5  reset: sw  journal: cold ===\nI (1) app: one\n".utf8)
        let b = Data("=== boot 5 part 1  reset: sw  journal: cold ===\nI (2) app: two\n".utf8)
        let lines = format.lines(a) + format.lines(b, firstID: 2)
        #expect(lines.map(\.id) == [0, 1, 2, 3])
        #expect(lines.filter { $0.kind == .log }.map(\.text) == ["one", "two"])
    }

    // MARK: Lines

    static let excerpt = """
        === boot 123 part 1 (continued)  reset: task wdt  journal: warm, 812 byte(s) of boot 122 rescued, 0 lost ===
        ESP-ROM:esp32s3-20210327
        I (312) app: clock 0.1.0: ed6214f
        W (1203) wifi: sta disconnected, reason 201
        D (5021) motion: home: edge at 11 342
        --- the last lines before the reset: kept in RAM, written by boot 124 ---
        E (3600123) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
        Backtrace: 0x40375a2e:0x3fc9b6f0 0x4037c1d5:0x3fc9b710
        === boot 124  reset: task wdt  journal: warm ===

        """

    @Test func parsesExcerpt() throws {
        let lines = format.lines(Data(Self.excerpt.utf8))
        #expect(lines.count == 9)
        #expect(lines.map(\.kind) == [.header, .plain, .log, .log, .log, .rescued, .log, .plain, .header])
        let info = lines[2]
        #expect(info.level == "I" && info.ms == 312 && info.tag == "app" && info.text == "clock 0.1.0: ed6214f")
        #expect(lines[3].tag == "wifi" && lines[3].level == "W")
        #expect(lines[0..<5].allSatisfy { !$0.beforeReset })
        #expect(lines[5..<8].allSatisfy { $0.beforeReset })
        #expect(!lines[8].beforeReset)
        #expect(format.resetReason(lines[0].raw) == "task wdt")
        #expect(format.resetReason("=== boot 1  reset: power-on  journal: cold ===") == "power-on")
        #expect(format.resetReason("=== boot 1  reset: new thing  journal: x ===") == "new thing")
        #expect(format.resetReason(lines[2].raw) == nil)
        let s = format.summary(Data(Self.excerpt.utf8))
        #expect(s.reset == "task wdt" && s.rescued)
    }

    @Test func lossyUTF8AndCRLF() {
        var d = Data("I (1) app: ok\r\n".utf8)
        d.append(contentsOf: [0x49, 0x20, 0xFF, 0x0A])
        let lines = format.lines(d)
        #expect(lines.count == 2 && lines[0].text == "ok" && lines[1].kind == .plain)
    }

    @Test func filterAndTime() {
        let lines = format.lines(Data(Self.excerpt.utf8))
        let errors = JournalFormat.filter(lines, levels: ["E"], tag: nil, search: "")
        #expect(errors.map(\.kind) == [.header, .plain, .rescued, .log, .plain, .header])
        let tagged = JournalFormat.filter(lines, levels: ["E", "W", "I", "D", "V"], tag: "wifi", search: "")
        #expect(tagged.filter { $0.kind == .log }.map(\.tag) == ["wifi"])
        #expect(!tagged.contains { $0.kind == .plain })
        let found = JournalFormat.filter(lines, levels: ["I"], tag: nil, search: "ED6214F")
        #expect(found.filter { $0.kind == .log }.count == 1)
        #expect(JournalFormat.elapsed(ms: 3_600_123) == "+1:00:00.123")
        #expect(JournalFormat.elapsed(ms: 61_005) == "+0:01:01.005")
    }

    @Test func archiveAppendsAndReplaces() throws {
        let root = URL.temporaryDirectory.appending(path: "journal-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: root) }
        let a = JournalArchive(clock: UUID(), root: root)
        try a.store("000001.log", from: 0, Data("ab".utf8))
        try a.store("000001.log", from: 2, Data("c".utf8))
        #expect(a.read("000001.log") == Data("abc".utf8))
        #expect(throws: JournalArchive.StoreError.self) { try a.store("000001.log", from: 9, Data()) }
        try a.store("000001.log", from: 0, Data("z".utf8))
        #expect(a.sizes() == ["000001.log": 1])
    }
}
