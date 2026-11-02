import SwiftUI

struct ContentView: View {
    @StateObject private var ble = BLEManager()
    @State private var ssid = ""
    @State private var password = ""

    var body: some View {
        NavigationView {
            Form {
                Section {
                    HStack {
                        Circle()
                            .fill(ble.isConnected ? Color.green : Color.gray)
                            .frame(width: 12, height: 12)
                        Text(ble.statusMessage)
                            .foregroundColor(.secondary)
                    }

                    if !ble.isConnected {
                        Button(ble.isScanning ? "Scanning..." : "Scan for Device") {
                            ble.startScan()
                        }
                        .disabled(ble.isScanning)
                    } else {
                        Button("Disconnect") {
                            ble.disconnect()
                        }
                        .foregroundColor(.red)
                    }
                }

                Section(header: Text("WiFi Configuration")) {
                    TextField("SSID", text: $ssid)
                        .autocapitalization(.none)
                        .disableAutocorrection(true)

                    SecureField("Password", text: $password)

                    Button("Configure WiFi") {
                        ble.configureWiFi(ssid: ssid, password: password)
                    }
                    .disabled(!ble.isConnected || ssid.isEmpty)
                }
            }
            .navigationTitle("RSSIMon Config")
        }
    }
}

#Preview {
    ContentView()
}
