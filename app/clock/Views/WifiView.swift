import SwiftUI

/// The clock's Wi-Fi (PROTOCOL.md "Wi-Fi"): pick a network from the clock's own scan, send the
/// password over the bonded link, watch it join and set its time.
struct WifiView: View {
    @Environment(ClockLink.self) private var link

    @State private var ssid = ""
    @State private var password = ""
    @State private var networks: [WifiNetwork] = []
    @State private var stored: String?
    @State private var scanning = false
    @State private var working = false
    @State private var message: (text: String, ok: Bool)?

    private var ready: Bool { link.phase == .ready }

    var body: some View {
        List {
            statusSection
            joinSection
            scanSection
            if stored != nil {
                Section {
                    Button("Forget this network", systemImage: "trash", role: .destructive) {
                        Task { await forget() }
                    }
                    .disabled(!ready || working)
                }
            }
        }
        .navigationTitle("Wi-Fi")
        .toolbar { ToolbarItem { LinkStatusLabel() } }
        .task(id: ready) {
            guard ready else { return }
            await refresh()
            if networks.isEmpty { await scan() }
        }
    }

    // MARK: Sections

    private var statusSection: some View {
        Section {
            let snap = link.snapshot
            LabeledContent("Network", value: stored.map { $0.isEmpty ? "none" : $0 } ?? "—")
            LabeledContent("State") {
                Text(snap?.text("wifi_state") ?? "—").foregroundStyle(stateColor(snap?.int("wifi_state")))
            }
            if let err = snap?.int("wifi_err"), err != 0 {
                LabeledContent("Last failure") {
                    Text(errorHint(snap?.text("wifi_err") ?? "")).foregroundStyle(.orange)
                }
            }
            if let rssi = snap?.int("wifi_rssi"), rssi != 0 {
                LabeledContent("Signal", value: "\(rssi) dBm")
            }
            LabeledContent("Time from the internet",
                           value: snap.map { $0.has("net_synced") ? "yes" : "not yet" } ?? "—")
            if let message {
                Label(message.text, systemImage: message.ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                    .foregroundStyle(message.ok ? .green : .orange)
            }
        } header: {
            Text("Status")
        } footer: {
            Text("Once on Wi-Fi the clock sets itself from internet time servers every hour; the time zone still comes from this phone.")
        }
    }

    private var joinSection: some View {
        Section("Join a network") {
            TextField("Network name (SSID)", text: $ssid)
                .textContentType(.none)
                #if os(iOS)
                .textInputAutocapitalization(.never)
                #endif
                .autocorrectionDisabled()
            SecureField("Password (empty for an open network)", text: $password)
                .textContentType(.password)
            Button {
                Task { await join() }
            } label: {
                if working { ProgressView() } else { Label("Join", systemImage: "wifi") }
            }
            .disabled(!ready || working || !joinable)
        }
    }

    private var scanSection: some View {
        Section {
            if scanning && networks.isEmpty {
                HStack { ProgressView(); Text("Scanning…").foregroundStyle(.secondary) }
            }
            ForEach(networks) { n in
                Button {
                    ssid = n.ssid
                    if !n.secured { password = "" }
                } label: {
                    HStack {
                        Image(systemName: n.secured ? "lock.fill" : "lock.open")
                            .foregroundStyle(.secondary)
                        Text(n.ssid).foregroundStyle(.primary)
                        Spacer()
                        Text("\(n.rssi) dBm").font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                    }
                }
            }
        } header: {
            HStack {
                Text("Networks the clock can see")
                Spacer()
                Button("Scan", systemImage: "arrow.clockwise") { Task { await scan() } }
                    .labelStyle(.iconOnly)
                    .disabled(!ready || scanning)
            }
        } footer: {
            Text("2.4 GHz only. Hidden networks are not listed — type the name.")
        }
    }

    // MARK: Actions

    /// SSID 1–32 bytes; password empty (open) or 8–63 characters, or 64 hex digits.
    private var joinable: Bool {
        let s = ssid.utf8.count, p = password.utf8.count
        return (1...32).contains(s) && (p == 0 || (8...64).contains(p))
    }

    private func refresh() async {
        let r = await link.send("net wifi", echo: false)
        if r.outcome.isOK { stored = r.pair("ssid") ?? "" }
    }

    private func scan() async {
        scanning = true
        defer { scanning = false }
        let r = await link.send("net wifi scan", echo: false)
        if r.outcome.isOK {
            networks = WifiNetwork.list(r.pairs)
        } else {
            message = ("Scan: \(r.lines.last ?? r.outcome.label)", false)
        }
    }

    private func join() async {
        working = true
        defer { working = false }
        var line = "net wifi join \(hexArgument(ssid))"
        if !password.isEmpty { line += " \(hexArgument(password))" }
        let r = await link.send(line, echo: false)  // never echo a password into the shell
        if r.outcome.isOK {
            message = ("Saved — the clock is joining “\(ssid)”.", true)
            password = ""
            await refresh()
        } else {
            message = (r.lines.first ?? r.outcome.label, false)
        }
    }

    private func forget() async {
        working = true
        defer { working = false }
        let r = await link.send("net wifi forget", echo: false)
        message = r.outcome.isOK ? ("Network forgotten.", true) : (r.outcome.label, false)
        await refresh()
    }

    // MARK: Presentation

    private func stateColor(_ state: Int?) -> Color {
        switch link.spec.snapshot.enums["wifi_state"].flatMap({ e in state.flatMap { $0 < e.count ? e[$0] : nil } }) {
        case "online": .green
        case "connecting": .blue
        case "backoff": .orange
        default: .secondary
        }
    }

    private func errorHint(_ err: String) -> String {
        switch err {
        case "no-ap": "network not found (2.4 GHz?)"
        case "auth": "wrong password"
        case "no-ip": "no address from the router"
        case "timeout": "no answer from the router"
        default: err
        }
    }
}

#Preview {
    NavigationStack { WifiView() }.environment(ClockLink.preview())
}
