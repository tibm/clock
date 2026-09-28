import Foundation
import Testing
@testable import clock

struct CommandCatalogTests {
    let spec: ProtocolSpec
    init() throws { spec = try ProtocolSpec.loadBundled() }

    /// Rows exactly as `help sys` prints them (registry.cpp: grammar padded to column 34).
    static let helpSys = [
        "sys stat                          one-screen: what is it doing right now",
        "sys ver                           app / build / sdk identity",
        "sys snap [--hex]                  everything, timestamped: the app's status record",
        "sys debug [<module|glob|all> <level>] show or set per-module log levels",
        "sys reboot [ota|dfu]              restart the whole image   [unsafe]",
        "sys coredump info                 is there a coredump, and from what",
    ]

    @Test func tokenizesGrammar() {
        typealias T = CommandEntry.Token
        #expect(CommandEntry.tokenize("audio tone [<hz>] [<ms>]") == [
            .word("audio"), .word("tone"),
            .arg(name: "hz", optional: true, choices: []), .arg(name: "ms", optional: true, choices: []),
        ])
        #expect(CommandEntry.tokenize("chrono alarm arm <on|off>").last == .arg(name: "on|off", optional: false, choices: ["on", "off"]))
        #expect(CommandEntry.tokenize("chrono time set <hh:mm[:ss]>").last == .arg(name: "hh:mm[:ss]", optional: false, choices: []))
        #expect(CommandEntry.tokenize("sys snap [--hex]").last == .arg(name: "--hex", optional: true, choices: ["--hex"]))
    }

    @Test func parsesHelpRows() throws {
        let reboot = try #require(CommandCatalog.parseHelpRow(Self.helpSys[4], group: "sys"))
        #expect(reboot.grammar == "sys reboot [ota|dfu]")
        #expect(reboot.help == "restart the whole image")
        #expect(reboot.unsafe)
        // One-space pad (grammar ≥ 33 chars): ends at the last placeholder.
        let debug = try #require(CommandCatalog.parseHelpRow(Self.helpSys[3], group: "sys"))
        #expect(debug.grammar == "sys debug [<module|glob|all> <level>]")
        #expect(debug.help == "show or set per-module log levels")
        #expect(CommandCatalog.parseHelpRow("no such group: sys", group: "sys") == nil)
    }

    @Test func parsesGroups() {
        let lines = ["groups  sys  help  unsafe  sensor  ui  motion  chrono  board  audio  net  ",
                     "        help [<group> [<verb>]]      unsafe <on|off>"]
        #expect(CommandCatalog.parseGroups(lines) == ["sys", "help", "unsafe", "sensor", "ui", "motion", "chrono", "board", "audio", "net"])
    }

    @Test func mergeKeepsSpecAndAddsDeviceOnly() {
        var c = CommandCatalog(spec: spec)
        let before = c.entries.count
        let added = c.merge(helpRows: Self.helpSys, group: "sys")
        // `sys ver` and `sys reboot` already come from the JSON; stat/snap --hex… are new shapes.
        #expect(added == c.entries.count - before)
        let ver = c.entries.first { $0.grammar == "sys ver" }
        #expect(ver?.sources == [.spec, .device])
        #expect(ver?.status == "implemented")
        #expect(c.entries.contains { $0.grammar == "sys stat" && $0.status == "device" })
    }

    @Test func completesWordByWord() {
        let c = CommandCatalog(spec: spec)
        #expect(c.completions(for: "au").map(\.label) == ["audio"])
        #expect(c.completions(for: "audio ").map(\.label).starts(with: ["stop", "tone", "vol"]))
        #expect(c.completions(for: "audio v").first?.insert == "audio vol ")
        #expect(c.completions(for: "audio vol ").map(\.label) == ["<pct 0–100>"])
        #expect(c.completions(for: "audio vol ").first?.insert == nil)
        #expect(c.completions(for: "chrono alarm arm ").map(\.label) == ["off", "on"])
        #expect(c.completions(for: "zzz").isEmpty)
    }

    @Test func matchingFiltersTable() {
        let c = CommandCatalog(spec: spec)
        #expect(c.matching("net ble p").map(\.grammar).sorted() == ["net ble pair off", "net ble period <ms>"])
    }
}
