import AVFoundation
import Speech

/// Talking to the pet: the iPhone's speech recognizer turns what you say into words, on the
/// phone itself whenever it can (iPhones from the XS on can, for English), so your voice
/// doesn't leave the phone. One turn at a time: the page starts listening, the words stream
/// back as you speak, and the turn ends by itself when you pause. Everything here runs on
/// the main queue except the microphone tap.
final class Listen {
    var emit: ((String, [String: Any]) -> Void)?

    private let recognizer = SFSpeechRecognizer(locale: Locale(identifier: "en-US")) ?? SFSpeechRecognizer()
    private var engine: AVAudioEngine?
    private var request: SFSpeechAudioBufferRecognitionRequest?
    private var task: SFSpeechRecognitionTask?
    private var watchdog: Timer?
    private var releaseTimer: Timer?

    private var finished = true
    private var text = ""
    private var startedAt = Date()
    private var heardAt: Date?          // the last time the words changed
    private var endedAt: Date?          // stopped listening, waiting for the final words
    private var silence: TimeInterval = 1.3
    private var wait: TimeInterval = 8
    private var longest: TimeInterval = 20
    private var lastLevel = Date.distantPast   // touched on the audio thread only

    var state: [String: Any] {
        var s: [String: Any] = [
            "available": recognizer != nil,
            "onDevice": recognizer?.supportsOnDeviceRecognition ?? false,
            "permission": permission,
        ]
        if recognizer == nil { s["why"] = "Speech recognition isn't available on this iPhone." }
        return s
    }

    private var permission: String {
        let speech = SFSpeechRecognizer.authorizationStatus()
        let mic = AVAudioSession.sharedInstance().recordPermission
        if speech == .denied || speech == .restricted || mic == .denied { return "denied" }
        if speech == .authorized && mic == .granted { return "granted" }
        return "ask"
    }

    // MARK: the page's requests

    func start(hints: [String], silence: Double, wait: Double, longest: Double,
               done: @escaping (Result<Any, Error>) -> Void) {
        self.silence = max(0.6, min(4, silence))
        self.wait = max(2, min(20, wait))
        self.longest = max(3, min(60, longest))
        askPermission { [weak self] problem in
            guard let self else { return }
            if let problem { return done(.failure(problem)) }
            do {
                try self.begin(hints: hints)
                done(.success(true))
            } catch {
                self.finished = true
                self.teardown()
                done(.failure(error))
            }
        }
    }

    /// Done talking: use what was heard so far.
    func stop() {
        guard !finished else { return }
        if text.isEmpty { return finish() }
        endAudio()
    }

    /// Throw away this turn.
    func cancel() {
        guard !finished else { return }
        finished = true
        task?.cancel()
        teardown()
        releaseLater()
    }

    /// Hand the phone's audio back to plain playback (so music and the pet's voice sound their
    /// best) once the conversation is over.
    func release() {
        releaseTimer?.invalidate()
        releaseTimer = nil
        guard finished else { return }
        try? AVAudioSession.sharedInstance().setCategory(.playback, mode: .default, options: [.mixWithOthers])
    }

    // MARK: listening

    private func askPermission(_ then: @escaping (Error?) -> Void) {
        SFSpeechRecognizer.requestAuthorization { status in
            DispatchQueue.main.async {
                guard status == .authorized else {
                    return then(BridgeError("Speech recognition permission was denied"))
                }
                AVAudioSession.sharedInstance().requestRecordPermission { granted in
                    DispatchQueue.main.async {
                        then(granted ? nil : BridgeError("Microphone permission was denied"))
                    }
                }
            }
        }
    }

