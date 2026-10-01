import Foundation

/// The clock's alarm as the snapshot carries it (PROTOCOL.md "Alarm schedule"): a weekly
/// schedule (a minute of day per weekday, each on/off, Monday first) and what rings next.
///
/// Read only from the snapshot — never from a command reply. A record without `alarm_week`
/// (older firmware, 132 B) has a single daily alarm: `week` is nil.
nonisolated struct AlarmSchedule: Sendable, Equatable {
    /// One weekday of the schedule. A day that is off keeps its time.
    struct Day: Sendable, Equatable {
        var on: Bool
        var minute: Int
    }

    /// Seven days, Monday first; nil on a firmware without the schedule.
    var week: [Day]?
    /// `alarm_next` enum label (`none` / `schedule` / `override` / `unknown(n)`); nil without the schedule.
    var next: String?
    /// The next alarm's minute of day (`alarm_h:alarm_m`).
    var nextMinute: Int?
    /// The next alarm's weekday, 0 = Monday; nil = none / no date (sentinel).
    var nextWeekday: Int?
    var armed: Bool

    init(_ snap: Snapshot) {
        armed = snap.has("alarm_armed")
        if let h = snap.int("alarm_h"), let m = snap.int("alarm_m") { nextMinute = h * 60 + m }
        if let minutes = snap.ints("alarm_week"), let days = snap.int("alarm_days") {
            week = minutes.enumerated().map { Day(on: days & (1 << $0.offset) != 0, minute: $0.element) }
        }
        next = snap["alarm_next"]?.enumLabel
        if let f = snap["alarm_next_wday"], f.sentinel == nil, let d = f.raw.integer { nextWeekday = Int(d) }
    }

    init(week: [Day]?, next: String? = nil, nextMinute: Int? = nil, nextWeekday: Int? = nil, armed: Bool = false) {
        self.week = week
        self.next = next
        self.nextMinute = nextMinute
        self.nextWeekday = nextWeekday
        self.armed = armed
    }

    /// A one-off (knob or `chrono alarm next`) is pending.
    var hasOverride: Bool { next == "override" }

    /// What the one-off replaced: the schedule's day it lands on, when known.
    var replaced: Day? {
        guard let week, let d = nextWeekday, week.indices.contains(d) else { return nil }
        return week[d]
    }

    // MARK: Command lines

    /// `chrono alarm week …`: the whole week, an off day as `-hh:mm` so its time is kept.
    static func weekCommand(_ week: [Day]) -> String {
        "chrono alarm week " + week.map { ($0.on ? "" : "-") + hhmm($0.minute) }.joined(separator: " ")
    }

    /// `hh:mm`, 24 h.
    static func hhmm(_ minute: Int) -> String {
        let m = ((minute % 1440) + 1440) % 1440
        return String(format: "%02d:%02d", m / 60, m % 60)
    }
}
