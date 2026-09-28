import Foundation
import CoreBluetooth

/// `protocol.json`: the machine-readable half of the clock ⇄ app contract (`PROTOCOL.md`).
///
/// Every number the app needs (UUIDs, snapshot offsets, types, scales, flags, enums, the
/// command list) comes from here, so a compatible protocol change is a JSON edit plus a
/// rebuild. Unknown keys are ignored; optional keys may be missing.
nonisolated struct ProtocolSpec: Decodable, Sendable {
    let protocolVersion: Int
    let snapshotSchema: Int
    let gatt: Gatt
    let advertising: Advertising
    let commandChannel: CommandChannel
    let commands: [Command]
    let snapshot: SnapshotSpec
    /// Absent on a contract older than the `blob` characteristic.
    let soundFiles: SoundFiles?

    enum CodingKeys: String, CodingKey {
        case protocolVersion = "protocol_version"
        case snapshotSchema = "snapshot_schema"
        case gatt, advertising
        case commandChannel = "command_channel"
        case commands, snapshot
        case soundFiles = "sound_files"
    }

    // MARK: GATT

    struct Gatt: Decodable, Sendable {
        let serviceUUID: String
        let characteristics: [String: Characteristic]
        let preferredMTU: Int?

        enum CodingKeys: String, CodingKey {
            case serviceUUID = "service_uuid"
            case characteristics
            case preferredMTU = "preferred_mtu"
        }
    }

    struct Characteristic: Decodable, Sendable {
        let uuid: String
        let properties: [String]
        let maxLen: Int?
        let len: Int?
        let encoding: String?

        enum CodingKeys: String, CodingKey {
            case uuid, properties, encoding, len
            case maxLen = "max_len"
        }
    }

    // MARK: Advertising

    struct Advertising: Decodable, Sendable {
        let localName: String?
        let manufacturerData: ManufacturerData?

        enum CodingKeys: String, CodingKey {
            case localName = "local_name"
            case manufacturerData = "manufacturer_data"
        }
    }

    struct ManufacturerData: Decodable, Sendable {
        let companyID: Int
        /// bit number (as a string key) → meaning, e.g. `"0": "pairing_window_open"`.
        let stateBits: [String: String]

        enum CodingKeys: String, CodingKey {
            case companyID = "company_id"
            case stateBits = "state_bits"
        }
    }

    // MARK: Command channel

    struct CommandChannel: Decodable, Sendable {
        let requestIDRange: [Int]
        let maxRequestBytes: Int
        let responseKinds: [String: String]
        let statuses: [String]
        let maxInFlight: Int
        let recommendedTimeoutMs: Int

        enum CodingKeys: String, CodingKey {
            case requestIDRange = "request_id_range"
            case maxRequestBytes = "max_request_bytes"
            case responseKinds = "response_kinds"
            case statuses
            case maxInFlight = "max_in_flight"
            case recommendedTimeoutMs = "recommended_timeout_ms"
        }
    }

    struct Command: Decodable, Sendable {
        /// Grammar, e.g. `audio vol <pct>` or `audio tone [<hz>] [<ms>]`.
        let line: String
        /// `implemented` or `planned` (other values are shown as-is).
        let status: String
        let unsafe: Bool?
        /// Argument name → range or free-text hint.
        let args: [String: ArgSpec]?
        let use: String?
    }

    /// One argument's constraint: `[min, max]` in the JSON, or a text hint (e.g. `">= 1e11 (ms)"`).
    enum ArgSpec: Decodable, Sendable, Hashable {
        case range(Double, Double)
        case hint(String)

        init(from decoder: Decoder) throws {
            let c = try decoder.singleValueContainer()
            if let r = try? c.decode([Double].self), r.count == 2 {
                self = .range(r[0], r[1])
            } else if let s = try? c.decode(String.self) {
                self = .hint(s)
            } else {
                self = .hint(String(describing: try c.decode(JSONValue.self)))
            }
        }

        var text: String {
            switch self {
            case .range(let lo, let hi): "\(Self.num(lo))–\(Self.num(hi))"
            case .hint(let s): s
            }
        }

        private static func num(_ d: Double) -> String {
            d == d.rounded() ? String(Int64(d)) : String(d)
        }
    }

    // MARK: Snapshot

    struct SnapshotSpec: Decodable, Sendable {
        let endianness: String?
        let size: Int
        let fields: [Field]
        /// Index = bit number.
        let flags: [String]
        let enums: [String: [String]]
        let golden: Golden?
    }

    struct Field: Decodable, Sendable {
        let off: Int
        let type: String
        let name: String
        let unit: String?
        let scale: Double?
        let validIf: String?
        let enumName: String?
        let bitfield: String?
        /// Raw value (as written in JSON, e.g. `"255"`, `"-1"`) → meaning.
        let sentinel: [String: String]?
        let note: String?

        enum CodingKeys: String, CodingKey {
            case off, type, name, unit, scale, bitfield, sentinel, note
            case validIf = "valid_if"
            case enumName = "enum"
        }
    }

    struct Golden: Decodable, Sendable {
        let hex: String
        let decoded: [String: JSONValue]
    }

    // MARK: Sound files

    struct SoundFiles: Decodable, Sendable {
        let dir: String?
        let format: AudioFormat
        let maxUploadBytes: Int
        let toneStates: [String]
        /// ATT error code as hex text (`"0x80"`) → its name and what to do.
        let blobATTErrors: [String: BlobError]

        enum CodingKeys: String, CodingKey {
            case dir, format
            case maxUploadBytes = "max_upload_bytes"
            case toneStates = "tone_states"
            case blobATTErrors = "blob_att_errors"
        }
    }

    struct AudioFormat: Decodable, Sendable {
        let container: String
        /// WAV `fmt ` audio format tag (1 = PCM).
        let audioFormat: Int
        let sampleRate: Int
        let channels: Int
        let bits: Int

        enum CodingKeys: String, CodingKey {
            case container, channels, bits
            case audioFormat = "audio_format"
            case sampleRate = "sample_rate"
        }
    }

    struct BlobError: Decodable, Sendable {
        let name: String
        let action: String
    }
}

// MARK: - Loading and lookups

nonisolated extension ProtocolSpec {
    enum LoadError: Error { case missingResource }

    /// The copy of `app/protocol.json` bundled with this build.
    static func loadBundled(_ bundle: Bundle = .main) throws -> ProtocolSpec {
        guard let url = bundle.url(forResource: "protocol", withExtension: "json") else {
            throw LoadError.missingResource
        }
        return try JSONDecoder().decode(ProtocolSpec.self, from: Data(contentsOf: url))
    }

    var serviceUUID: CBUUID { CBUUID(string: gatt.serviceUUID) }

    /// UUID of a characteristic by its protocol name (`cmd`, `rsp`, `status`, `info`).
    func characteristicUUID(_ name: String) -> CBUUID? {
        gatt.characteristics[name].map { CBUUID(string: $0.uuid) }
    }

    /// Name of a `blob` ATT error code (`busy`, `bad-offset`, …), nil when the JSON doesn't list it.
    func blobErrorName(_ code: Int) -> String? {
        soundFiles?.blobATTErrors.first { Int($0.key.dropFirst(2), radix: 16) == code }?.value.name
    }

    /// Bit number of a named advertising state bit (e.g. `pairing_window_open`).
    func advertisingStateBit(_ meaning: String) -> Int? {
        advertising.manufacturerData?.stateBits.first { $0.value == meaning }.flatMap { Int($0.key) }
    }
}
