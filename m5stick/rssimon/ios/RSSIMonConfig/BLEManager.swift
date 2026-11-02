import Foundation
import CoreBluetooth

class BLEManager: NSObject, ObservableObject {
    @Published var isScanning = false
    @Published var isConnected = false
    @Published var deviceFound = false
    @Published var statusMessage = "Tap Scan to find device"

    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var ssidChar: CBCharacteristic?
    private var passChar: CBCharacteristic?
    private var cmdChar: CBCharacteristic?
    private var statusChar: CBCharacteristic?

    private let serviceUUID = CBUUID(string: "12345678-1234-1234-1234-123456789abc")
    private let ssidUUID = CBUUID(string: "12345678-1234-1234-1234-123456789ab1")
    private let passUUID = CBUUID(string: "12345678-1234-1234-1234-123456789ab2")
    private let cmdUUID = CBUUID(string: "12345678-1234-1234-1234-123456789ab3")
    private let statusUUID = CBUUID(string: "12345678-1234-1234-1234-123456789ab4")

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    func startScan() {
        guard centralManager.state == .poweredOn else {
            statusMessage = "Bluetooth not available"
            return
        }
        isScanning = true
        deviceFound = false
        statusMessage = "Scanning..."
        centralManager.scanForPeripherals(withServices: [serviceUUID], options: nil)

        DispatchQueue.main.asyncAfter(deadline: .now() + 10) { [weak self] in
            if self?.isScanning == true && self?.deviceFound == false {
                self?.stopScan()
                self?.statusMessage = "Device not found"
            }
        }
    }

    func stopScan() {
        centralManager.stopScan()
        isScanning = false
    }

    func disconnect() {
        if let p = peripheral {
            centralManager.cancelPeripheralConnection(p)
        }
    }

    func configureWiFi(ssid: String, password: String) {
        guard let ssidChar = ssidChar, let passChar = passChar, let cmdChar = cmdChar else {
            statusMessage = "Not ready"
            return
        }
        guard let p = peripheral else { return }

        statusMessage = "Sending config..."

        if let data = ssid.data(using: .utf8) {
            p.writeValue(data, for: ssidChar, type: .withResponse)
        }

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
            if let data = password.data(using: .utf8) {
                p.writeValue(data, for: passChar, type: .withResponse)
            }

            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                if let data = "connect".data(using: .utf8) {
                    p.writeValue(data, for: cmdChar, type: .withResponse)
                }
                self.statusMessage = "Config sent!"
            }
        }
    }
}

extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            statusMessage = "Tap Scan to find device"
        case .poweredOff:
            statusMessage = "Bluetooth is off"
        case .unauthorized:
            statusMessage = "Bluetooth unauthorized"
        default:
            statusMessage = "Bluetooth unavailable"
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        stopScan()
        deviceFound = true
        self.peripheral = peripheral
        statusMessage = "Found RSSIMon, connecting..."
        centralManager.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        isConnected = true
        statusMessage = "Connected!"
        peripheral.delegate = self
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        isConnected = false
        self.peripheral = nil
        ssidChar = nil
        passChar = nil
        cmdChar = nil
        statusChar = nil
        statusMessage = "Disconnected"
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        statusMessage = "Connection failed"
    }
}

extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let services = peripheral.services else { return }
        for service in services {
            peripheral.discoverCharacteristics(nil, for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard let chars = service.characteristics else { return }
        for char in chars {
            switch char.uuid {
            case ssidUUID: ssidChar = char
            case passUUID: passChar = char
            case cmdUUID: cmdChar = char
            case statusUUID:
                statusChar = char
                peripheral.setNotifyValue(true, for: char)
            default: break
            }
        }
        statusMessage = "Ready to configure"
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if characteristic.uuid == statusUUID, let data = characteristic.value, let status = String(data: data, encoding: .utf8) {
            statusMessage = "Device: \(status)"
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            statusMessage = "Write error: \(error.localizedDescription)"
        }
    }
}
