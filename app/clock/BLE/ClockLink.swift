import Foundation
import CoreBluetooth
import Observation

/// The BLE link to one clock (PROTOCOL.md §1–§3, §7).
///
/// Owns the central, the connection, the `cmd`/`rsp` request queue, the latest `status`
/// snapshot and `info`. Every UUID and limit comes from `ProtocolSpec`. All CoreBluetooth
/// callbacks arrive on the main queue.
@Observable
final class ClockLink: NSObject {
    let spec: ProtocolSpec

    // MARK: Observable state

    private(set) var bluetooth: CBManagerState = .unknown
    private(set) var phase: LinkPhase = .idle
    private(set) var problem: LinkProblem?
    private(set) var discovered: [DiscoveredClock] = []
    private(set) var connectedName: String?

    private(set) var info: DeviceInfo?
    private(set) var snapshot: Snapshot?
    private(set) var snapshotError: String?
    /// Records missed since connecting (from `seq` gaps).
    private(set) var missedSnapshots = 0
    private(set) var snapshotCount = 0

    private(set) var catalog: CommandCatalog
    private(set) var transcript: [ShellLine] = []
    /// Show every `rsp` frame as received, before reassembly.
    var showRawFrames = false
    /// Largest single `cmd` write (`maximumWriteValueLength(.withResponse)`).
    private(set) var maxWrite: Int?
    /// True while a request is in flight.
    var isBusy: Bool { inFlight != nil }

    /// Send `chrono time epoch` on every connect and on timezone changes (PROTOCOL.md "Keeping time").
    var autoSyncTime: Bool {
        get { access(keyPath: \.autoSyncTime); return defaults.object(forKey: Keys.autoSyncTime) as? Bool ?? true }
        set { withMutation(keyPath: \.autoSyncTime) { defaults.set(newValue, forKey: Keys.autoSyncTime) } }
    }

    // MARK: Private

    private enum Keys {
        static let lastClock = "lastClockID"
        static let autoSyncTime = "autoSyncTime"
    }

    @ObservationIgnored private let defaults = UserDefaults.standard
    @ObservationIgnored private var central: CBCentralManager!
    @ObservationIgnored private var peripheral: CBPeripheral?
    @ObservationIgnored private var peripherals: [UUID: CBPeripheral] = [:]
    @ObservationIgnored private var chars: [String: CBCharacteristic] = [:]
    @ObservationIgnored private var framer = ResponseFramer()
    @ObservationIgnored private let decoder: SnapshotDecoder
    @ObservationIgnored private var userDisconnect = false
    @ObservationIgnored private var securingRetries = 0
    @ObservationIgnored private var wantScan = false
    @ObservationIgnored private var tzObserver: Task<Void, Never>?

    private struct Pending {
        var result: CommandResult
        let echo: Bool
        var busyRetries = 0
        let continuation: CheckedContinuation<CommandResult, Never>
    }
    @ObservationIgnored private var queue: [Pending] = []
    private var inFlight: Pending?
    @ObservationIgnored private var timeoutTask: Task<Void, Never>?
    @ObservationIgnored private var nextID: UInt16 = 1

    private static let transcriptLimit = 3000

    init(spec: ProtocolSpec, startCentral: Bool = true) {
        self.spec = spec
        self.decoder = SnapshotDecoder(spec: spec)
        self.catalog = CommandCatalog(spec: spec)
        super.init()
        if startCentral {
            central = CBCentralManager(delegate: self, queue: .main)
        }
        tzObserver = Task { [weak self] in
            for await _ in NotificationCenter.default.notifications(named: .NSSystemTimeZoneDidChange) {
                guard let self, self.phase == .ready, self.autoSyncTime else { continue }
                self.note("timezone changed, resending time")
                _ = await self.syncTime()
            }
        }
    }

    // MARK: Scanning and connecting

    var lastClockID: UUID? { defaults.string(forKey: Keys.lastClock).flatMap(UUID.init) }

