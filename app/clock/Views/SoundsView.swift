import SwiftUI

/// The clock's sound files (`/sd/tones`) and the WAVs bundled with the app (PROTOCOL.md "Sound files").
struct SoundsView: View {
    @Environment(ClockLink.self) private var link
    @Environment(ToneStore.self) private var store

    var body: some View {
        NavigationStack {
            Group {
                if link.phase != .ready {
                    ContentUnavailableView("Not connected", systemImage: "speaker.slash",
                                           description: Text("Connect to a clock on the Clock tab."))
                } else if !store.supported {
                    ContentUnavailableView("No sound files", systemImage: "speaker.slash",
                                           description: Text("This clock's firmware has no `blob` characteristic."))
                } else if store.noCard {
                    ContentUnavailableView("No microSD card", systemImage: "sdcard",
                                           description: Text("Insert a card in the clock, then refresh."))
                } else {
                    content
                }
            }
            .navigationTitle("Sounds")
            .toolbar {
                ToolbarItem { LinkStatusLabel() }
                ToolbarItem {
                    Button("Refresh", systemImage: "arrow.clockwise") { Task { await store.refresh() } }
                        .disabled(link.phase != .ready || store.loading || store.upload != nil)
                }
            }
            .task(id: link.phase == .ready && store.supported) {
                if link.phase == .ready && store.supported { await store.refresh() }
            }
        }
    }

    private var content: some View {
        List {
            if let upload = store.upload { UploadSection(upload: upload) }
            if let m = store.message {
                Label(m.text, systemImage: m.ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                    .foregroundStyle(m.ok ? .green : .orange)
            }

            Section {
                if let list = store.list {
                    if list.tones.isEmpty {
                        Text("No sound files on the card.").foregroundStyle(.secondary)
                    }
                    ForEach(list.tones) { tone in
                        ClockToneRow(tone: tone, isAlarm: list.alarm == tone.name)
                    }
                    .onDelete { rows in
                        let names = rows.map { list.tones[$0].name }
                        Task { for n in names { await store.delete(n) } }
                    }
                    .deleteDisabled(store.upload != nil)
                } else if let err = store.listError {
                    Text("storage tones: \(err)").foregroundStyle(.red)
                } else {
                    ProgressView()
                }
            } header: {
                Text("On the clock")
            } footer: {
                if let list = store.list {
                    VStack(alignment: .leading) {
                        Text("Alarm: \(list.alarm ?? "the built-in beep")")
                        if let total = list.cardTotal, let free = list.cardFree {
                            Text("Card: \(bytes(free)) free of \(bytes(total))")
                        }
                    }
                }
            }

            Section {
                if store.bundled.isEmpty {
                    Text("No .wav files in the app. Drop them into `app/clock/Tones/` and rebuild.")
                        .foregroundStyle(.secondary)
                }
                ForEach(store.bundled) { tone in
                    BundledToneRow(tone: tone, onClock: store.isOnClock(tone))
                }
            } header: {
                Text("In the app")
            } footer: {
                if let f = link.spec.soundFiles?.format {
                    Text("Only \(f.container.uppercased()) PCM \(f.sampleRate) Hz, \(f.channels == 1 ? "mono" : "\(f.channels) ch"), \(f.bits)-bit plays. Keep the app open while uploading — a dropped link resumes on the next upload.")
                }
            }
        }
    }
}

private struct UploadSection: View {
    @Environment(ToneStore.self) private var store
    let upload: ToneStore.Upload

    var body: some View {
        Section("Uploading \(upload.name)") {
            ProgressView(value: upload.fraction) {
                Text("\(upload.phase) · \(bytes(upload.sent)) / \(bytes(upload.total))")
            } currentValueLabel: {
                if upload.rate > 0 {
                    let left = Double(upload.total - upload.sent) / upload.rate
                    Text("\(bytes(Int(upload.rate)))/s · \(Int(left)) s left")
                }
            }
            Button("Cancel upload", systemImage: "xmark.circle", role: .destructive) { store.cancelUpload() }
        }
    }
}

private struct ClockToneRow: View {
    @Environment(ToneStore.self) private var store
    let tone: ToneList.Tone
    let isAlarm: Bool

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(tone.name)
                Text("\(bytes(tone.size)) · \(String(format: "%.1f", Double(tone.durationMs) / 1000)) s")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            if isAlarm { Badge(text: "alarm", color: .accentColor) }
            if !tone.playable { Badge(text: tone.state, color: .orange) }
        }
        .contextMenu {
            if tone.playable {
                Button("Play on clock", systemImage: "play") { Task { await store.play(tone.name) } }
                Button("Stop", systemImage: "stop") { Task { await store.stop() } }
                if isAlarm {
                    Button("Use the beep for the alarm", systemImage: "bell.slash") { Task { await store.setAlarm(nil) } }
                } else {
                    Button("Use for the alarm", systemImage: "alarm") { Task { await store.setAlarm(tone.name) } }
                }
            }
            Button("Delete", systemImage: "trash", role: .destructive) { Task { await store.delete(tone.name) } }
                .disabled(store.upload != nil)
        }
    }
}

private struct BundledToneRow: View {
    @Environment(ToneStore.self) private var store
    let tone: BundledTone
    let onClock: Bool

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(tone.name)
                Group {
                    if let problem = tone.problem {
                        Text(problem).foregroundStyle(.orange)
                    } else {
                        Text("\(bytes(tone.size))" + (tone.header?.durationMs.map { String(format: " · %.1f s", Double($0) / 1000) } ?? ""))
                    }
                }
                .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            if onClock { Badge(text: "on clock", color: .green) }
            Button(onClock ? "Replace" : "Upload", systemImage: "square.and.arrow.up") { store.startUpload(tone) }
                .labelStyle(.iconOnly)
                .buttonStyle(.borderless)
                .disabled(tone.problem != nil || store.upload != nil)
        }
    }
}

private func bytes(_ n: some BinaryInteger) -> String {
    Int64(n).formatted(.byteCount(style: .file))
}

#Preview {
    let store = ToneStore.preview()
    SoundsView().environment(store.link).environment(store)
}
