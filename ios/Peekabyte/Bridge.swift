import UIKit
import WebKit

struct BridgeError: LocalizedError {
    let text: String
    init(_ text: String) { self.text = text }
    var errorDescription: String? { text }
}

/// Messages between the web app and the phone. The page asks with
/// `webkit.messageHandlers.peeka.postMessage({rid, cmd, ...})` and hears back through
/// `PeekaNative._reply(rid, ok, value)`; news it didn't ask for (Bluetooth packets, download
/// progress, AI tokens) arrives through `PeekaNative._event(name, data)`. See app/js/native.js.
final class Bridge: NSObject, WKScriptMessageHandler {
    weak var webView: WKWebView?
    let ble = BLE()
    let models = ModelStore()
    let llm = LLM()
    let speech = Speech()
    private var wantAwake = false

    static let version = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.0"

    /// Runs before any of the page's scripts, so the web app knows it's inside the iPhone app.
    static var bootstrap: String {
        """
        window.PeekaNative = { platform: 'ios', version: '\(version)', queue: [],
          _event(name, data) { this.queue.push([name, data]); }, _reply() {} };
        """
    }

    override init() {
        super.init()
        ble.emit = { [weak self] name, data in self?.emit(name, data) }
        models.emit = { [weak self] name, data in
            self?.emit(name, data)
            self?.updateIdleTimer()
        }
        NotificationCenter.default.addObserver(self, selector: #selector(memoryWarning),
                                               name: UIApplication.didReceiveMemoryWarningNotification, object: nil)
    }

    func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {
        guard message.frameInfo.securityOrigin.host == Shell.home.host,
              let body = message.body as? [String: Any], let cmd = body["cmd"] as? String else { return }
        let rid = (body["rid"] as? NSNumber)?.intValue ?? 0   // "id" is often a device id
        handle(cmd, body) { [weak self] result in
            switch result {
            case .success(let value): self?.reply(rid, ok: true, value)
            case .failure(let error): self?.reply(rid, ok: false, error.localizedDescription)
            }
        }
    }