    func startScan() {
        wantScan = true
        guard bluetooth == .poweredOn, phase == .idle || phase == .scanning else { return }
        discovered.removeAll()
        central.scanForPeripherals(withServices: [spec.serviceUUID],
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
        phase = .scanning
    }

    func stopScan() {
        wantScan = false
        guard central != nil else { return }
        central.stopScan()
        if phase == .scanning { phase = .idle }
    }

    func connect(_ id: UUID) {
        guard bluetooth == .poweredOn else { return }
        let p = peripherals[id] ?? central.retrievePeripherals(withIdentifiers: [id]).first
        guard let p else { problem = .failed("That clock is no longer known — scan again."); return }
        central.stopScan()
        wantScan = false
        problem = nil
        userDisconnect = false
        peripheral = p
        peripherals[id] = p
        connectedName = p.name ?? "clock"
        phase = .connecting
        defaults.set(id.uuidString, forKey: Keys.lastClock)
        central.connect(p)
        note("connecting to \(connectedName ?? "clock") (\(id.uuidString.prefix(8)))")
    }

    /// Reconnects the last clock if Bluetooth is up and nothing is connected.
    func reconnectLast() {
        guard bluetooth == .poweredOn, phase == .idle || phase == .scanning, let id = lastClockID else { return }
        connect(id)
    }

    func disconnect() {
        userDisconnect = true
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        if phase == .connecting { resetLink(reason: nil) }
    }

    func forgetLastClock() { defaults.removeObject(forKey: Keys.lastClock) }

    // MARK: Commands

    /// Sends one CLI line and waits for its `$` (or a timeout / disconnect).
    /// `echo: false` keeps app-internal requests (help discovery, time sync) out of the shell.
    @discardableResult
    func send(_ line: String, echo: Bool = true) async -> CommandResult {
        let line = line.trimmingCharacters(in: .whitespacesAndNewlines)
        let id = takeID()
        var result = CommandResult(id: id, line: line)
        guard phase == .ready, chars["cmd"] != nil else {
            result.outcome = .notConnected
            if echo { transcript(.sent, line, id); transcript(.status(.notConnected), "not connected", id) }
            return result
        }
        let bytes = "\(id) \(line)".utf8.count
        let limit = min(spec.commandChannel.maxRequestBytes, maxWrite ?? .max)
        if bytes > limit {
            result.outcome = .tooLong(bytes)
            if echo { transcript(.sent, line, id); transcript(.status(result.outcome), result.outcome.label, id) }
            return result
        }
        return await withCheckedContinuation { cont in
            queue.append(Pending(result: result, echo: echo, continuation: cont))
            pump()
        }
    }

    /// PROTOCOL.md "Keeping time": phone time + current UTC offset.
    @discardableResult
    func syncTime(echo: Bool = false) async -> CommandResult {
        let ms = Int64(Date().timeIntervalSince1970 * 1000)
        let off = TimeZone.current.secondsFromGMT() / 60
        return await send("chrono time epoch \(ms) \(off)", echo: echo)
    }

    /// Asks the firmware for its command list (`help`, then `help <group>`) and merges it.
    func refreshCatalog() async {
        let top = await send("help", echo: false)
        guard top.outcome.isOK else { return }
        var added = 0
        for group in CommandCatalog.parseGroups(top.lines) {
            let r = await send("help \(group)", echo: false)
            guard r.outcome.isOK else { continue }
            added += catalog.merge(helpRows: r.lines, group: group)
        }
        note("help: \(catalog.entries.count) commands (\(added) from firmware only)")
    }

    func readStatus() { if let c = chars["status"] { peripheral?.readValue(for: c) } }
    func readInfo() { if let c = chars["info"] { peripheral?.readValue(for: c) } }

    func clearTranscript() { transcript.removeAll() }

    // MARK: Queue

    private func takeID() -> UInt16 {
        let id = nextID
        let hi = UInt16(clamping: spec.commandChannel.requestIDRange.last ?? 65535)
        nextID = nextID >= hi ? 1 : nextID + 1  // skip 0: that's "no id"
        return id
    }

    private func pump() {
        guard inFlight == nil, !queue.isEmpty, let p = peripheral, let cmd = chars["cmd"] else { return }
        let next = queue.removeFirst()
        inFlight = next
        if next.echo && next.busyRetries == 0 { transcript(.sent, next.result.line, next.result.id) }
        let data = Data("\(next.result.id) \(next.result.line)".utf8)
        p.writeValue(data, for: cmd, type: .withResponse)
        let id = next.result.id
        let timeout = Duration.milliseconds(spec.commandChannel.recommendedTimeoutMs)
        timeoutTask?.cancel()
        timeoutTask = Task { [weak self] in
            try? await Task.sleep(for: timeout)
            guard !Task.isCancelled, let self, self.inFlight?.result.id == id else { return }
            self.finish(.timeout)
        }
    }

    private func finish(_ outcome: CommandOutcome) {
        guard var p = inFlight else { return }
        timeoutTask?.cancel()
        inFlight = nil
        // `busy`: the CLI is occupied or the queue is full → one retry after ~1 s (§3).
        if outcome == .status(.busy), p.busyRetries == 0 {
            p.busyRetries += 1
            p.result.lines.removeAll()
            if p.echo { note("busy, retrying in 1 s", id: p.result.id) }
            Task { [weak self] in
                try? await Task.sleep(for: .seconds(1))
                guard let self else { return }
                self.queue.insert(p, at: 0)
                self.pump()
            }
            return
        }
        p.result.outcome = outcome
        if p.echo { transcript(.status(outcome), outcome.label, p.result.id) }
        p.continuation.resume(returning: p.result)
        pump()
    }

    private func handle(_ event: ResponseEvent) {
        let mine = event.id != nil && event.id == inFlight?.result.id
        let echo = mine ? (inFlight?.echo ?? true) : true
        switch event {
        case .line(let id, let text):
            if mine { inFlight?.result.lines.append(text) }
            if echo { transcript(.text, text, id) }
        case .pair(let id, let key, let value):
            if mine { inFlight?.result.pairs.append((key, value)) }
            if echo { transcript(.pair, "\(key)=\(value)", id) }
        case .terminal(let id, let status):
            if mine { finish(.status(status)) }
            else { transcript(.status(.status(status)), "\(status) (unexpected id \(id))", id) }
        case .malformed(let raw):
            transcript(.raw, "malformed: \(raw)")
        }
    }

    // MARK: Transcript

    private func transcript(_ kind: ShellLine.Kind, _ text: String, _ id: UInt16? = nil) {
        transcript.append(ShellLine(kind, text, requestID: id))
        if transcript.count > Self.transcriptLimit {
            transcript.removeFirst(transcript.count - Self.transcriptLimit)
        }
    }

    func note(_ text: String, id: UInt16? = nil) { transcript(.note, text, id) }

    // MARK: Link lifecycle

    private func resetLink(reason: LinkProblem?) {
        for p in queue { var r = p.result; r.outcome = .linkLost; p.continuation.resume(returning: r) }
        queue.removeAll()
        if inFlight != nil { finish(.linkLost) }
        chars.removeAll()
        framer.reset()
        peripheral = nil
        maxWrite = nil
        connectedName = nil
        securingRetries = 0
        phase = .idle
        if let reason { problem = reason }
        if wantScan { startScan() }
    }

    private func becameReady() {
        guard phase != .ready else { return }
        phase = .ready
        problem = nil
        missedSnapshots = 0
        snapshotCount = 0
        if let p = peripheral { maxWrite = p.maximumWriteValueLength(for: .withResponse) }
        note("ready — mtu write \(maxWrite ?? 0) B")
        Task {
            if autoSyncTime {
                let r = await syncTime()
                note("time sync: \(r.outcome.label)")
            }
            await refreshCatalog()
        }
    }

    private func applySnapshot(_ data: Data) {
        do {
            let s = try decoder.decode(data)
            if let old = snapshot?.int("seq"), let new = s.int("seq"), snapshotCount > 0 {
                let gap = (new - old - 1 + 65536) % 65536  // u16 wraps
                if gap > 0 && gap < 1000 { missedSnapshots += gap }
            }
            snapshot = s
            snapshotError = nil
            snapshotCount += 1
        } catch {
            snapshotError = String(describing: error)
        }
    }

    private static func isSecurityError(_ error: Error) -> Bool {
        guard let att = error as? CBATTError else { return false }
        return [.insufficientAuthentication, .insufficientEncryption, .insufficientEncryptionKeySize]
            .contains(att.code)
    }
}

// MARK: - CBCentralManagerDelegate

extension ClockLink: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        bluetooth = central.state
        switch central.state {
        case .poweredOn:
            if problem == .bluetoothOff || problem == .bluetoothUnauthorized { problem = nil }
            if wantScan { startScan() }
        case .poweredOff:
            problem = .bluetoothOff
            resetLink(reason: .bluetoothOff)
        case .unauthorized: problem = .bluetoothUnauthorized
        case .unsupported: problem = .bluetoothUnsupported
        default: break
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        peripherals[peripheral.identifier] = peripheral
        let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String
            ?? peripheral.name ?? spec.advertising.localName ?? "clock"
        let pairing = pairingOpen(advertisementData[CBAdvertisementDataManufacturerDataKey] as? Data)
        let rssi = RSSI.intValue == 127 ? -127 : RSSI.intValue  // 127 = unavailable
        if let i = discovered.firstIndex(where: { $0.id == peripheral.identifier }) {
            discovered[i].name = name
            discovered[i].rssi = rssi
            if let pairing { discovered[i].pairingOpen = pairing }
            discovered[i].lastSeen = .now
        } else {
            discovered.append(DiscoveredClock(id: peripheral.identifier, name: name, rssi: rssi,
                                              pairingOpen: pairing, lastSeen: .now))
        }
    }

