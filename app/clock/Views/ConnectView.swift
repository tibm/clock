import SwiftUI
import CoreBluetooth

/// Scan, connect, pairing guidance, and the clock's `info`.
struct ConnectView: View {
    @Environment(ClockLink.self) private var link

    var body: some View {
        @Bindable var link = link
        NavigationStack {
            List {
                Section("Link") {
                    LabeledContent("State") {
                        Text(link.phase.label).foregroundStyle(link.phase.color)
                    }
                    LabeledContent("Bluetooth", value: link.bluetooth.label)
                    if let name = link.connectedName { LabeledContent("Clock", value: name) }
                    if let mtu = link.maxWrite { LabeledContent("Max write", value: "\(mtu) B") }
                    if let problem = link.problem {
                        VStack(alignment: .leading, spacing: 4) {
                            Label(problem.title, systemImage: "exclamationmark.triangle.fill")
                                .foregroundStyle(.orange)
                                .font(.headline)
                            Text(problem.hint).font(.callout)
                        }
                        .padding(.vertical, 4)
                    }
                    controls
                }

                if link.phase == .scanning || !link.discovered.isEmpty {
                    Section {
                        if link.discovered.isEmpty {
                            HStack { ProgressView(); Text("Looking for clocks…").foregroundStyle(.secondary) }
                        }
                        ForEach(link.discovered.sorted { $0.rssi > $1.rssi }) { clock in
                            Button { link.connect(clock.id) } label: { DiscoveredRow(clock: clock) }
                                .disabled(link.phase != .scanning && link.phase != .idle)
                        }
                    } header: {
                        Text("Nearby clocks")
                    } footer: {
                        Text("To pair a new phone, hold the knob 10 s until the five LEDs breathe blue (“ready to pair”). A bonded phone connects any time.")
                    }
                }

                if let info = link.info { InfoSection(info: info, appProto: link.spec.protocolVersion) }

                Section("Alarm") {
                    NavigationLink {
                        AlarmView()
                    } label: {
                        LabeledContent("Alarm", value: AlarmView.summary(link.snapshot))
                    }
                    .disabled(link.phase != .ready)
                }

                Section("Wi-Fi") {
                    NavigationLink {
                        WifiView()
                    } label: {
                        LabeledContent("Wi-Fi", value: link.snapshot?.text("wifi_state") ?? "—")
                    }
                    .disabled(link.phase != .ready)
                }

                Section("Time") {
                    Toggle("Send phone time + zone on connect", isOn: $link.autoSyncTime)
                    Button("Sync time now") { Task { await link.syncTime(echo: true) } }
                        .disabled(link.phase != .ready)
                }
            }
            .navigationTitle("Clock")
            .toolbar { ToolbarItem { LinkStatusLabel() } }
        }
    }

    @ViewBuilder private var controls: some View {
        switch link.phase {
        case .idle:
            Button("Scan for clocks", systemImage: "antenna.radiowaves.left.and.right") { link.startScan() }
                .disabled(link.bluetooth != .poweredOn)
            if link.lastClockID != nil {
                Button("Reconnect last clock", systemImage: "arrow.clockwise") { link.reconnectLast() }
                    .disabled(link.bluetooth != .poweredOn)
            }
        case .scanning:
            Button("Stop scanning", systemImage: "stop.circle") { link.stopScan() }
        default:
            Button("Disconnect", systemImage: "xmark.circle", role: .destructive) { link.disconnect() }
        }
    }
}

private struct DiscoveredRow: View {
    let clock: DiscoveredClock

    var body: some View {
        HStack {
            VStack(alignment: .leading) {
                Text(clock.name).font(.body.weight(.medium))
                Text(clock.id.uuidString.prefix(8)).font(.caption.monospaced()).foregroundStyle(.secondary)
            }
            Spacer()
            switch clock.pairingOpen {
            case true?: Badge(text: "ready to pair", color: .green)
            case false?: Badge(text: "window shut")
            case nil: EmptyView()
            }
            Text("\(clock.rssi) dBm").font(.caption.monospacedDigit()).foregroundStyle(.secondary)
        }
    }
}

private struct InfoSection: View {
    let info: DeviceInfo
    let appProto: Int

    var body: some View {
        Section("Firmware") {
            switch info.compatibility(appProto: appProto) {
            case .appTooOld(let clock, let app):
                Label("Clock speaks protocol \(clock), this app \(app) — update the app.",
                      systemImage: "exclamationmark.triangle.fill").foregroundStyle(.orange)
            case .firmwareOlder(let clock, let app):
                Label("Clock firmware is older (protocol \(clock) < \(app)); newer features may answer bad-arg.",
                      systemImage: "info.circle").foregroundStyle(.secondary)
            case .match, .unknown:
                EmptyView()
            }
            ForEach(info.pairs, id: \.key) { pair in
                LabeledContent(pair.key) { Text(pair.value).font(.body.monospaced()).textSelection(.enabled) }
            }
        }
    }
}

extension CBManagerState {
    var label: String {
        switch self {
        case .poweredOn: "on"
        case .poweredOff: "off"
        case .unauthorized: "not allowed"
        case .unsupported: "unsupported"
        case .resetting: "resetting"
        case .unknown: "…"
        @unknown default: "unknown"
        }
    }
}

#Preview {
    ConnectView().environment(ClockLink.preview())
}
