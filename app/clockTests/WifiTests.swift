import Foundation
import Testing
@testable import clock

/// The zone rule and Wi-Fi argument/scan formats (PROTOCOL.md "Keeping time", "Wi-Fi").
/// The strings below are what the firmware's `domain/tz.hpp` is tested against.
struct WifiTests {
    static func at(_ iso: String) -> Date { ISO8601DateFormatter().date(from: iso)! }

    @Test func posixRuleSanFrancisco() {
        let tz = TimeZone(identifier: "America/Los_Angeles")!
        #expect(PosixTimeZone.rule(for: tz, at: Self.at("2026-09-28T12:00:00Z")) == "<-08>8<-07>,M3.2.0,M11.1.0")
        // Same rule from inside winter: the order of the two transitions flips, the rule does not.
        #expect(PosixTimeZone.rule(for: tz, at: Self.at("2026-12-25T12:00:00Z")) == "<-08>8<-07>,M3.2.0,M11.1.0")
    }

    @Test func posixRuleEurope() {
        let tz = TimeZone(identifier: "Europe/Zurich")!
        #expect(PosixTimeZone.rule(for: tz, at: Self.at("2026-09-28T12:00:00Z")) == "<+01>-1<+02>,M3.5.0,M10.5.0/3")
    }

    @Test func posixRuleSouthernHemisphere() {
        let tz = TimeZone(identifier: "Australia/Sydney")!
        #expect(PosixTimeZone.rule(for: tz, at: Self.at("2026-09-28T12:00:00Z")) == "<+10>-10<+11>,M10.1.0,M4.1.0/3")
    }

    @Test func posixRuleNoDST() {
        #expect(PosixTimeZone.rule(for: TimeZone(identifier: "Asia/Kolkata")!) == "<+0530>-5:30")
        #expect(PosixTimeZone.rule(for: TimeZone(identifier: "UTC")!) == "<+00>0")
        #expect(PosixTimeZone.rule(for: TimeZone(identifier: "America/Phoenix")!) == "<-07>7")
    }

    @Test func hexArguments() {
        #expect(hexArgument("home") == "hex:686f6d65")
        #expect(hexArgument("a b\"'") == "hex:6120622227")
        #expect(hexArgument("é") == "hex:c3a9")
    }

    @Test func scanPairs() {
        let pairs: [(key: String, value: String)] = [
            ("ap", "-70/open/1/cafe"),
            ("ap", "-40/secured/6/home/upstairs"),
            ("ap", "-80/secured/11/home/upstairs"),  // the same SSID again, weaker: dropped
            ("ap", "garbage"),
            ("card", "1/2"),
        ]
        let list = WifiNetwork.list(pairs)
        #expect(list.map(\.ssid) == ["home/upstairs", "cafe"])
        #expect(list[0].secured && list[0].rssi == -40 && list[0].channel == 6)
        #expect(!list[1].secured)
    }

    @Test func contractHasWifi() throws {
        let spec = try ProtocolSpec.loadBundled()
        #expect(spec.snapshot.enums["wifi_state"] == ["off", "idle", "connecting", "online", "backoff"])
        #expect(spec.snapshot.enums["wifi_err"]?.first == "none")
        #expect(spec.snapshot.fields.contains { $0.off == 131 && $0.name == "wifi_err" })
        #expect(spec.commands.contains { $0.line.hasPrefix("net wifi join") })
    }
}
