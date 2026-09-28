//
//  clockTests.swift
//  clockTests
//
//  Created by Tibo Mauron on 9/27/26.
//

import Foundation
import Testing
@testable import clock

struct ProtocolSpecTests {
    /// The bundled contract, from the host app's bundle (tests run inside the app).
    let spec: ProtocolSpec

    init() throws { spec = try ProtocolSpec.loadBundled() }

    @Test func loadsBundledContract() {
        #expect(spec.protocolVersion >= 1)
        #expect(spec.characteristicUUID("cmd") != nil)
        #expect(spec.characteristicUUID("rsp") != nil)
        #expect(spec.characteristicUUID("status") != nil)
        #expect(spec.characteristicUUID("info") != nil)
        #expect(spec.advertisingStateBit("pairing_window_open") == 0)
        #expect(spec.commandChannel.statuses.contains("ok"))
    }

    @Test func everyFieldTypeIsKnownAndFits() {
        for f in spec.snapshot.fields {
            let w = SnapshotDecoder.width(of: f.type)
            #expect(w != nil, "unknown type \(f.type) for \(f.name)")
            #expect(f.off + (w ?? 0) <= spec.snapshot.size, "\(f.name) runs past size")
        }
    }
}

struct SnapshotDecoderTests {
    let spec: ProtocolSpec
    let decoder: SnapshotDecoder

    init() throws {
        spec = try ProtocolSpec.loadBundled()
        decoder = SnapshotDecoder(spec: spec)
    }

    var goldenData: Data {
        get throws {
            let golden = try #require(spec.snapshot.golden)
            return try #require(Data(hex: golden.hex))
        }
    }

    /// PROTOCOL.md §8: the golden `hex` must decode to exactly `decoded`.
    @Test func goldenVector() throws {
        let golden = try #require(spec.snapshot.golden)
        let snap = try decoder.decode(try goldenData)

        for (key, want) in golden.decoded {
            switch key {
            case "flags_set":
                let names = try #require(want.array).compactMap(\.string)
                #expect(Set(snap.flagsSet) == Set(names))
            case "pixels":
                let quads = try #require(want.array).map { ($0.array ?? []).compactMap(\.number).map { UInt8($0) } }
                #expect(snap.pixels == quads)
            case let k where k.hasSuffix("_raw") && snap[String(k.dropLast(4))] != nil:
                // e.g. `opto_raw`: the unscaled value of `opto`.
                let f = try #require(snap[String(k.dropLast(4))])
                #expect(f.raw.number == want.number, "\(k)")
            default:
                let f = try #require(snap[key], "golden key \(key) has no field")
                let w = try #require(want.number, "\(key) not numeric")
                let got = try #require(f.raw.number, "\(key) not numeric") * (f.spec.scale ?? 1)
                if f.spec.scale != nil || f.spec.type.hasPrefix("f") {
                    #expect(abs(got - w) < 1e-3, "\(key): \(got) vs \(w)")
                } else {
                    #expect(got == w, "\(key): \(got) vs \(w)")
                }
            }
        }
    }

    @Test func validityFollowsFlags() throws {
        let snap = try decoder.decode(try goldenData)
        // env_ok set → room values valid; power_ok clear → battery shown as "—".
        #expect(snap["temp"]?.isValid == true)
        #expect(snap["temp"]?.display == "-12.34 degC")
        #expect(snap["vbat_mv"]?.isValid == false)
        #expect(snap["vbat_mv"]?.display == "—")
        #expect(snap["ui_mode"]?.display == "pairing")
        #expect(snap["reset_reason"]?.display == "software")
    }

    @Test func rejectsShortRecord() throws {
        let short = try goldenData.prefix(100)
        #expect(throws: SnapshotDecoder.DecodeError.tooShort(got: 100, need: spec.snapshot.size)) {
            try decoder.decode(Data(short))
        }
    }

    @Test func rejectsWrongSchema() throws {
        var d = try goldenData
        d[0] = 2
        #expect(throws: SnapshotDecoder.DecodeError.wrongSchema(got: 2, want: spec.snapshotSchema)) {
            try decoder.decode(d)
        }
    }

    @Test func ignoresTrailingBytes() throws {
        var d = try goldenData
        d.append(contentsOf: [1, 2, 3, 4])
        #expect(try decoder.decode(d)["seq"]?.raw == .uint(48879))
    }

    @Test func unknownEnumAndFlagBits() throws {
        var d = try goldenData
        let ui = try #require(spec.snapshot.fields.first { $0.name == "ui_mode" })
        d[ui.off] = 200
        let flags = try #require(spec.snapshot.fields.first { $0.name == "flags" })
        d[flags.off + 3] |= 0x80  // bit 31
        let snap = try decoder.decode(d)
        #expect(snap["ui_mode"]?.display == "unknown(200)")
        if spec.snapshot.flags.count <= 31 { #expect(snap.unknownFlagBits == [31]) }
    }

    @Test func localTimeAddsOffset() throws {
        let snap = try decoder.decode(try goldenData)
        let t = try #require(snap.localTime)
        #expect(t.hasDate == false)  // date_valid clear in the golden record
        // epoch_ms + tz_off_min (-300) × 60 000
        #expect(abs(t.date.timeIntervalSince1970 - (1_790_000_000.123 - 300 * 60)) < 1e-6)
    }

    @Test func argSpecsAreRangesOrHints() throws {
        let epoch = try #require(spec.commands.first { $0.line.hasPrefix("chrono time epoch") })
        #expect(epoch.args?["utc_offset_min"] == .range(-720, 840))
        if case .hint = epoch.args?["unix_ms"] {} else { Issue.record("unix_ms should be a hint") }
    }
}
