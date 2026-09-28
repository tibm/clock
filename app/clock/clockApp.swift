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
    @State private var link: ClockLink?

    var body: some Scene {
        WindowGroup {
            switch loaded {
            case .success(let spec):
                if let link {
                    ContentView().environment(link)
                } else {
                    ProgressView().onAppear { link = ClockLink(spec: spec) }
                }
            case .failure(let error):
                ContentUnavailableView("protocol.json unreadable", systemImage: "exclamationmark.triangle",
                                       description: Text(String(describing: error)))
            }
        }
    }
}
