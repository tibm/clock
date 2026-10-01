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
    /// Absent on a contract older than the history log and the `bulk` characteristic.
    let history: History?

    enum CodingKeys: String, CodingKey {
        case protocolVersion = "protocol_version"
        case snapshotSchema = "snapshot_schema"
        case gatt, advertising
        case commandChannel = "command_channel"
        case commands, snapshot
        case soundFiles = "sound_files"
        case history
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
        /// The oldest record a decoder must accept; fields past it may be absent. Nil → `size`.
        let minSize: Int?
        let fields: [Field]
        /// Index = bit number.
        let flags: [String]
        let enums: [String: [String]]
        let golden: Golden?

        enum CodingKeys: String, CodingKey {
            case endianness, size, fields, flags, enums, golden
            case minSize = "min_size"
        }

        /// Bytes a record must have to be decoded at all.
        var requiredSize: Int { min(minSize ?? size, size) }
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

    // MARK: History

    struct History: Decodable, Sendable {
        let dir: String?
        let defaults: HistoryDefaults?
        let header: HistoryHeaderSpec
        let record: HistoryRecordSpec
        /// Encoding name → formula text, e.g. `"log_gas": "ohms = 10^(v / 8192)"`.
        let encodings: [String: String]
        /// Every top-level string list (`sample_flags`, `event_code`, …), by key. Fields name
        /// them in `bitfield` / `enum`, so a new list needs no Swift change.
        let lists: [String: [String]]
        /// Event code name → what its args mean (display only).
        let eventArgs: [String: String]
        let golden: HistoryGolden?

        enum CodingKeys: String, CodingKey {
            case dir, defaults, header, record, encodings, golden
            case eventArgs = "event_args"
        }

        private struct AnyKey: CodingKey {
            var stringValue: String
            var intValue: Int? { nil }
            init(stringValue: String) { self.stringValue = stringValue }
            init?(intValue: Int) { nil }
        }

        init(from decoder: Decoder) throws {
            let c = try decoder.container(keyedBy: CodingKeys.self)
            dir = try c.decodeIfPresent(String.self, forKey: .dir)
            defaults = try c.decodeIfPresent(HistoryDefaults.self, forKey: .defaults)
            header = try c.decode(HistoryHeaderSpec.self, forKey: .header)
            record = try c.decode(HistoryRecordSpec.self, forKey: .record)
            encodings = try c.decodeIfPresent([String: String].self, forKey: .encodings) ?? [:]
            eventArgs = try c.decodeIfPresent([String: String].self, forKey: .eventArgs) ?? [:]
            golden = try? c.decodeIfPresent(HistoryGolden.self, forKey: .golden)
            let any = try decoder.container(keyedBy: AnyKey.self)
            var lists: [String: [String]] = [:]
            for key in any.allKeys {
                if let l = try? any.decode([String].self, forKey: key) { lists[key.stringValue] = l }
            }
            self.lists = lists
        }
    }

    struct HistoryDefaults: Decodable, Sendable {
        let periodS: Int?
        let keepDays: Int?
        let capMB: Int?

        enum CodingKeys: String, CodingKey {
            case periodS = "period_s"
            case keepDays = "keep_days"
            case capMB = "cap_mb"
        }
    }

    struct HistoryHeaderSpec: Decodable, Sendable {
        let size: Int
        let fields: [HistoryField]
    }

    struct HistoryRecordSpec: Decodable, Sendable {
        let size: Int
        /// Kind byte (as a string key) → `sample` / `event`.
        let kinds: [String: String]
        let sample: [HistoryField]
        let event: [HistoryField]
    }

    /// One field of the history header or a record.
    struct HistoryField: Decodable, Sendable {
        let off: Int
        let type: String
        let name: String
        /// The only value allowed here (header magic/version, a record's kind).
        let value: JSONValue?
        let unit: String?
        let scale: Double?
        let validIf: String?
        /// Key of an `encodings` entry.
        let encoding: String?
        /// Key of a string list (e.g. `sample_flags`).
        let bitfield: String?
        let enumName: String?
        let sentinel: [String: String]?
        let note: String?

        enum CodingKeys: String, CodingKey {
            case off, type, name, value, unit, scale, encoding, bitfield, sentinel, note
            case validIf = "valid_if"
            case enumName = "enum"
        }
    }

    struct HistoryGolden: Decodable, Sendable {
        let sampleHex: String?
        let sampleDecoded: [String: JSONValue]?
        let eventHex: String?
        let eventDecoded: [String: JSONValue]?
        let headerHex: String?
        let headerDecoded: [String: JSONValue]?

        enum CodingKeys: String, CodingKey {
            case sampleHex = "sample_hex"
            case sampleDecoded = "sample_decoded"
            case eventHex = "event_hex"
            case eventDecoded = "event_decoded"
            case headerHex = "header_hex"
            case headerDecoded = "header_decoded"
        }
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
