import Foundation

/// A `TimeZone` as the POSIX TZ rule the clock takes (PROTOCOL.md "Keeping time"), e.g.
/// `America/Los_Angeles` → `<-08>8<-07>,M3.2.0,M11.1.0`.
///
/// iOS has no POSIX string for a zone, so it is rebuilt from the zone's behaviour: the standard
/// and daylight offsets, and the next two DST transitions, each written as "the w-th weekday d
/// of month m, at the local time just before the switch". Names are always the numeric `<±hh>`
/// form — the clock only needs them to parse, and the IANA identifier is sent as the label.
nonisolated enum PosixTimeZone {
    static func rule(for tz: TimeZone, at now: Date = .now) -> String {
        guard let t1 = tz.nextDaylightSavingTimeTransition(after: now),
              let t2 = tz.nextDaylightSavingTimeTransition(after: t1) else {
            let off = tz.secondsFromGMT(for: now)
            return name(off) + offset(off)
        }
        // Label each transition by what it switches INTO.
        let (start, end) = tz.isDaylightSavingTime(for: t1) ? (t1, t2) : (t2, t1)
        let stdOff = tz.secondsFromGMT(for: end)
        let dstOff = tz.secondsFromGMT(for: start)
        var s = name(stdOff) + offset(stdOff) + name(dstOff)
        if dstOff != stdOff + 3600 { s += offset(dstOff) }
        // The start is written in standard time and the end in daylight time (POSIX).
        s += "," + when(start, localOffset: stdOff) + "," + when(end, localOffset: dstOff)
        return s
    }

    /// `<+0530>`, `<-08>`: the tzdata spelling for a zone with no letters of its own.
    static func name(_ off: Int) -> String {
        let a = abs(off), h = a / 3600, m = (a % 3600) / 60
        let sign = off < 0 ? "-" : "+"
        return m == 0 ? String(format: "<%@%02d>", sign, h) : String(format: "<%@%02d%02d>", sign, h, m)
    }

    /// POSIX offset: west of Greenwich is POSITIVE. `hh[:mm[:ss]]`.
    static func offset(_ off: Int) -> String {
        let a = abs(off)
        var s = (off > 0 ? "-" : "") + String(a / 3600)
        if a % 3600 != 0 {
            s += String(format: ":%02d", (a % 3600) / 60)
            if a % 60 != 0 { s += String(format: ":%02d", a % 60) }
        }
        return s
    }

    /// `Mm.w.d[/time]` for a transition instant, in the wall time in force just before it.
    static func when(_ instant: Date, localOffset: Int) -> String {
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = TimeZone(secondsFromGMT: 0)!
        let local = instant.addingTimeInterval(TimeInterval(localOffset))
        let c = cal.dateComponents([.month, .day, .weekday, .hour, .minute, .second], from: local)
        let day = c.day!, month = c.month!
        let days = cal.range(of: .day, in: .month, for: local)!.count
        let week = day + 7 > days ? 5 : (day - 1) / 7 + 1
        var s = "M\(month).\(week).\(c.weekday! - 1)"
        let secs = c.hour! * 3600 + c.minute! * 60 + c.second!
        if secs != 7200 {
            s += "/\(secs / 3600)"
            if secs % 3600 != 0 { s += String(format: ":%02d", (secs % 3600) / 60) }
            if secs % 60 != 0 { s += String(format: ":%02d", secs % 60) }
        }
        return s
    }
}

/// `hex:` + the UTF-8 bytes in hex — how Wi-Fi names and passwords travel (PROTOCOL.md "Wi-Fi"),
/// so spaces and quotes need no escaping.
nonisolated func hexArgument(_ s: String) -> String {
    "hex:" + s.utf8.map { String(format: "%02x", $0) }.joined()
}

/// One `=ap=<rssi>/<open|secured>/<channel>/<ssid>` pair from `net wifi scan`.
nonisolated struct WifiNetwork: Identifiable, Sendable, Equatable {
    var id: String { ssid }
    let ssid: String
    let rssi: Int
    let secured: Bool
    let channel: Int

    /// The SSID is last and may itself contain `/`: split on the first three.
    init?(pair value: String) {
        let parts = value.split(separator: "/", maxSplits: 3, omittingEmptySubsequences: false)
        guard parts.count == 4, let rssi = Int(parts[0]), let ch = Int(parts[2]), !parts[3].isEmpty else { return nil }
        self.rssi = rssi
        self.secured = parts[1] != "open"
        self.channel = ch
        self.ssid = String(parts[3])
    }

    /// Every `ap` pair, strongest first, one row per SSID (mesh networks repeat it).
    static func list(_ pairs: [(key: String, value: String)]) -> [WifiNetwork] {
        var seen = Set<String>()
        return pairs.filter { $0.key == "ap" }
            .compactMap { WifiNetwork(pair: $0.value) }
            .sorted { $0.rssi > $1.rssi }
            .filter { seen.insert($0.ssid).inserted }
    }
}
