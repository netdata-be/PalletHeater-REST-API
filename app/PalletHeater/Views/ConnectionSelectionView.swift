import SwiftUI

struct ConnectionSelectionView: View {
    @EnvironmentObject var ble: BLEManager
    @AppStorage("lastWiFiHost") private var lastWiFiHost: String = ""
    @State private var wifiHost: String = ""
    @State private var showBLE = false
    @State private var wifiError: String? = nil

    var body: some View {
        NavigationStack {
            ZStack {
                Color(.systemGroupedBackground).ignoresSafeArea()

                VStack(spacing: 32) {
                    Spacer()

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
                        Text("Choose connection method")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }

                    Spacer()

                    VStack(spacing: 16) {
                        bluetoothCard
                        wifiCard
                    }
                    .padding(.horizontal)

                    Spacer()
                }
            }
            .navigationDestination(isPresented: $showBLE) {
                ScanView()
            }
            .onAppear {
                if wifiHost.isEmpty { wifiHost = lastWiFiHost }
            }
        }
    }

    private var bluetoothCard: some View {
        Button {
            showBLE = true
        } label: {
            HStack(spacing: 16) {
                ZStack {
                    Circle()
                        .fill(Color.blue.opacity(0.12))
                        .frame(width: 48, height: 48)
                    Image(systemName: "antenna.radiowaves.left.and.right")
                        .font(.title2)
                        .foregroundStyle(.blue)
                }
                VStack(alignment: .leading, spacing: 2) {
                    Text("Bluetooth")
                        .font(.headline)
                        .foregroundStyle(.primary)
                    Text("Connect via BLE")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Image(systemName: "chevron.right")
                    .font(.caption.bold())
                    .foregroundStyle(.secondary)
            }
            .padding()
            .background(.background, in: RoundedRectangle(cornerRadius: 16))
            .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
        }
        .buttonStyle(.plain)
    }

    private var wifiCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 16) {
                ZStack {
                    Circle()
                        .fill(Color.orange.opacity(0.12))
                        .frame(width: 48, height: 48)
                    Image(systemName: "wifi")
                        .font(.title2)
                        .foregroundStyle(.orange)
                }
                VStack(alignment: .leading, spacing: 2) {
                    Text("Wi-Fi")
                        .font(.headline)
                    Text("Connect via REST API")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            TextField("IP address or hostname (e.g. 192.168.1.100)", text: $wifiHost)
                .keyboardType(.URL)
                .autocorrectionDisabled()
                .textInputAutocapitalization(.never)
                .padding(10)
                .background(Color(.systemFill), in: RoundedRectangle(cornerRadius: 10))

            if let err = wifiError {
                Text(err)
                    .font(.caption)
                    .foregroundStyle(.red)
            }

            Button {
                connectWiFi()
            } label: {
                Text("Connect")
                    .font(.headline)
                    .frame(maxWidth: .infinity)
                    .padding()
                    .background(wifiHost.trimmingCharacters(in: .whitespaces).isEmpty ? Color.secondary : Color.orange,
                                in: RoundedRectangle(cornerRadius: 12))
                    .foregroundStyle(.white)
            }
            .disabled(wifiHost.trimmingCharacters(in: .whitespaces).isEmpty)
        }
        .padding()
        .background(.background, in: RoundedRectangle(cornerRadius: 16))
        .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
    }

    private func connectWiFi() {
        let host = wifiHost.trimmingCharacters(in: .whitespaces)
        guard !host.isEmpty else { return }
        wifiError = nil
        lastWiFiHost = host
        ble.connectWiFi(host: host)
    }
}