    private func begin(hints: [String]) throws {
        guard let recognizer, recognizer.isAvailable else {
            throw BridgeError("Speech recognition isn't available right now. Try again in a moment.")
        }
        if !finished { task?.cancel() }
        teardown()
        releaseTimer?.invalidate()

        let session = AVAudioSession.sharedInstance()
        try session.setCategory(.playAndRecord, mode: .default, options: [.defaultToSpeaker, .allowBluetoothA2DP, .mixWithOthers])
        try session.setActive(true)

        let request = SFSpeechAudioBufferRecognitionRequest()
        request.shouldReportPartialResults = true
        request.requiresOnDeviceRecognition = recognizer.supportsOnDeviceRecognition
        request.contextualStrings = Array(hints.filter { !$0.isEmpty }.prefix(50))
        request.taskHint = .dictation
        request.addsPunctuation = true

        let engine = AVAudioEngine()
        let input = engine.inputNode
        let format = input.outputFormat(forBus: 0)
        guard format.sampleRate > 0, format.channelCount > 0 else { throw BridgeError("The microphone isn't available") }
        input.installTap(onBus: 0, bufferSize: 1024, format: format) { [weak self] buffer, _ in
            request.append(buffer)
            self?.measure(buffer)
        }
        engine.prepare()
        try engine.start()

        self.engine = engine
        self.request = request
        text = ""
        heardAt = nil
        endedAt = nil
        startedAt = Date()
        finished = false
        task = recognizer.recognitionTask(with: request) { [weak self] result, error in
            DispatchQueue.main.async { self?.recognized(result, error) }
        }
        watchdog = Timer.scheduledTimer(withTimeInterval: 0.15, repeats: true) { [weak self] _ in self?.check() }
    }

    private func recognized(_ result: SFSpeechRecognitionResult?, _ error: Error?) {
        guard !finished else { return }
        if let result {
            let words = result.bestTranscription.formattedString
            if words != text {
                text = words
                heardAt = Date()
                emit?("mic.partial", ["text": words])
            }
            if result.isFinal { return finish() }
        }
        guard let error else { return }
        let code = (error as NSError).code
        // "No speech detected" and "cancelled" just mean the turn is over.
        if !text.isEmpty || code == 1110 || code == 216 || code == 301 { return finish() }
        fail("Couldn't understand that: \(error.localizedDescription)")
    }

    private func check() {
        guard !finished else { return }
        let now = Date()
        if let endedAt {
            if now.timeIntervalSince(endedAt) > 2 { finish() }   // the final words never came
            return
        }
        if let heardAt {
            if now.timeIntervalSince(heardAt) > silence { endAudio() }   // you paused: that's your turn
        } else if now.timeIntervalSince(startedAt) > wait {
            finish()   // nothing said
        }
        if now.timeIntervalSince(startedAt) > longest { endAudio() }
    }

    /// How loud you are, for the pet's "all ears" look and the pulsing button. Audio thread.
    private func measure(_ buffer: AVAudioPCMBuffer) {
        let now = Date()
        guard now.timeIntervalSince(lastLevel) > 0.1, let samples = buffer.floatChannelData?[0] else { return }
        let n = Int(buffer.frameLength)
        guard n > 0 else { return }
        lastLevel = now
        var sum: Float = 0
        for i in 0..<n { sum += samples[i] * samples[i] }
        let db = 20 * log10(max(sqrt(sum / Float(n)), 1e-6))
        let level = Double(max(0, min(1, (db + 50) / 40)))   // -50 dB .. -10 dB
        DispatchQueue.main.async { [weak self] in
            guard let self, !self.finished, self.endedAt == nil else { return }
            self.emit?("mic.level", ["level": level])
        }
    }

    private func endAudio() {
        guard endedAt == nil else { return }
        endedAt = Date()
        stopMicrophone()
        request?.endAudio()
    }

    private func finish() {
        guard !finished else { return }
        finished = true
        let words = text
        task?.cancel()   // no-op once the final words are in
        teardown()
        emit?("mic.final", ["text": words])
        releaseLater()
    }

    private func fail(_ message: String) {
        guard !finished else { return }
        finished = true
        task?.cancel()
        teardown()
        emit?("mic.error", ["message": message])
        releaseLater()
    }

    private func stopMicrophone() {
        guard let engine else { return }
        engine.inputNode.removeTap(onBus: 0)
        if engine.isRunning { engine.stop() }
        self.engine = nil
    }

    private func teardown() {
        watchdog?.invalidate()
        watchdog = nil
        stopMicrophone()
        task = nil
        request = nil
    }

    private func releaseLater() {
        releaseTimer?.invalidate()
        releaseTimer = Timer.scheduledTimer(withTimeInterval: 45, repeats: false) { [weak self] _ in self?.release() }
    }
}
