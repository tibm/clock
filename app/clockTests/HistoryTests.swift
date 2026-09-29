import Foundation
import Testing
@testable import clock

struct HistoryTests {
    let spec: ProtocolSpec
    let history: ProtocolSpec.History
    let decoder: HistoryDecoder

    init() throws {
        spec = try ProtocolSpec.loadBundled()
        history = try #require(spec.history)
        decoder = HistoryDecoder(spec: history)
    }

    private func golden() throws -> ProtocolSpec.HistoryGolden { try #require(history.golden) }

    private static func near(_ a: Double?, _ b: Double, rel: Double = 1e-3) -> Bool {
        guard let a else { return false }
        return abs(a - b) <= max(abs(b) * rel, 1e-9)
    }

    /// A file: the golden header, then `records`, then optional junk.
    private func file(_ records: [Data], tail: Data = Data()) throws -> Data {
        var d = try #require(Data(hex: try golden().headerHex ?? ""))
        for r in records { d.append(r) }
        d.append(tail)
        return d
    }

    // MARK: Contract

    @Test func contractHasHistory() {
        #expect(spec.characteristicUUID("bulk") != nil)
        #expect(history.record.size == 24 && history.header.size == 32)
        #expect(history.lists["sample_flags"]?.first == "env_ok")
        #expect(history.lists["event_code"]?.contains("alarm-fire") == true)
        for e in history.encodings.keys { #expect(HistoryDecoder.Encoding(history.encodings[e]!) != nil, "\(e)") }
        #expect(decoder.columns.map(\.name).contains("temp"))
        #expect(!decoder.columns.map(\.name).contains("crc8"))
        #expect(!decoder.columns.map(\.name).contains("t"))
    }

    @Test func crc8CheckValue() {
        // CRC-8/SMBUS check value.
        #expect(HistoryDecoder.crc8(Array("123456789".utf8)) == 0xF4)
    }

    // MARK: Golden vectors

    @Test func goldenHeader() throws {
        let g = try golden()
        let h = try decoder.header([UInt8](try #require(Data(hex: g.headerHex ?? ""))))
        let want = try #require(g.headerDecoded)
        #expect(h.periodS == want["period_s"]?.number.map(Int.init))
        #expect(h.day?.timeIntervalSince1970 == want["day"]?.number)
        #expect(h.fwID == want["fw_id"]?.string)
    }

    @Test func goldenSample() throws {
        let g = try golden()
        let bytes = [UInt8](try #require(Data(hex: g.sampleHex ?? "")))
        #expect(HistoryDecoder.crc8(bytes[0..<23]) == bytes[23])
        let s = try #require(decoder.sample(bytes))
        let want = try #require(g.sampleDecoded)
        for (key, value) in want {
            switch key {
            case "t":
                #expect(s.t.timeIntervalSince1970 == value.number)
            case "flags":
                #expect(decoder.flagNames(s.flags) == value.array?.compactMap(\.string))
            default:
                let col = try #require(decoder.column(key), "no column \(key)")
                let n = try #require(value.number)
                #expect(Self.near(s.value(col), n), "\(key): \(String(describing: s.value(col)))")
            }
        }
    }

    @Test func goldenEvent() throws {
        let g = try golden()
        let raw = try #require(Data(hex: g.eventHex ?? ""))
        let e = try #require(decoder.event([UInt8](raw)))
        let want = try #require(g.eventDecoded)
        #expect(e.code == want["code"]?.string)
        #expect(e.t.timeIntervalSince1970 == want["t"]?.number)
        let args = want["args"]?.array?.compactMap(\.number).map { UInt8($0) } ?? []
        #expect(Array(e.args.prefix(args.count)) == args)
    }

    // MARK: Files

    @Test func decodesFileSkippingBadCRCAndTornTail() throws {
        let g = try golden()
        let sample = try #require(Data(hex: g.sampleHex ?? ""))
        let event = try #require(Data(hex: g.eventHex ?? ""))
        var flipped = sample
        flipped[7] ^= 0x10
        var future = Data(count: 24)
        future[0] = 9
        future[23] = HistoryDecoder.crc8(future.prefix(23))
        let f = try decoder.decode(try file([sample, flipped, event, future, sample], tail: sample.prefix(10)))
        #expect(f.samples.count == 2)
        #expect(f.events.count == 1)
        #expect(f.badCRC == 1)
        #expect(f.unknownKind == 1)
    }

    @Test func refusesBadHeader() throws {
        var d = try file([])
        d[0] = UInt8(ascii: "X")
        #expect(throws: HistoryDecoder.DecodeError.self) { try decoder.decode(d) }
        #expect(throws: HistoryDecoder.DecodeError.self) { try decoder.decode(Data(count: 10)) }
        var v = try file([])
        v[4] = 2  // version
        #expect(throws: HistoryDecoder.DecodeError.self) { try decoder.decode(v) }
    }

    @Test func invalidFlagClearsValue() throws {
        var b = [UInt8](try #require(Data(hex: try golden().sampleHex ?? "")))
        b[20] &= ~0x01  // env_ok off
        b[23] = HistoryDecoder.crc8(b[0..<23])
        let s = try #require(decoder.sample(b))
        let temp = try #require(decoder.column("temp"))
        let vbat = try #require(decoder.column("vbat"))
        #expect(s.value(temp) == nil)
        #expect(s.value(vbat) != nil)
    }

    // MARK: Sync plan

    @Test func parsesLogDays() {
        let days = HistorySync.days([("day", "20260927/6944"), ("day", "bad"), ("x", "1"), ("day", "2026092/1")])
        #expect(days.count == 1 && days[0].day == "20260927" && days[0].bytes == 6944)
    }

    @Test func planNewGrownShrunkAndClockDeleted() {
        let plan = HistorySync.plan(
            clock: [("20260928", 3000), ("20260925", 6944), ("20260926", 6944), ("20260927", 100)],
            local: ["20260920": 6944, "20260926": 6944, "20260927": 6944, "20260928": 1000])
        #expect(plan == [
            .init(day: "20260925", from: 0, size: 6944),    // new
            .init(day: "20260927", from: 0, size: 100),     // shorter on the clock: replace
            .init(day: "20260928", from: 1000, size: 3000), // grown: the tail
        ])
        #expect(plan.reduce(0) { $0 + $1.bytes } == 6944 + 100 + 2000)
    }

    // MARK: Bulk

    @Test func bulkPacket() throws {
        let p = try #require(HistorySync.packet(SoundUpload.blobValue(offset: 0x0102_0304, Data([1, 2]))))
        #expect(p.offset == 0x0102_0304 && [UInt8](p.payload) == [1, 2])
        #expect(HistorySync.packet(Data([1, 2, 3])) == nil)
    }

    @Test func assemblerResumesAndHandlesRepeatsAndGaps() {
        let bytes = Data((0..<100).map { UInt8($0) })
        var a = BulkAssembler(from: 40)
        #expect(a.add(offset: 40, bytes[40..<60]) == 20)     // before `=size=`
        a.expect(size: 100)
        #expect(a.add(offset: 50, bytes[50..<70]) == 10)     // overlap: only the new part
        #expect(a.add(offset: 40, bytes[40..<50]) == 0)      // repeat
        #expect(a.add(offset: 70, bytes[70..<100]) == 30)
        #expect(a.isComplete && a.data == bytes[40..<100])

        var g = BulkAssembler(from: 0)
        g.expect(size: 100)
        g.add(offset: 0, bytes[0..<10])
        g.add(offset: 20, bytes[20..<30])                   // a lost packet
        #expect(g.gap && !g.isComplete)
    }

    @Test func archiveAppendsAndReplaces() throws {
        let root = URL.temporaryDirectory.appending(path: "history-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: root) }
        let a = HistoryArchive(clock: UUID(), root: root)
        try a.store("20260928", from: 0, Data([1, 2, 3]))
        try a.store("20260928", from: 3, Data([4, 5]))
        #expect(a.read("20260928") == Data([1, 2, 3, 4, 5]))
        #expect(throws: HistoryArchive.StoreError.self) { try a.store("20260928", from: 9, Data([0])) }
        try a.store("20260928", from: 0, Data([9]))
        #expect(a.sizes() == ["20260928": 1])
        #expect(HistoryArchive.clocks(root: root).count == 1)
    }

    // MARK: Series

    @Test func bucketsAndSegments() {
        let t0 = 1_790_553_600.0
        let samples = (0..<24).map { i in
            HistorySample(t: Date(timeIntervalSince1970: t0 + Double(i) * 300), values: [Float(i)], flags: 0)
        }
        let raw = HistorySeries.points(samples, column: 0, bucket: 0)
        #expect(raw.count == 24)
        let hourly = HistorySeries.points(samples, column: 0, bucket: 3600)
        #expect(hourly.count == 2)
        #expect(hourly[0].min == 0 && hourly[0].max == 11 && hourly[0].mean == 5.5)
        var gappy = raw
        gappy.removeSubrange(5..<10)
        let seg = HistorySeries.segmented(gappy, maxGap: 600)
        #expect(Set(seg.map(\.segment)) == [0, 1])
        #expect(HistorySeries.bucket(span: 86400, samples: 288) == 0)
        #expect(HistorySeries.bucket(span: 365 * 86400, samples: 105_120) == 86400)
        #expect(HistorySeries.dayString(Date(timeIntervalSince1970: t0)) == "20260928")
        #expect(HistorySeries.dayStart("20260928")?.timeIntervalSince1970 == t0)
    }
}