    /// Manufacturer data `<company id LE> <state>`: the pairing-window bit, per the spec.
    private func pairingOpen(_ data: Data?) -> Bool? {
        guard let md = spec.advertising.manufacturerData, let data, data.count >= 3 else { return nil }
        let b = [UInt8](data)
        guard Int(b[0]) | Int(b[1]) << 8 == md.companyID,
              let bit = spec.advertisingStateBit("pairing_window_open") else { return nil }
        return b[2] & (1 << bit) != 0
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        phase = .discovering
        peripheral.delegate = self
        peripheral.discoverServices([spec.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        note("connect failed: \(error?.localizedDescription ?? "unknown")")
        resetLink(reason: .failed(error?.localizedDescription ?? "Could not connect."))
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        timestamp: CFAbsoluteTime, isReconnecting: Bool, error: Error?) {
        let was = phase
        var reason: LinkProblem?
        if let e = error as? CBError, e.code == .peerRemovedPairingInformation {
            reason = .forgetThisDevice
        } else if !userDisconnect && (was == .securing || was == .discovering) {
            reason = .pairingWindowShut
        } else if !userDisconnect, let error {
            reason = .failed(error.localizedDescription)
        }
        note("disconnected" + (error.map { ": \($0.localizedDescription)" } ?? ""))
        resetLink(reason: reason)
    }
}

// MARK: - CBPeripheralDelegate

extension ClockLink: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let service = peripheral.services?.first(where: { $0.uuid == spec.serviceUUID }) else {
            note("clock service not found")
            central.cancelPeripheralConnection(peripheral)
            return
        }
        let uuids = spec.gatt.characteristics.values.map { CBUUID(string: $0.uuid) }
        peripheral.discoverCharacteristics(uuids, for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        for c in service.characteristics ?? [] {
            if let name = spec.gatt.characteristics.first(where: { CBUUID(string: $0.value.uuid) == c.uuid })?.key {
                chars[name] = c
            }
        }
        guard let rsp = chars["rsp"] else {
            note("rsp characteristic missing")
            return
        }
        // Subscribe to rsp first (§3). On an unbonded link this is what makes iOS pair.
        phase = .securing
        peripheral.setNotifyValue(true, for: rsp)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        let isRsp = characteristic.uuid == chars["rsp"]?.uuid
        if let error {
            // Before bonding every access fails; iOS runs the pairing prompt meanwhile. Retry.
            if Self.isSecurityError(error), securingRetries < 8 {
                securingRetries += 1
                Task { [weak self] in
                    try? await Task.sleep(for: .seconds(1.5))
                    guard let self, self.peripheral === peripheral else { return }
                    peripheral.setNotifyValue(true, for: characteristic)
                }
            } else {
                note("subscribe failed: \(error.localizedDescription)")
            }
            return
        }
        if isRsp {
            if let status = chars["status"] {
                peripheral.setNotifyValue(true, for: status)
                peripheral.readValue(for: status)
            }
            if let info = chars["info"] { peripheral.readValue(for: info) }
            becameReady()
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            note("read \(name(of: characteristic)) failed: \(error.localizedDescription)")
            return
        }
        guard let data = characteristic.value else { return }
        switch name(of: characteristic) {
        case "rsp":
            if showRawFrames { transcript(.raw, String(decoding: data, as: UTF8.self)) }
            for e in framer.feed(data) { handle(e) }
        case "status":
            applySnapshot(data)
        case "info":
            info = DeviceInfo(String(decoding: data, as: UTF8.self))
            if case .appTooOld(let clock, let app)? = info?.compatibility(appProto: spec.protocolVersion) {
                note("clock speaks protocol \(clock), this app \(app) — update the app")
            }
        default:
            break
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let error, name(of: characteristic) == "cmd" else { return }
        finish(.writeFailed(error.localizedDescription))
    }

    private func name(of c: CBCharacteristic) -> String {
        chars.first { $0.value.uuid == c.uuid }?.key ?? c.uuid.uuidString
    }
}

// MARK: - Previews

extension ClockLink {
    /// A link with no radio, filled from the golden vector, for SwiftUI previews.
    static func preview() -> ClockLink {
        let spec = try! ProtocolSpec.loadBundled()
        let link = ClockLink(spec: spec, startCentral: false)
        if let hex = spec.snapshot.golden?.hex, let data = Data(hex: hex) { link.applySnapshot(data) }
        link.info = DeviceInfo("fw=0.1.0 sha=ed6214f built=2026-09-27T12:00:00Z board=rev0_3 profile=dev sdk=v5.5.5 proto=1 schema=1")
        link.phase = .ready
        link.connectedName = "clock"
        link.discovered = [DiscoveredClock(id: UUID(), name: "clock", rssi: -52, pairingOpen: true, lastSeen: .now),
                           DiscoveredClock(id: UUID(), name: "clock", rssi: -80, pairingOpen: false, lastSeen: .now)]
        link.transcript = [
            ShellLine(.sent, "sys ver", requestID: 1),
            ShellLine(.text, "app     clock 0.1.0  ed6214f  2026-09-27T12:00:00Z", requestID: 1),
            ShellLine(.text, "build   PROFILE=dev  BOARD=rev0_3  C++23", requestID: 1),
            ShellLine(.status(.status(.ok)), "ok", requestID: 1),
            ShellLine(.sent, "motion home", requestID: 2),
            ShellLine(.text, "needs `unsafe on`", requestID: 2),
            ShellLine(.status(.status(.denied)), "denied", requestID: 2),
        ]
        return link
    }
}
