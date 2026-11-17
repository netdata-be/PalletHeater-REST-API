import SwiftUI

struct ScanView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        ZStack {
            Color(.systemGroupedBackground).ignoresSafeArea()

            VStack(spacing: 32) {
                Spacer()

                // Icon
                ZStack {
                    Circle()
                        .fill(Color.orange.opacity(0.15))
                        .frame(width: 140, height: 140)
                    Image(systemName: "flame.fill")
                        .font(.system(size: 64))
                        .foregroundStyle(
                            LinearGradient(
                                colors: [.orange, .red],
                                startPoint: .top,
                                endPoint: .bottom
                            )
                        )
                }

                VStack(spacing: 8) {
                    Text("PalletHeater")
                        .font(.largeTitle.bold())
                    Text("Pellet stove controller")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }

                Spacer()

                // Status card
                VStack(spacing: 16) {
                    stateIndicator
                    signalIndicator
                    actionButton
                }
                .padding(24)
                .background(.background, in: RoundedRectangle(cornerRadius: 20))
                .padding(.horizontal)

                Spacer()
            }
        }
        .onAppear {
            if ble.bleState == .idle {
                ble.startScan()
            }
        }
        .animation(.easeInOut, value: ble.bleState)
    }

    @ViewBuilder
    private var stateIndicator: some View {
        HStack(spacing: 12) {
            if ble.bleState == .scanning || ble.bleState == .connecting {
                ProgressView()
                    .scaleEffect(0.9)
            } else {
                Image(systemName: stateIcon)
                    .foregroundStyle(stateColor)
            }
            Text(ble.bleState.label)
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
        }
    }

    @ViewBuilder
    private var signalIndicator: some View {
        if ble.rssi != nil {
            HStack(spacing: 12) {
                Image(systemName: ble.signalIconName)
                    .foregroundStyle(signalColor)
                    .frame(width: 20)
                Text("Bluetooth signal")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Text(ble.signalDescription)
                    .font(.caption.bold())
                    .foregroundStyle(signalColor)
            }
        }
    }

    @ViewBuilder
    private var actionButton: some View {
        switch ble.bleState {
        case .idle, .disconnected, .bluetoothOff:
            Button {
                ble.startScan()
            } label: {
                Label("Scan for Stove", systemImage: "antenna.radiowaves.left.and.right")
                    .font(.headline)
                    .frame(maxWidth: .infinity)
                    .padding()
                    .background(Color.orange)
                    .foregroundStyle(.white)
                    .clipShape(RoundedRectangle(cornerRadius: 14))
            }
        case .scanning, .connecting:
            Button {
                ble.disconnect()
            } label: {
                Text("Cancel")
                    .font(.headline)
                    .frame(maxWidth: .infinity)
                    .padding()
                    .background(Color(.systemFill))
                    .clipShape(RoundedRectangle(cornerRadius: 14))
            }
        case .connected:
            EmptyView()
        }
    }

    private var stateIcon: String {
        switch ble.bleState {
        case .idle:         return "wave.3.right"
        case .disconnected: return "wifi.slash"
        case .bluetoothOff: return "bluetooth.slash"
        default:            return "checkmark.circle.fill"
        }
    }

    private var stateColor: Color {
        switch ble.bleState {
        case .bluetoothOff: return .red
        case .disconnected: return .orange
        default:            return .secondary
        }
    }

    private var signalColor: Color {
        switch ble.signalQualityLabel {
        case "Good": return .green
        case "Okay": return .orange
        case "Bad":  return .red
        default:     return .secondary
        }
    }
}
