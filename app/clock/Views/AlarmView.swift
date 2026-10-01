import SwiftUI

/// The alarm (PROTOCOL.md "Alarm schedule"): the master switch, the weekly schedule, and the
/// knob's one-off. Everything shown comes from the snapshot, so a knob edit appears live.
struct AlarmView: View {
    @Environment(ClockLink.self) private var link

    /// An edit not yet confirmed by a snapshot; nil = show the clock's week.
    @State private var draft: [AlarmSchedule.Day]?
    @State private var once = AlarmView.date(7 * 60)
    @State private var daily = AlarmView.date(7 * 60)
    @State private var working = false
    @State private var message: (text: String, ok: Bool)?

    private var ready: Bool { link.phase == .ready }

    var body: some View {
        Group {
            if let snap = link.snapshot {
                let alarm = AlarmSchedule(snap)
                List {
                    nextSection(alarm, snap)
                    if let week = draft ?? alarm.week {
                        weekSection(week)
                        onceSection
                    } else {
                        dailySection(alarm)
                    }
                }
                // Pickers edit a minute of day: carry it as a GMT time so no zone shifts it.
                .environment(\.timeZone, .gmt)
                .onChange(of: alarm.week) { _, week in
                    if week == draft { draft = nil }
                }
            } else {
                ContentUnavailableView("No status yet", systemImage: "alarm",
                                       description: Text("Connect to a clock on the Clock tab."))
            }
        }
        .navigationTitle("Alarm")
        .toolbar { ToolbarItem { LinkStatusLabel() } }
        .task(id: draft) { await sendDraft() }
    }

    // MARK: Sections

