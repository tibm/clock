import Foundation
import Testing
@testable import clock

@MainActor
struct ShellHistoryTests {
    @Test func upDownWalksAndRestoresDraft() {
        UserDefaults.standard.removeObject(forKey: "shellHistory")
        var h = ShellHistory()
        h.add("sys ver")
        h.add("help")
        #expect(h.step(-1, current: "au") == "help")
        #expect(h.step(-1, current: "help") == "sys ver")
        #expect(h.step(-1, current: "sys ver") == "sys ver")  // stays at oldest
        #expect(h.step(1, current: "sys ver") == "help")
        #expect(h.step(1, current: "help") == "au")  // back to the draft
        #expect(h.step(1, current: "au") == nil)
        UserDefaults.standard.removeObject(forKey: "shellHistory")
    }
}
