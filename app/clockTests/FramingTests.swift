import Foundation
import Testing
@testable import clock

struct ResponseFramerTests {
    /// The `help sys` example from PROTOCOL.md §3, at a 20-byte MTU.
    @Test func reassemblesFragments() {
        var f = ResponseFramer()
        var out: [ResponseEvent] = []
        for frame in ["8+sys stat                          one-scre",
                      "8|en: what is it doing right now",
                      "8+sys ver                           app / bu",
                      "8|ild / sdk identity",
                      "8$ok"] {
            out += f.feed(frame)
        }
        #expect(out == [
            .line(id: 8, text: "sys stat                          one-screen: what is it doing right now"),
            .line(id: 8, text: "sys ver                           app / build / sdk identity"),
            .terminal(id: 8, status: .ok),
        ])
    }

    @Test func pairsAndStatuses() {
        var f = ResponseFramer()
        #expect(f.feed("3=fw=0.1.0") == [.pair(id: 3, key: "fw", value: "0.1.0")])
        #expect(f.feed("3+a=") + f.feed("3=b") == [.pair(id: 3, key: "a", value: "b")])
        #expect(f.feed("3$denied\n") == [.terminal(id: 3, status: .denied)])
        #expect(f.feed("4$brand-new") == [.terminal(id: 4, status: CommandStatus(rawValue: "brand-new"))])
    }

    @Test func fragmentsAreKeptPerID() {
        var f = ResponseFramer()
        #expect(f.feed("1+ab").isEmpty)
        #expect(f.feed("2|x") == [.line(id: 2, text: "x")])
        #expect(f.feed("1|cd") == [.line(id: 1, text: "abcd")])
    }

    @Test func danglingFragmentFlushedBeforeTerminal() {
        var f = ResponseFramer()
        _ = f.feed("5+partial")
        #expect(f.feed("5$failed") == [.line(id: 5, text: "partial"), .terminal(id: 5, status: .failed)])
    }

    @Test func malformed() {
        var f = ResponseFramer()
        #expect(f.feed("hello") == [.malformed("hello")])
        #expect(f.feed("12") == [.malformed("12")])
        #expect(f.feed("12?x") == [.malformed("12?x")])
    }
}

struct DeviceInfoTests {
    let sample = "fw=0.1.0 sha=ed6214f built=2026-09-27T12:00:00Z board=rev0_3 profile=dev sdk=v5.5.5 proto=1 schema=1 future=x"

    @Test func parses() {
        let i = DeviceInfo(sample)
        #expect(i.firmware == "0.1.0")
        #expect(i["sha"] == "ed6214f")
        #expect(i["future"] == "x")
        #expect(i.proto == 1)
        #expect(i.pairs.count == 9)
    }

    @Test func compatibility() {
        #expect(DeviceInfo(sample).compatibility(appProto: 1) == .match)
        #expect(DeviceInfo("proto=2").compatibility(appProto: 1) == .appTooOld(clock: 2, app: 1))
        #expect(DeviceInfo("proto=1").compatibility(appProto: 2) == .firmwareOlder(clock: 1, app: 2))
        #expect(DeviceInfo("fw=1").compatibility(appProto: 1) == .unknown)
    }
}