    private func nextSection(_ a: AlarmSchedule, _ snap: Snapshot) -> some View {
        Section {
            Toggle("Armed", isOn: Binding(get: { a.armed }, set: { on in
                run("chrono alarm arm \(on ? "on" : "off")")
            }))
            .disabled(!ready || working)
            LabeledContent("Next alarm") {
                Text(Self.nextText(a)).font(.body.monospacedDigit())
                    .foregroundStyle(a.armed ? .primary : .secondary)
            }
            if snap.text("ui_mode") == "alarm" {
                Label("Being set on the knob right now", systemImage: "dial.medium")
                    .foregroundStyle(.blue)
            }
            if a.hasOverride {
                VStack(alignment: .leading, spacing: 6) {
                    Label("Changed on the clock, just once", systemImage: "hand.point.up.left")
                        .font(.headline)
                    if let r = a.replaced {
                        Text("Replaces the schedule's \(r.on ? AlarmSchedule.hhmm(r.minute) : "day off") for that day; the schedule is unchanged.")
                            .font(.callout).foregroundStyle(.secondary)
                    }
                    Button("Cancel the one-off", systemImage: "xmark.circle", role: .destructive) {
                        run("chrono alarm next clear")
                    }
                    .disabled(!ready || working)
                }
                .padding(.vertical, 4)
            }
            if a.week != nil, !snap.has("date_valid") {
                Label("The clock has no date: only an every-day schedule (or a one-off) can ring. Sync the time from the Clock tab.",
                      systemImage: "calendar.badge.exclamationmark")
                    .font(.callout).foregroundStyle(.orange)
            }
            if let message {
                Label(message.text, systemImage: message.ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                    .foregroundStyle(message.ok ? .green : .orange)
            }
        } footer: {
            if !a.armed { Text("Disarmed: nothing rings. The schedule and any one-off are kept.") }
        }
    }

    private func weekSection(_ week: [AlarmSchedule.Day]) -> some View {
        Section {
            ForEach(week.indices, id: \.self) { d in
                HStack {
                    Text(Self.dayName(d, short: false))
                        .foregroundStyle(week[d].on ? .primary : .secondary)
                    Spacer()
                    DatePicker("Time", selection: Binding(
                        get: { Self.date(week[d].minute) },
                        set: { t in edit(week, d) { $0.minute = Self.minute(t) } }
                    ), displayedComponents: .hourAndMinute)
                    .labelsHidden()
                    .opacity(week[d].on ? 1 : 0.5)
                    Toggle("On", isOn: Binding(
                        get: { week[d].on },
                        set: { on in edit(week, d) { $0.on = on } }
                    ))
                    .labelsHidden()
                }
                .disabled(!ready)
            }
        } header: {
            HStack {
                Text("Every week")
                Spacer()
                if draft != nil { ProgressView().controlSize(.small) }
            }
        } footer: {
            Text("Set here, kept on the clock. An off day keeps its time. Turning the knob on the clock changes only the next alarm.")
        }
    }

    private var onceSection: some View {
        Section {
            DatePicker("Time", selection: $once, displayedComponents: .hourAndMinute)
            Button("Ring once at \(AlarmSchedule.hhmm(Self.minute(once)))", systemImage: "1.circle") {
                run("chrono alarm next \(AlarmSchedule.hhmm(Self.minute(once)))")
            }
            .disabled(!ready || working)
        } header: {
            Text("Just once")
        } footer: {
            Text("The next time the clock reaches this time it rings, instead of that day's scheduled alarm — like turning the knob.")
        }
    }

    /// Firmware without the schedule (132-byte snapshot): one alarm, every day.
    private func dailySection(_ a: AlarmSchedule) -> some View {
        Section {
            DatePicker("Time", selection: $daily, displayedComponents: .hourAndMinute)
            Button("Set the daily alarm", systemImage: "alarm") {
                run("chrono alarm set \(AlarmSchedule.hhmm(Self.minute(daily)))")
            }
            .disabled(!ready || working)
        } header: {
            Text("Every day")
        } footer: {
            Text("This clock's firmware has a single daily alarm — update it for a weekly schedule.")
        }
        .task(id: a.nextMinute) { if let m = a.nextMinute { daily = Self.date(m) } }
    }

    // MARK: Actions

    private func edit(_ week: [AlarmSchedule.Day], _ d: Int, _ change: (inout AlarmSchedule.Day) -> Void) {
        var w = week
        change(&w[d])
        if w != week { draft = w }
    }

    /// Debounced: a picker spinning sends one command, the whole week (PROTOCOL.md).
    private func sendDraft() async {
        guard let week = draft else { return }
        try? await Task.sleep(for: .milliseconds(700))
        guard !Task.isCancelled else { return }
        let r = await link.send(AlarmSchedule.weekCommand(week))
        guard !Task.isCancelled else { return }
        if r.outcome.isOK {
            message = nil
            link.readStatus()
            // The snapshot clears the draft when it shows the week; give up after a few seconds.
            try? await Task.sleep(for: .seconds(4))
        } else {
            message = ("Schedule: \(r.lines.first ?? r.outcome.label)", false)
        }
        if !Task.isCancelled { draft = nil }
    }

    private func run(_ line: String) {
        Task {
            working = true
            defer { working = false }
            let r = await link.send(line)
            message = r.outcome.isOK ? nil : ("\(line): \(r.lines.first ?? r.outcome.label)", false)
            link.readStatus()
        }
    }

    // MARK: Presentation

    /// Minute of day ↔ a GMT `Date` for the pickers.
    static func date(_ minute: Int) -> Date { Date(timeIntervalSince1970: Double(minute * 60)) }
    static func minute(_ date: Date) -> Int {
        let s = Int(date.timeIntervalSince1970.rounded(.down)) % 86_400
        return (s < 0 ? s + 86_400 : s) / 60
    }

    /// Protocol weekday (0 = Monday) → localized name.
    static func dayName(_ d: Int, short: Bool = true) -> String {
        let cal = Calendar.current
        let names = short ? cal.shortWeekdaySymbols : cal.weekdaySymbols  // Sunday first
        return names.count == 7 ? names[(d + 1) % 7] : "day \(d)"
    }

    static func nextText(_ a: AlarmSchedule) -> String {
        if a.next == "none" { return "none — every day is off" }
        guard let m = a.nextMinute else { return "—" }
        let day = a.nextWeekday.map { Self.dayName($0) + " " } ?? ""
        return day + AlarmSchedule.hhmm(m) + (a.hasOverride ? " (once)" : "")
    }

    /// One line for the Clock tab.
    static func summary(_ snap: Snapshot?) -> String {
        guard let snap else { return "—" }
        let a = AlarmSchedule(snap)
        return a.armed ? nextText(a) : "off"
    }
}

#Preview {
    NavigationStack { AlarmView() }.environment(ClockLink.preview())
}
