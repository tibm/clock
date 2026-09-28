import SwiftUI
#if os(iOS)
import UIKit
#else
import AppKit
#endif

extension CommandOutcome {
    var color: Color {
        switch self {
        case .status(.ok): .green
        case .status(.busy): .yellow
        case .status(.badArg), .status(.denied), .status(.notPresent): .orange
        case .status: .red
        case .timeout, .linkLost, .notConnected, .tooLong, .writeFailed: .red
        }
    }
}

extension LinkPhase {
    var label: String {
        switch self {
        case .idle: "Not connected"
        case .scanning: "Scanning…"
        case .connecting: "Connecting…"
        case .discovering: "Discovering…"
        case .securing: "Pairing / securing…"
        case .ready: "Connected"
        }
    }

    var color: Color {
        switch self {
        case .ready: .green
        case .idle: .secondary
        default: .orange
        }
    }
}

/// A small capsule label.
struct Badge: View {
    let text: String
    var color: Color = .secondary

    var body: some View {
        Text(text)
            .font(.caption2.weight(.semibold))
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .foregroundStyle(color)
            .background(color.opacity(0.15), in: Capsule())
    }
}

/// Connection dot + label, for toolbars.
struct LinkStatusLabel: View {
    @Environment(ClockLink.self) private var link

    var body: some View {
        HStack(spacing: 6) {
            Circle().fill(link.phase.color).frame(width: 8, height: 8)
            Text(link.phase == .ready ? (link.connectedName ?? "clock") : link.phase.label)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }
}

enum Pasteboard {
    static func copy(_ text: String) {
        #if os(iOS)
        UIPasteboard.general.string = text
        #else
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
        #endif
    }
}

extension View {
    /// No autocapitalisation / autocorrection: CLI input.
    func cliInput() -> some View {
        #if os(iOS)
        self.textInputAutocapitalization(.never).autocorrectionDisabled()
        #else
        self.autocorrectionDisabled()
        #endif
    }
}
