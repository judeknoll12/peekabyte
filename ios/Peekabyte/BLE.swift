import CoreBluetooth
import Foundation

/// Bluetooth for the web page: the slice of CoreBluetooth that app/js/native.js turns back
/// into Web Bluetooth. The app declares the bluetooth-central background mode, so the link
/// to the pet stays up while the phone is locked or the app is in the background.
/// Everything runs on the main queue.
final class BLE: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var emit: ((String, [String: Any]) -> Void)?

    private var central: CBCentralManager!
    private var peripherals: [UUID: CBPeripheral] = [:]
    private var lastReported: [UUID: Date] = [:]
    private var scanning = false
    private var scanService: CBUUID?
    private var stateWaiters: [() -> Void] = []

    private var pendingConnect: [UUID: (Result<[String: Any], Error>) -> Void] = [:]
    private var connectService: [UUID: CBUUID] = [:]
    private var connectTimers: [UUID: Timer] = [:]
    private var pendingNotify: [String: (Result<Bool, Error>) -> Void] = [:]
    private var pendingWrites: [UUID: [(Result<Bool, Error>) -> Void]] = [:]
    private var waitingForRoom: [UUID: [() -> Void]] = [:]

    private let knownKey = "peekabyte.lastPet"

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    var stateName: String {
        switch central.state {
        case .poweredOn: return "on"
        case .poweredOff: return "off"
        case .unauthorized: return "unauthorized"
        case .unsupported: return "unsupported"
        case .resetting: return "resetting"
        default: return "unknown"
        }
    }

    private var stateError: Error? {
        switch central.state {
        case .poweredOn: return nil
        case .poweredOff: return BridgeError("Bluetooth is off. Turn it on in Control Center.")
        case .unauthorized: return BridgeError("Peekabyte isn't allowed to use Bluetooth. Turn it on in Settings › Peekabyte.")
        default: return BridgeError("Bluetooth isn't available right now.")
        }
    }

    /// Runs `then` once Bluetooth has started up (right after launch it's briefly "unknown").
    private func whenReady(_ then: @escaping (Error?) -> Void) {
        if central.state == .unknown || central.state == .resetting {
            stateWaiters.append { [weak self] in then(self?.stateError) }
        } else {
            then(stateError)
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        emit?("ble.state", ["state": stateName])
        if central.state != .unknown && central.state != .resetting {
            let waiters = stateWaiters
            stateWaiters.removeAll()
            waiters.forEach { $0() }
        }
        if central.state == .poweredOn && scanning { startScan() }
    }

    // MARK: finding pets

    func scan(_ on: Bool, service: String) {
        scanning = on
        scanService = uuid(service)
        guard on else {
            if central.state == .poweredOn { central.stopScan() }
            return
        }
        whenReady { [weak self] error in
            if let error { self?.emit?("ble.scanError", ["error": error.localizedDescription]) } else { self?.startScan() }
        }
    }

    private func startScan() {
        guard scanning, central.state == .poweredOn else { return }
        lastReported.removeAll()
        central.scanForPeripherals(withServices: scanService.map { [$0] },
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
        // A pet this phone is already connected to may not show up in a scan; list it anyway.
        if let service = scanService {
            for p in central.retrieveConnectedPeripherals(withServices: [service]) {
                peripherals[p.identifier] = p
                emit?("ble.found", ["id": p.identifier.uuidString, "name": p.name ?? "Peekabyte", "rssi": 0])
            }
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        peripherals[p.identifier] = p
        let now = Date()
        if let last = lastReported[p.identifier], now.timeIntervalSince(last) < 0.7 { return }
        lastReported[p.identifier] = now
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? p.name ?? "Peekabyte"
        emit?("ble.found", ["id": p.identifier.uuidString, "name": name, "rssi": RSSI.intValue])
    }

    /// The pet this phone connected to last time, so the page can reconnect without asking.
    func known(done: @escaping (Result<[[String: Any]], Error>) -> Void) {
        whenReady { [weak self] _ in
            guard let self, let saved = UserDefaults.standard.dictionary(forKey: self.knownKey),
                  let idText = saved["id"] as? String, let id = UUID(uuidString: idText) else { return done(.success([])) }
            if self.peripherals[id] == nil, self.central.state == .poweredOn,
               let p = self.central.retrievePeripherals(withIdentifiers: [id]).first {
                self.peripherals[id] = p
            }
            guard let p = self.peripherals[id] else { return done(.success([])) }
            done(.success([["id": idText, "name": p.name ?? (saved["name"] as? String ?? "Peekabyte"),
                            "connected": p.state == .connected]]))
        }
    }

    private func remember(_ p: CBPeripheral) {
        UserDefaults.standard.set(["id": p.identifier.uuidString, "name": p.name ?? "Peekabyte"], forKey: knownKey)
    }

    // MARK: connecting

    func connect(id: String, service: String, done: @escaping (Result<[String: Any], Error>) -> Void) {
        whenReady { [weak self] error in
            guard let self else { return }
            if let error { return done(.failure(error)) }
            guard let key = UUID(uuidString: id) else { return done(.failure(BridgeError("Unknown pet"))) }
            if self.peripherals[key] == nil, let p = self.central.retrievePeripherals(withIdentifiers: [key]).first {
                self.peripherals[key] = p
            }
            guard let p = self.peripherals[key] else { return done(.failure(BridgeError("That pet isn't nearby."))) }
            p.delegate = self
            self.connectService[key] = self.uuid(service)
            self.pendingConnect.removeValue(forKey: key)?(.failure(BridgeError("Replaced by a newer attempt")))
            self.pendingConnect[key] = done
            self.connectTimers[key]?.invalidate()
            self.connectTimers[key] = Timer.scheduledTimer(withTimeInterval: 15, repeats: false) { [weak self] _ in
                self?.finishConnect(key, .failure(BridgeError("The pet didn't answer. Is it switched on and close by?")))
                self?.central.cancelPeripheralConnection(p)
            }
            if p.state == .connected { self.discover(p) } else { self.central.connect(p, options: nil) }
        }
    }

    func disconnect(id: String) {
        guard let key = UUID(uuidString: id), let p = peripherals[key] else { return }
        central.cancelPeripheralConnection(p)
    }

    func centralManager(_ central: CBCentralManager, didConnect p: CBPeripheral) {
        remember(p)
        discover(p)
    }

    private func discover(_ p: CBPeripheral) {
        let wanted = connectService[p.identifier]
        if let s = p.services?.first(where: { wanted == nil || $0.uuid == wanted }), s.characteristics?.isEmpty == false {
            return finishConnect(p.identifier, .success(describe(p)))
        }
        p.discoverServices(wanted.map { [$0] })
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        if let error { return finishConnect(p.identifier, .failure(error)) }
        let wanted = connectService[p.identifier]
        guard let s = p.services?.first(where: { wanted == nil || $0.uuid == wanted }) else {
            return finishConnect(p.identifier, .failure(BridgeError("That doesn't look like a Peekabyte.")))
        }
        p.discoverCharacteristics(nil, for: s)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error { return finishConnect(p.identifier, .failure(error)) }
        finishConnect(p.identifier, .success(describe(p)))
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        finishConnect(p.identifier, .failure(error ?? BridgeError("Couldn't connect to the pet.")))
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        let key = p.identifier
        finishConnect(key, .failure(error ?? BridgeError("The pet disconnected.")))
        let lost = BridgeError("The pet disconnected.")
        pendingWrites.removeValue(forKey: key)?.forEach { $0(.failure(lost)) }
        waitingForRoom.removeValue(forKey: key)
        for (k, done) in pendingNotify where k.hasPrefix(key.uuidString) {
            pendingNotify.removeValue(forKey: k)
            done(.failure(lost))
        }
        emit?("ble.disconnected", ["id": key.uuidString, "error": error?.localizedDescription ?? ""])
    }

    private func finishConnect(_ key: UUID, _ result: Result<[String: Any], Error>) {
        connectTimers.removeValue(forKey: key)?.invalidate()
        pendingConnect.removeValue(forKey: key)?(result)
    }

    private func describe(_ p: CBPeripheral) -> [String: Any] {
        ["id": p.identifier.uuidString, "name": p.name ?? "Peekabyte"]
    }

    // MARK: data

    func subscribe(id: String, service: String, char: String, on: Bool, done: @escaping (Result<Bool, Error>) -> Void) {
        guard let (p, c) = find(id, service, char) else { return done(.failure(BridgeError("Not connected"))) }
        if c.isNotifying == on { return done(.success(true)) }
        pendingNotify[key(p, c)] = done
        p.setNotifyValue(on, for: c)
    }

    func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor c: CBCharacteristic, error: Error?) {
        guard let done = pendingNotify.removeValue(forKey: key(p, c)) else { return }
        if let error { done(.failure(error)) } else { done(.success(true)) }
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
        guard error == nil, let value = c.value else { return }
        emit?("ble.notify", ["id": p.identifier.uuidString, "char": c.uuid.uuidString, "data": value.base64EncodedString()])
    }

    func write(id: String, service: String, char: String, data: Data, response: Bool,
               done: @escaping (Result<Bool, Error>) -> Void) {
        guard let (p, c) = find(id, service, char), p.state == .connected else {
            return done(.failure(BridgeError("Not connected")))
        }
        if response {
            pendingWrites[p.identifier, default: []].append(done)
            p.writeValue(data, for: c, type: .withResponse)
        } else if p.canSendWriteWithoutResponse {
            p.writeValue(data, for: c, type: .withoutResponse)
            done(.success(true))
        } else {
            // The radio's queue is full; send it as soon as there's room again.
            waitingForRoom[p.identifier, default: []].append { [weak self] in
                self?.write(id: id, service: service, char: char, data: data, response: false, done: done)
            }
        }
    }

    func peripheralIsReady(toSendWriteWithoutResponse p: CBPeripheral) {
        let waiting = waitingForRoom.removeValue(forKey: p.identifier) ?? []
        waiting.forEach { $0() }
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor c: CBCharacteristic, error: Error?) {
        guard var queue = pendingWrites[p.identifier], !queue.isEmpty else { return }
        let done = queue.removeFirst()
        pendingWrites[p.identifier] = queue
        if let error { done(.failure(error)) } else { done(.success(true)) }
    }

    // MARK: helpers

    private func find(_ id: String, _ service: String, _ char: String) -> (CBPeripheral, CBCharacteristic)? {
        guard let key = UUID(uuidString: id), let p = peripherals[key],
              let s = uuid(service), let cu = uuid(char),
              let svc = p.services?.first(where: { $0.uuid == s }),
              let c = svc.characteristics?.first(where: { $0.uuid == cu }) else { return nil }
        return (p, c)
    }

    private func key(_ p: CBPeripheral, _ c: CBCharacteristic) -> String {
        p.identifier.uuidString + "/" + c.uuid.uuidString
    }

    /// CBUUID(string:) crashes on malformed input, so check first.
    private func uuid(_ text: String) -> CBUUID? {
        let t = text.trimmingCharacters(in: .whitespaces)
        if UUID(uuidString: t) != nil { return CBUUID(string: t) }
        let hex = CharacterSet(charactersIn: "0123456789abcdefABCDEF")
        if (t.count == 4 || t.count == 8) && t.unicodeScalars.allSatisfy({ hex.contains($0) }) { return CBUUID(string: t) }
        return nil
    }
}
