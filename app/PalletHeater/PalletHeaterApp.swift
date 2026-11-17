import SwiftUI

@main
struct PalletHeaterApp: App {
    @StateObject private var ble = BLEManager()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(ble)
        }
    }
}

struct RootView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        if ble.hasConnected {
            MainTabView()
                .transition(.opacity)
        } else if ble.bleState == .scanning || ble.bleState == .connecting || ble.bleState == .disconnected {
            ScanView()
                .transition(.opacity)
        } else {
            ConnectionSelectionView()
                .transition(.opacity)
        }
    }
}
