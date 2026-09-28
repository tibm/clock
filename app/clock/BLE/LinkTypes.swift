import Foundation
import CoreBluetooth

/// A clock seen in a scan.
struct DiscoveredClock: Identifiable, Equatable {
    let id: UUID
    var name: String
    var rssi: Int
    /// From the manufacturer data state byte; nil when the advertisement didn't carry it
    /// (background scans, or a firmware without it).
    var pairingOpen: Bool?
    var lastSeen: Date
}

/// Where the link is.
enum LinkPhase: Equatable {
    case idle
    case scanning
    case connecting
    case discovering
    /// Waiting for iOS pairing / encryption before the characteristics answer.
    case securing
    case ready
}

/// Why the last attempt ended, phrased for the user (PROTOCOL.md §2 failure table).
enum LinkProblem: Equatable {
    case bluetoothOff
    case bluetoothUnauthorized
    case bluetoothUnsupported
    /// Dropped right after encryption: the pairing window was shut.
    case pairingWindowShut
    /// The clock forgot this phone but iOS still has keys.
    case forgetThisDevice
    case failed(String)

    var title: String {
        switch self {
        case .bluetoothOff: "Bluetooth is off"
        case .bluetoothUnauthorized: "Bluetooth permission denied"
        case .bluetoothUnsupported: "Bluetooth LE not available"
        case .pairingWindowShut: "Pairing refused"
        case .forgetThisDevice: "Clock forgot this device"
        case .failed: "Connection failed"
        }
    }

    var hint: String {
        switch self {
        case .bluetoothOff: "Turn Bluetooth on in Settings or Control Center."
        case .bluetoothUnauthorized: "Allow Bluetooth for this app in Settings → Privacy → Bluetooth."
        case .bluetoothUnsupported: "This device (or the Simulator) has no Bluetooth LE. Run on an iPhone or a Mac."
        case .pairingWindowShut: "Hold the knob on top of the clock for 10 seconds until the lights breathe blue, then try again."
        case .forgetThisDevice: "Settings → Bluetooth → clock → Forget This Device, then open the pairing window (hold the knob 10 s) and connect again."
        case .failed(let why): why
        }
    }
}

/// How a request ended. Only `.status` means the clock answered.
enum CommandOutcome: Equatable {
    case status(CommandStatus)
    case timeout
    /// The link dropped before `$`: outcome unknown (expected for `sys reboot`).
    case linkLost
    case notConnected
    case tooLong(Int)
    case writeFailed(String)

    var label: String {
        switch self {
        case .status(let s): s.rawValue
        case .timeout: "timeout"
        case .linkLost: "link lost"
        case .notConnected: "not connected"
        case .tooLong(let n): "too long (\(n) B)"
        case .writeFailed(let e): "write failed: \(e)"
        }
    }

    var isOK: Bool { self == .status(.ok) }
}

struct CommandResult {
    let id: UInt16
    let line: String
    var lines: [String] = []
    var pairs: [(key: String, value: String)] = []
    var outcome: CommandOutcome = .timeout

    /// Text lines joined, for inline display.
    var text: String { lines.joined(separator: "\n") }

    /// The first `=` pair with this key.
    func pair(_ key: String) -> String? { pairs.first { $0.key == key }?.value }
}

/// How a `blob` write ended.
enum BlobWriteResult: Equatable {
    case ok
    /// The clock refused it with this ATT error code (PROTOCOL.md "Sound files").
    case att(Int)
    case failed(String)
    case linkLost
    case notConnected

    init(_ error: Error) {
        let e = error as NSError
        self = e.domain == CBATTErrorDomain ? .att(e.code) : .failed(e.localizedDescription)
    }
}

/// One line in the shell transcript.
struct ShellLine: Identifiable, Equatable {
    enum Kind: Equatable {
        case sent
        case text
        case pair
        case status(CommandOutcome)
        case note
        case raw
    }

    let id = UUID()
    let at: Date
    let requestID: UInt16?
    let kind: Kind
    let text: String

    init(_ kind: Kind, _ text: String, requestID: UInt16? = nil, at: Date = .now) {
        self.kind = kind
        self.text = text
        self.requestID = requestID
        self.at = at
    }
}
