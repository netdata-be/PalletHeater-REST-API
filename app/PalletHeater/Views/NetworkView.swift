import SwiftUI

struct NetworkView: View {
    @EnvironmentObject var ble: BLEManager
    @State private var showRebootAlert = false
    @State private var showHostnameEditor = false
    @State private var editingHostname = ""
    @State private var showNetConfigEditor = false
    @State private var editingNetConfig = NetworkConfig()

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    wifiSection
                    bleSection
                    deviceSection
                }
                .padding()
            }
            .navigationTitle("Network")
            .navigationBarTitleDisplayMode(.large)
        }
        .sheet(isPresented: $showHostnameEditor) {
            HostnameEditor(hostname: $editingHostname) {
                ble.setHostname(editingHostname)
                showHostnameEditor = false
            }
            .presentationDetents([.medium])
        }
        .sheet(isPresented: $showNetConfigEditor) {
            NetConfigEditor(config: $editingNetConfig) {
                ble.setNetConfig(editingNetConfig)
                showNetConfigEditor = false
            }
            .presentationDetents([.large])
        }
    }

    private var wifiSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Wi-Fi", systemImage: ble.wifiIconName)
                .font(.headline)
                .foregroundStyle(ble.wifiSignalColor)

            Divider()

            infoRow(label: "SSID", value: ble.wifiSSID.isEmpty ? (ble.isConnected && ble.isPopulating ? "Loading…" : "—") : ble.wifiSSID)
            infoRow(label: "IP Address", value: ble.ipAddress.isEmpty ? (ble.isConnected && ble.isPopulating ? "Loading…" : "—") : ble.ipAddress)

            if ble.isConnected && !ble.connectionMode.isWiFi {
                Divider()

                HStack {
                    Text("IP Mode")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    Spacer()
                    Text(ble.networkConfig.mode == .dhcp ? "DHCP" : "Static")
                        .font(.subheadline.bold())
                    Button {
                        editingNetConfig = ble.networkConfig
                        showNetConfigEditor = true
                    } label: {
                        Image(systemName: "pencil")
                            .font(.subheadline)
                            .foregroundStyle(.orange)
                    }
                    .buttonStyle(.borderless)
                }

                if ble.networkConfig.mode == .staticIP {
                    infoRow(label: "Gateway", value: ble.networkConfig.gateway.isEmpty ? "—" : ble.networkConfig.gateway)
                    infoRow(label: "Subnet", value: ble.networkConfig.subnet.isEmpty ? "—" : ble.networkConfig.subnet)
                    infoRow(label: "DNS", value: ble.networkConfig.dns.isEmpty ? "—" : ble.networkConfig.dns)
                }
            }
        }
        .padding()
        .background(.background, in: RoundedRectangle(cornerRadius: 16))
        .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
    }

    @ViewBuilder
    private var bleSection: some View {
        if !ble.connectionMode.isWiFi {
            VStack(alignment: .leading, spacing: 12) {
                Label("Bluetooth", systemImage: ble.signalIconName)
                    .font(.headline)
                    .foregroundStyle(bleColor)

                Divider()

                if ble.isPopulating {
                    HStack(spacing: 10) {
                        ProgressView(value: ble.initialReadTotalCount > 0 ?
                                     Double(ble.initialReadTotalCount - ble.pendingInitialReadCount) / Double(ble.initialReadTotalCount) : nil)
                            .progressViewStyle(.linear)
                            .frame(height: 8)
                        Text(ble.initialReadProgressText)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                infoRow(label: "Signal", value: ble.rssi.map { "\(ble.signalQualityLabel) (\($0) dBm)" } ?? "—")

                if let rssi = ble.rssi {
                    SignalBar(value: rssi, range: -100 ... -30, color: bleColor)
                        .frame(height: 8)
                        .padding(.top, 2)
                }
            }
            .padding()
            .background(.background, in: RoundedRectangle(cornerRadius: 16))
            .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
        }
    }

    private var deviceSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("Device", systemImage: "cpu")
                .font(.headline)

            Divider()

            if !ble.isConnected {
                Text("Connect to control the device")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            } else if ble.connectionMode.isWiFi {
                wifiDeviceContent
            } else {
                bleDeviceContent
            }
        }
        .padding()
        .background(.background, in: RoundedRectangle(cornerRadius: 16))
        .shadow(color: .black.opacity(0.04), radius: 4, y: 2)
        .alert("Restart Device?", isPresented: $showRebootAlert) {
            Button("Restart", role: .destructive) { ble.reboot() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("The device will restart and reconnect.")
        }
    }

    @ViewBuilder
    private var wifiDeviceContent: some View {
        if let host = ble.connectionMode.host {
            infoRow(label: "Host", value: host)
            Divider()
        }
        Button(role: .destructive) {
            showRebootAlert = true
        } label: {
            HStack {
                Label("Restart Device", systemImage: "arrow.clockwise")
                Spacer()
                if ble.isRebooting { ProgressView() }
            }
        }
        .disabled(ble.isRebooting)

        if !ble.rebootStatus.isEmpty {
            Text(ble.rebootStatus)
                .font(.caption)
                .foregroundStyle(.secondary)
        }

        Divider()

        Button {
            ble.switchToBLE()
        } label: {
            Label("Reconnect via Bluetooth", systemImage: "antenna.radiowaves.left.and.right")
        }

        Button(role: .destructive) {
            ble.disconnectWiFi()
        } label: {
            Label("Disconnect", systemImage: "wifi.slash")
        }
    }

    @ViewBuilder
    private var bleDeviceContent: some View {
        HStack {
            Text("Hostname")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Text(ble.deviceHostname.isEmpty ? "—" : ble.deviceHostname)
                .font(.subheadline.bold())
            Button {
                editingHostname = ble.deviceHostname
                showHostnameEditor = true
            } label: {
                Image(systemName: "pencil")
                    .font(.subheadline)
                    .foregroundStyle(.orange)
            }
            .buttonStyle(.borderless)
        }

        Divider()

        Button(role: .destructive) {
            showRebootAlert = true
        } label: {
            HStack {
                Label("Restart Device", systemImage: "arrow.clockwise")
                Spacer()
                if ble.isRebooting { ProgressView() }
            }
        }
        .disabled(ble.isRebooting)

        if !ble.rebootStatus.isEmpty {
            Text(ble.rebootStatus)
                .font(.caption)
                .foregroundStyle(.secondary)
        }

        if !ble.ipAddress.isEmpty {
            Divider()
            Button {
                ble.switchToWiFi()
            } label: {
                Label("Reconnect via Wi-Fi (\(ble.ipAddress))", systemImage: "wifi")
            }
        }
    }

    private func infoRow(label: String, value: String) -> some View {
        HStack {
            Text(label)
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Text(value)
                .font(.subheadline.bold())
                .multilineTextAlignment(.trailing)
        }
    }

    private var wifiSignalText: String {
        guard let rssi = ble.wifiRSSI, rssi < 0 else { return "Not connected" }
        return "\(ble.wifiSignalLabel) (\(rssi) dBm)"
    }

    private var bleColor: Color {
        switch ble.signalQualityLabel {
        case "Good": return .green
        case "Okay": return .orange
        case "Bad":  return .red
        default:     return .secondary
        }
    }
}

