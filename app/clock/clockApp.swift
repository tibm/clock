//
//  clockApp.swift
//  clock
//
//  Created by Tibo Mauron on 9/27/26.
//

import SwiftUI

@main
struct clockApp: App {
    /// The contract is bundled; a bad `protocol.json` is a build problem, shown rather than crashed on.
    private let loaded = Result { try ProtocolSpec.loadBundled() }
    @State private var tones: ToneStore?
    @State private var history: HistoryStore?

    var body: some Scene {
        WindowGroup {
            switch loaded {
            case .success(let spec):
                if let tones, let history {
                    ContentView().environment(tones.link).environment(tones).environment(history)
                } else {
                    ProgressView().onAppear {
                        let link = ClockLink(spec: spec)
                        tones = ToneStore(link: link)
                        history = HistoryStore(link: link)
                    }
                }
            case .failure(let error):
                ContentUnavailableView("protocol.json unreadable", systemImage: "exclamationmark.triangle",
                                       description: Text(String(describing: error)))
            }
        }
    }
}
