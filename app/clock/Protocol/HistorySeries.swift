import Foundation

/// One plotted point: a sample, or a bucket of them (min / mean / max).
nonisolated struct HistoryPoint: Sendable, Identifiable, Equatable {
    var id: Date { t }
    let t: Date
    let mean: Double
    let min: Double
    let max: Double
    /// Points with the same segment are joined; a new segment starts after a gap (PROTOCOL.md:
    /// don't join points across more than two periods).
    var segment = 0
}

/// Downsampling for the charts: few enough points for Swift Charts, whatever the range.
nonisolated enum HistorySeries {
    /// Bucket widths, seconds. All divide a day, so buckets never straddle two day files.
    static let bucketWidths = [300, 900, 3600, 6 * 3600, 86400]
    static let maxPoints = 1000

    /// Bucket width for `span` seconds holding about `samples` records; 0 = plot every sample.
    static func bucket(span: TimeInterval, samples: Int) -> Int {
        if samples <= maxPoints { return 0 }
        return bucketWidths.first { span / Double($0) <= Double(maxPoints) } ?? bucketWidths.last!
    }

    /// Points for one column of `samples` (time-ordered or not), invalid values left out.
    static func points(_ samples: [HistorySample], column: Int, bucket: Int,
                       in range: ClosedRange<Date>? = nil) -> [HistoryPoint] {
        var out: [HistoryPoint] = []
        if bucket <= 0 {
            for s in samples {
                guard range?.contains(s.t) ?? true, let v = s.value(column) else { continue }
                out.append(HistoryPoint(t: s.t, mean: v, min: v, max: v))
            }
            return out.sorted { $0.t < $1.t }
        }
        var acc: [Int: (sum: Double, n: Int, lo: Double, hi: Double)] = [:]
        for s in samples {
            guard range?.contains(s.t) ?? true, let v = s.value(column) else { continue }
            let key = Int(s.t.timeIntervalSince1970) / bucket * bucket
            let a = acc[key] ?? (0, 0, .infinity, -.infinity)
            acc[key] = (a.sum + v, a.n + 1, Swift.min(a.lo, v), Swift.max(a.hi, v))
        }
        return acc.keys.sorted().map { k in
            let a = acc[k]!
            return HistoryPoint(t: Date(timeIntervalSince1970: Double(k)), mean: a.sum / Double(a.n), min: a.lo, max: a.hi)
        }
    }

    /// Numbers the segments: a new one wherever two points are more than `maxGap` seconds apart.
    static func segmented(_ points: [HistoryPoint], maxGap: TimeInterval) -> [HistoryPoint] {
        var seg = 0
        var prev: Date?
        return points.map { p in
            if let prev, p.t.timeIntervalSince(prev) > maxGap { seg += 1 }
            prev = p.t
            var p = p
            p.segment = seg
            return p
        }
    }

    /// `yyyymmdd` of a UTC day, and back.
    static func dayString(_ date: Date) -> String {
        let c = utc.dateComponents([.year, .month, .day], from: date)
        return String(format: "%04d%02d%02d", c.year ?? 0, c.month ?? 0, c.day ?? 0)
    }

    static func dayStart(_ day: String) -> Date? {
        guard day.count == 8, let n = Int(day) else { return nil }
        return utc.date(from: DateComponents(year: n / 10000, month: n / 100 % 100, day: n % 100))
    }

    private static let utc: Calendar = {
        var c = Calendar(identifier: .gregorian)
        c.timeZone = TimeZone(identifier: "UTC")!
        return c
    }()
}