    private func handle(_ cmd: String, _ a: [String: Any], done: @escaping (Result<Any, Error>) -> Void) {
        func str(_ k: String) -> String { a[k] as? String ?? "" }
        func num(_ k: String, _ fallback: Double) -> Double { (a[k] as? NSNumber)?.doubleValue ?? fallback }
        func flag(_ k: String) -> Bool { (a[k] as? NSNumber)?.boolValue ?? false }

        switch cmd {
        case "app.info":
            done(.success(appInfo()))
        case "app.keepAwake":
            wantAwake = flag("on")
            updateIdleTimer()
            done(.success(true))

        case "ble.state":
            done(.success(ble.stateName))
        case "ble.scan":
            ble.scan(flag("on"), service: str("service"))
            done(.success(true))
        case "ble.known":
            ble.known { r in done(r.map { $0 as Any }) }
        case "ble.connect":
            ble.connect(id: str("id"), service: str("service")) { r in done(r.map { $0 as Any }) }
        case "ble.disconnect":
            ble.disconnect(id: str("id"))
            done(.success(true))
        case "ble.subscribe":
            ble.subscribe(id: str("id"), service: str("service"), char: str("char"), on: flag("on")) { r in done(r.map { $0 as Any }) }
        case "ble.write":
            guard let data = Data(base64Encoded: str("data")) else { return done(.failure(BridgeError("Bad data"))) }
            ble.write(id: str("id"), service: str("service"), char: str("char"), data: data, response: flag("response")) { r in done(r.map { $0 as Any }) }

        case "llm.files":
            done(.success(["files": models.files(), "free": models.freeSpace(), "loaded": llm.loadedFile]))
        case "llm.download":
            guard let url = URL(string: str("url")), url.scheme == "https" else { return done(.failure(BridgeError("Bad link"))) }
            do {
                try models.download(url: url, file: str("file"), bytes: Int64(num("bytes", 0)))
                done(.success(true))
            } catch { done(.failure(error)) }
        case "llm.cancel":
            models.cancel(file: str("file"))
            done(.success(true))
        case "llm.delete":
            let file = str("file")
            if llm.loadedFile == file { llm.unload() }
            do { try models.delete(file: file); done(.success(true)) } catch { done(.failure(error)) }
        case "llm.load":
            guard let path = models.path(str("file")), FileManager.default.fileExists(atPath: path.path) else {
                return done(.failure(BridgeError("That brain isn't downloaded yet.")))
            }
            llm.load(path: path.path, contextSize: Int(num("ctx", 1024))) { r in done(r.map { $0 as Any }) }
        case "llm.generate":
            let id = (a["gen"] as? NSNumber)?.intValue ?? 0
            let raw = a["messages"] as? [[String: Any]] ?? []
            let messages = raw.map { ["role": $0["role"] as? String ?? "user", "content": $0["content"] as? String ?? ""] }
            llm.generate(messages: messages, maxTokens: Int(num("maxTokens", 40)), temperature: Float(num("temperature", 0.8)),
                         topP: Float(num("topP", 0.9)), repeatPenalty: Float(num("repeatPenalty", 1.1)),
                         onText: { [weak self] piece in self?.emit("llm.token", ["gen": id, "text": piece]) },
                         done: { r in done(r.map { $0 as Any }) })
        case "llm.stop":
            llm.stop()
            done(.success(true))
        case "llm.unload":
            llm.unload()
            done(.success(true))

        case "tts.state":
            done(.success(["loaded": speech.loadedModel]))
        case "tts.load":
            guard let model = models.path(str("model")), let voices = models.path(str("voices")),
                  FileManager.default.fileExists(atPath: model.path), FileManager.default.fileExists(atPath: voices.path) else {
                return done(.failure(BridgeError("The natural voice isn't downloaded yet.")))
            }
            speech.load(model: model, voices: voices) { r in done(r.map { $0 as Any }) }
        case "tts.speak":
            speech.speak(text: str("text"), speaker: Int(num("sid", 3)), speed: Float(num("speed", 1))) { r in done(r.map { $0 as Any }) }
        case "tts.unload":
            speech.unload()
            done(.success(true))

        default:
            done(.failure(BridgeError("Unknown request \(cmd)")))
        }
    }

    // MARK: talking to the page

    func emit(_ name: String, _ data: Any) {
        run("window.PeekaNative&&PeekaNative._event(\(json(name)),\(json(data)))")
    }

    private func reply(_ id: Int, ok: Bool, _ value: Any) {
        run("window.PeekaNative&&PeekaNative._reply(\(id),\(ok),\(json(value)))")
    }

    private func run(_ script: String) {
        if Thread.isMainThread {
            webView?.evaluateJavaScript(script, completionHandler: nil)
        } else {
            DispatchQueue.main.async { self.webView?.evaluateJavaScript(script, completionHandler: nil) }
        }
    }

    private func json(_ value: Any) -> String {
        guard JSONSerialization.isValidJSONObject([value]),
              let data = try? JSONSerialization.data(withJSONObject: [value]),
              let text = String(data: data, encoding: .utf8) else { return "null" }
        return String(text.dropFirst().dropLast())   // [value] -> value
    }

    // MARK: housekeeping

    func pageRestarted() {}

    private func updateIdleTimer() {
        // Stay awake while the page asks for it, and during big downloads.
        UIApplication.shared.isIdleTimerDisabled = wantAwake || models.isDownloading
    }

    /// iOS is short on memory. Letting go of the AI now keeps the whole app (and the Bluetooth
    /// link) from being closed; the page reloads the brain the next time it needs it.
    @objc private func memoryWarning() {
        guard llm.isLoaded else { return }
        llm.stop()
        llm.unload()
        emit("llm.unloaded", ["reason": "memory"])
    }

    private func appInfo() -> [String: Any] {
        var sys = utsname()
        uname(&sys)
        let device = withUnsafeBytes(of: &sys.machine) { raw in
            String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self)
        }
        return [
            "version": Self.version, "device": device, "ios": UIDevice.current.systemVersion,
            "memory": Double(ProcessInfo.processInfo.physicalMemory), "free": models.freeSpace(),
        ]
    }
}
