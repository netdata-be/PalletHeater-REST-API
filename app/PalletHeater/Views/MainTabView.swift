import SwiftUI

struct MainTabView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        TabView {
            DashboardView()
                .tabItem {
                    Label("Dashboard", systemImage: "gauge.medium")
                }
            ControlsView()
                .tabItem {
                    Label("Controls", systemImage: "slider.horizontal.3")
                }
            ScheduleView()
                .tabItem {
                    Label("Schedule", systemImage: "calendar")
                }
            NetworkView()
                .tabItem {
                    Label("Network", systemImage: "wifi")
                }
        }
        .safeAreaInset(edge: .top, spacing: 0) {
            if !ble.isConnected {
                reconnectingBanner
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
        }
        .animation(.easeInOut(duration: 0.3), value: ble.isConnected)
    }

    private var reconnectingBanner: some View {
        HStack(spacing: 10) {
            ProgressView()
                .scaleEffect(0.8)
            VStack(alignment: .leading, spacing: 1) {
                Text(ble.bleState.label)
                    .font(.caption.bold())
                    .foregroundStyle(.white)
                if !ble.lastDisconnectError.isEmpty {
                    Text(ble.lastDisconnectError)
                        .font(.system(size: 9, weight: .regular, design: .monospaced))
                        .foregroundStyle(.white.opacity(0.8))
                }
            }
            Spacer()
            Button("Reconnect") { ble.startScan() }
                .font(.caption.bold())
                .foregroundStyle(.white.opacity(0.85))
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(Color.orange.gradient)
    }
}
