//
//  ContentView.swift
//  clock
//
//  Created by Tibo Mauron on 9/27/26.
//

import SwiftUI
import CoreBluetooth

struct ContentView: View {
    @Environment(ClockLink.self) private var link
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        TabView {
            Tab("Clock", systemImage: "dot.radiowaves.left.and.right") { ConnectView() }
            Tab("Status", systemImage: "gauge.with.dots.needle.33percent") { StatusView() }
            Tab("History", systemImage: "chart.xyaxis.line") { HistoryView() }
            Tab("Sounds", systemImage: "speaker.wave.2") { SoundsView() }
            Tab("Commands", systemImage: "list.bullet.rectangle") { CommandsView() }
            Tab("Shell", systemImage: "terminal") { ShellView() }
        }
        .onChange(of: scenePhase) { _, phase in
            // PROTOCOL.md §7: don't hold the (single) link open in the background.
            #if os(iOS)
            switch phase {
            case .background: link.disconnect()
            case .active: link.reconnectLast()
            default: break
            }
            #endif
        }
        .onChange(of: link.bluetooth) { _, state in
            if state == .poweredOn { link.reconnectLast() }
        }
    }
}

#Preview {
    let store = ToneStore.preview()
    ContentView().environment(store.link).environment(store).environment(HistoryStore.preview(link: store.link))
}
