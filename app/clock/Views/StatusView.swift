import SwiftUI

/// Live view of the `status` snapshot and `info`.
///
/// Field groups below are a layout hint only: every value is looked up by name in the decoded
/// snapshot, missing names are skipped, and any field not listed lands in "Other fields" —
/// so a new field in `protocol.json` shows up without touching this file.
struct StatusView: View {
    @Environment(ClockLink.self) private var link

    private static let groups: [(title: String, fields: [String])] = [
        ("System", ["uptime_s", "reset_reason", "clk_src", "fw_id", "heap_free", "heap_min"]),
        ("Power", ["vbat_mv", "soc_pct", "vbat_src"]),
        ("Room", ["temp", "rh", "pressure", "gas_ohms", "env_age_s"]),
        ("Light", ["lux", "als_age_s"]),
        ("Motion sensor", ["grav_x", "grav_y", "grav_z", "yaw", "pitch", "roll", "taps"]),
        ("Hands", ["motion_state", "dial_tick", "hand_h", "hand_m", "target_h", "target_m",
                   "opto", "motion_faults", "trims", "last_trim"]),
        ("UI", ["ui_mode", "volume", "brightness", "wake_warm", "wake_cool", "knob_count"]),
        ("Alarm", ["alarm_next", "alarm_h", "alarm_m", "alarm_next_wday", "alarm_days", "alarm_week"]),
        ("Radio", ["ble_state", "bonds", "wifi_state", "wifi_rssi", "wifi_err"]),
    ]
    /// Shown elsewhere (header, time, LEDs, flags) or meaningless on screen. `reserved*` too.
    private static let hidden: Set<String> = ["schema", "size", "seq", "flags", "pixels", "epoch_ms", "tz_off_min"]

    private static let periods: [(label: String, ms: Int)] = [
        ("0.5 s", 500), ("1 s", 1000), ("5 s", 5000), ("60 s", 60000),
    ]

    var body: some View {
        NavigationStack {
            Group {
                if let snap = link.snapshot {
                    content(snap)
                } else {
                    ContentUnavailableView {
                        Label("No status yet", systemImage: "waveform.path.ecg")
                    } description: {
                        Text(link.snapshotError ?? (link.phase == .ready ? "Waiting for the first snapshot…" : "Connect to a clock on the Clock tab."))
                    }
                }
            }
            .navigationTitle("Status")
            .toolbar {
                ToolbarItem { LinkStatusLabel() }
                ToolbarItem {
                    Menu("Period", systemImage: "timer") {
                        ForEach(Self.periods, id: \.ms) { p in
                            Button(p.label) { Task { await link.send("net ble period \(p.ms)") } }
                        }
                    }
                    .disabled(link.phase != .ready)
                }
                ToolbarItem {
                    Button("Read", systemImage: "arrow.clockwise") { link.readStatus() }
                        .disabled(link.phase != .ready)
                }
            }
        }
    }

    private func content(_ snap: Snapshot) -> some View {
        let listed = Set(Self.groups.flatMap(\.fields)).union(Self.hidden)
        let others = snap.fields.filter { !listed.contains($0.name) && !$0.name.hasPrefix("reserved") }
        return List {
            Section {
                HeaderRow(snap: snap)
                if let error = link.snapshotError {
                    Label(error, systemImage: "exclamationmark.triangle").foregroundStyle(.orange)
                }
            }
            Section("Time") { TimeRows(snap: snap) }
            Section("Flags") { FlagGrid(snap: snap, names: link.spec.snapshot.flags) }
            if !snap.pixels.isEmpty {
                Section("LEDs") { PixelRow(pixels: snap.pixels) }
            }
            ForEach(Self.groups, id: \.title) { group in
                let fields = group.fields.compactMap { snap[$0] }
                if !fields.isEmpty {
                    Section(group.title) { ForEach(fields) { FieldRow(field: $0) } }
                }
            }
            if !others.isEmpty {
                Section("Other fields") { ForEach(others) { FieldRow(field: $0) } }
            }
            Section("Raw (\(snap.raw.count) B)") {
                Text(snap.raw.map { String(format: "%02x", $0) }.joined())
                    .font(.caption2.monospaced())
                    .textSelection(.enabled)
            }
        }
    }
}

private struct HeaderRow: View {
    @Environment(ClockLink.self) private var link
    let snap: Snapshot

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1)) { ctx in
            HStack {
                VStack(alignment: .leading) {
                    Text("seq \(snap.text("seq"))").font(.headline.monospacedDigit())
                    Text("\(link.snapshotCount) received · \(link.missedSnapshots) missed")
                        .font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
                let age = max(0, ctx.date.timeIntervalSince(snap.receivedAt))
                Text(age < 1 ? "now" : "\(Int(age)) s ago")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(age > 5 ? .orange : .secondary)
            }
        }
    }
}

private struct TimeRows: View {
    let snap: Snapshot