private struct NetConfigEditor: View {
    @Binding var config: NetworkConfig
    let onSave: () -> Void
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Picker("IP Mode", selection: $config.mode) {
                        Text("DHCP").tag(NetworkConfig.Mode.dhcp)
                        Text("Static").tag(NetworkConfig.Mode.staticIP)
                    }
                    .pickerStyle(.segmented)
                    .listRowBackground(Color.clear)
                    .listRowInsets(.init())
                    .padding(.vertical, 4)
                } header: {
                    Text("Mode")
                }

                if config.mode == .staticIP {
                    Section("Static IP") {
                        ipField("IP Address", text: $config.ip, placeholder: "192.168.1.100")
                        ipField("Gateway", text: $config.gateway, placeholder: "192.168.1.1")
                        ipField("Subnet Mask", text: $config.subnet, placeholder: "255.255.255.0")
                        ipField("DNS Server", text: $config.dns, placeholder: "8.8.8.8")
                    }
                }

                Section {
                    Text("Changes take effect after restarting the device.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("IP Configuration")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Save") { onSave() }
                        .bold()
                        .disabled(!config.isValid)
                }
            }
        }
    }

    private func ipField(_ label: String, text: Binding<String>, placeholder: String) -> some View {
        HStack {
            Text(label)
                .foregroundStyle(.secondary)
            Spacer()
            TextField(placeholder, text: text)
                .multilineTextAlignment(.trailing)
                .keyboardType(.numbersAndPunctuation)
                .autocorrectionDisabled()
                .frame(maxWidth: 180)
        }
    }
}

private struct HostnameEditor: View {
    @Binding var hostname: String
    let onSave: () -> Void
    @Environment(\.dismiss) private var dismiss
    @FocusState private var focused: Bool

    var body: some View {
        NavigationStack {
            VStack(spacing: 24) {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Hostname")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    TextField("e.g. openpelletbridge", text: $hostname)
                        .font(.title3)
                        .textFieldStyle(.roundedBorder)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                        .focused($focused)
                }
                Text("Takes effect after restart. Use only letters, numbers, and hyphens.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                Spacer()
                Button(action: onSave) {
                    Text("Save")
                        .font(.headline)
                        .frame(maxWidth: .infinity)
                        .padding()
                        .background(hostname.isEmpty ? Color.secondary : Color.orange,
                                    in: RoundedRectangle(cornerRadius: 16))
                        .foregroundStyle(.white)
                }
                .disabled(hostname.isEmpty)
            }
            .padding()
            .navigationTitle("Edit Hostname")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Button("Cancel") { dismiss() }
                }
            }
            .onAppear { focused = true }
        }
    }
}

private struct SignalBar: View {
    let value: Int
    let range: ClosedRange<Int>
    let color: Color

    private var fraction: Double {
        let clamped = Double(max(range.lowerBound, min(range.upperBound, value)))
        return (clamped - Double(range.lowerBound)) / Double(range.upperBound - range.lowerBound)
    }

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 4)
                    .fill(Color.secondary.opacity(0.15))
                RoundedRectangle(cornerRadius: 4)
                    .fill(color.gradient)
                    .frame(width: geo.size.width * fraction)
            }
        }
    }
}