    var body: some View {
        if let t = snap.localTime {
            LabeledContent("Clock time") {
                Text(Self.format(t.date, withDate: t.hasDate)).font(.body.monospacedDigit())
            }
        } else {
            LabeledContent("Clock time", value: "— (not set since boot)")
        }
        if let off = snap["tz_off_min"] {
            LabeledContent("UTC offset", value: off.isValid ? Self.offset(off.raw.integer ?? 0) : "— (never set)")
        }
        LabeledContent("Phone time") {
            TimelineView(.periodic(from: .now, by: 1)) { ctx in
                Text(ctx.date.formatted(date: .omitted, time: .standard)).font(.body.monospacedDigit())
            }
        }
    }

    /// Local time is carried as "UTC" per PROTOCOL.md §5.
    static func format(_ d: Date, withDate: Bool) -> String {
        var style = Date.ISO8601FormatStyle(timeZone: .gmt)
        style = withDate ? style.year().month().day().time(includingFractionalSeconds: false)
                         : style.time(includingFractionalSeconds: false)
        return d.formatted(style).replacingOccurrences(of: "T", with: "  ").replacingOccurrences(of: "Z", with: "")
    }

    static func offset(_ min: Int64) -> String {
        let sign = min < 0 ? "−" : "+"
        return String(format: "UTC%@%02d:%02d", sign, abs(min) / 60, abs(min) % 60)
    }
}

private struct FieldRow: View {
    let field: DecodedField

    var body: some View {
        LabeledContent {
            Text(value)
                .font(.body.monospacedDigit())
                .foregroundStyle(field.isValid ? .primary : .secondary)
                .textSelection(.enabled)
        } label: {
            Text(field.name).font(.callout.monospaced())
            if let note = field.spec.note { Text(note).font(.caption2) }
        }
    }

    private var value: String {
        switch field.name {
        case "fw_id": field.raw.integer.map { String(format: "%08x", $0) } ?? "—"
        case "uptime_s": field.raw.integer.map { Duration.seconds($0).formatted(.time(pattern: .hourMinuteSecond)) } ?? "—"
        case "motion_faults": field.raw.integer.map { String(format: "0x%08x", $0) } ?? "—"
        // Alarm schedule: weekday 0 = Monday, minutes of day.
        case "alarm_week" where field.isValid:
            field.raw.numbers.map { m in
                m.enumerated().map { "\(AlarmView.dayName($0.offset)) \(AlarmSchedule.hhmm(Int($0.element)))" }
                    .joined(separator: ", ")
            } ?? field.display
        case "alarm_days" where field.isValid:
            field.raw.integer.map { bits in
                let on = (0..<7).filter { bits & (1 << $0) != 0 }.map { AlarmView.dayName($0) }
                return on.isEmpty ? "none" : on.joined(separator: " ")
            } ?? field.display
        case "alarm_next_wday" where field.isValid && field.sentinel == nil:
            field.raw.integer.map { AlarmView.dayName(Int($0)) } ?? field.display
        default: field.display
        }
    }
}

private struct FlagGrid: View {
    let snap: Snapshot
    let names: [String]

    var body: some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 120), alignment: .leading)], alignment: .leading, spacing: 6) {
            ForEach(Array(names.enumerated()), id: \.offset) { _, name in
                let on = snap.has(name)
                Text(name)
                    .font(.caption.monospaced())
                    .padding(.horizontal, 6).padding(.vertical, 3)
                    .foregroundStyle(on ? Color.white : Color.secondary)
                    .background(on ? Color.accentColor : Color.secondary.opacity(0.12), in: RoundedRectangle(cornerRadius: 5))
            }
            ForEach(snap.unknownFlagBits, id: \.self) { bit in
                Text("bit \(bit)")
                    .font(.caption.monospaced())
                    .padding(.horizontal, 6).padding(.vertical, 3)
                    .background(Color.purple.opacity(0.3), in: RoundedRectangle(cornerRadius: 5))
            }
        }
        .padding(.vertical, 4)
    }
}

private struct PixelRow: View {
    let pixels: [[UInt8]]
    /// Chain order per protocol.json `pixels` note.
    private static let names = ["dial0", "dial1", "bell", "alarm", "clock", "vol", "batt"]

    var body: some View {
        HStack(spacing: 10) {
            ForEach(Array(pixels.enumerated()), id: \.offset) { i, p in
                VStack(spacing: 4) {
                    Circle()
                        .fill(color(p))
                        .overlay(Circle().stroke(.secondary.opacity(0.4)))
                        .frame(width: 26, height: 26)
                    Text(i < Self.names.count ? Self.names[i] : "\(i)").font(.caption2)
                }
                .help(p.map(String.init).joined(separator: ","))
            }
        }
    }

    /// RGBW → display colour: W adds equally to R, G, B. Black stays dark grey so it's visible.
    private func color(_ p: [UInt8]) -> Color {
        guard p.count == 4 else { return .clear }
        let w = Double(p[3])
        let c = { (v: UInt8) in min(1, (Double(v) + w) / 255) }
        if p.allSatisfy({ $0 == 0 }) { return Color.black.opacity(0.8) }
        return Color(red: c(p[0]), green: c(p[1]), blue: c(p[2]))
    }
}

#Preview {
    StatusView().environment(ClockLink.preview())
}
