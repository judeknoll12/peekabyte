import AVFoundation

/// Every sound the pet makes in the iPhone app is played here, by the app itself, instead of by
/// the web page: an app's own audio needs no tap to get going, plays with the ring switch on
/// silent, and carries on after calls, Siri and the microphone. None of that is guaranteed for
/// web audio inside an app, which is how the pet used to go quiet.
///
/// The page hands over finished audio (16-bit PCM: the natural voice, babble, sound effects) or
/// text for the phone's own voice (AVSpeechSynthesizer), and hears back when each one starts,
/// when a word is spoken and when it has finished. Main queue only.
final class Sound: NSObject, AVSpeechSynthesizerDelegate {
    var emit: ((String, [String: Any]) -> Void)?

    /// An engine with its players. iOS stops the engine whenever the audio setup changes (the
    /// microphone, headphones, a call); a fresh one is built for the next sound, which is
    /// simpler and safer than rewiring a stopped one.
    private final class Graph {
        let engine = AVAudioEngine()
        let voice = AVAudioPlayerNode()      // the pet's lines, one at a time
        let loop = AVAudioPlayerNode()       // the purr while you pet it
        var sfx: [AVAudioPlayerNode] = []    // sound effects, several at once
        var nextSfx = 0

        init(format: AVAudioFormat) {
            for node in [voice, loop] {
                engine.attach(node)
                engine.connect(node, to: engine.mainMixerNode, format: format)
            }
            for _ in 0..<4 {
                let node = AVAudioPlayerNode()
                engine.attach(node)
                engine.connect(node, to: engine.mainMixerNode, format: format)
                sfx.append(node)
            }
        }

        var players: [AVAudioPlayerNode] { [voice, loop] + sfx }
    }

    private let format = AVAudioFormat(standardFormatWithSampleRate: 24000, channels: 1)!
    private var graph: Graph?
    private let synth = AVSpeechSynthesizer()
    // Lines being spoken, kept alive until they finish so an id can't be mixed up with a newer line's.
    private var utterances: [ObjectIdentifier: (utterance: AVSpeechUtterance, id: Int)] = [:]
    private(set) var lastError = ""

    override init() {
        super.init()
        synth.delegate = self
        NotificationCenter.default.addObserver(self, selector: #selector(interruption(_:)),
                                               name: AVAudioSession.interruptionNotification, object: nil)
    }

    // MARK: the page's requests

    /// Plays 16-bit little-endian mono PCM. Returns its length in seconds.
    func play(id: Int, pcm: Data, rate: Double, volume: Float, channel: String, loop: Bool) throws -> Double {
        guard let buffer = makeBuffer(pcm, rate: rate) else { throw BridgeError("Couldn't read that sound") }
        let g = try ready()
        let node: AVAudioPlayerNode
        switch channel {
        case "voice":
            node = g.voice
            if synth.isSpeaking { synth.stopSpeaking(at: .immediate) }
            node.stop()   // one line at a time; the one cut short reports that it ended
        case "loop":
            node = g.loop
            node.stop()
        default:
            node = g.sfx[g.nextSfx]
            g.nextSfx = (g.nextSfx + 1) % g.sfx.count
        }
        node.volume = max(0, min(1, volume))
        node.scheduleBuffer(buffer, at: nil, options: loop ? [.loops] : [], completionCallbackType: .dataPlayedBack) { [weak self] _ in
            DispatchQueue.main.async { self?.emit?("audio.ended", ["id": id]) }
        }
        node.play()
        emit?("audio.started", ["id": id])
        return Double(buffer.frameLength) / buffer.format.sampleRate
    }

    /// Says text with the phone's own voice. `voice` is a voice identifier (empty: the best
    /// English voice on the phone); `rate` and `pitch` are multipliers.
    func say(id: Int, text: String, voice: String, rate: Float, pitch: Float, volume: Float) throws {
        let g = try ready()
        g.voice.stop()
        if synth.isSpeaking { synth.stopSpeaking(at: .immediate) }
        let utterance = AVSpeechUtterance(string: text)
        utterance.voice = (voice.isEmpty ? nil : AVSpeechSynthesisVoice(identifier: voice)) ?? Self.bestVoice()
        utterance.rate = min(AVSpeechUtteranceMaximumSpeechRate,
                             max(AVSpeechUtteranceMinimumSpeechRate, AVSpeechUtteranceDefaultSpeechRate * rate))
        utterance.pitchMultiplier = min(2, max(0.5, pitch))
        utterance.volume = min(1, max(0, volume))
        utterances[ObjectIdentifier(utterance)] = (utterance, id)
        synth.speak(utterance)
    }

    func stop(channel: String) {
        switch channel {
        case "voice":
            graph?.voice.stop()
            if synth.isSpeaking { synth.stopSpeaking(at: .immediate) }
        case "loop":
            graph?.loop.stop()
        default:
            graph?.sfx.forEach { $0.stop() }
        }
    }

    /// Where the sound goes and how loud the phone is, for the settings page.
    func state() -> [String: Any] {
        _ = try? ready()
        let session = AVAudioSession.sharedInstance()
        let out = session.currentRoute.outputs.first
        let best = Self.bestVoice()
        return [
            "route": out?.portName ?? "", "port": out?.portType.rawValue ?? "",
            "volume": Double(session.outputVolume), "otherAudio": session.isOtherAudioPlaying,
            "running": graph?.engine.isRunning ?? false, "error": lastError,
            "bestVoice": best?.name ?? "", "bestQuality": best.map { Self.quality($0) } ?? "",
        ]
    }

    /// The phone's English voices, best first.
    func voices() -> [[String: Any]] {
        AVSpeechSynthesisVoice.speechVoices()
            .filter { $0.language.hasPrefix("en") && !Self.isPersonal($0) }
            .sorted { Self.score($0) > Self.score($1) }
            .map { ["id": $0.identifier, "name": $0.name, "lang": $0.language, "quality": Self.quality($0), "novelty": Self.isNovelty($0)] }
    }

    // MARK: audio

    private func ready() throws -> Graph {
        do {
            let session = AVAudioSession.sharedInstance()
            if session.category != .playback && session.category != .playAndRecord {
                try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            }
            try session.setActive(true)
            let g: Graph
            if let current = graph {
                g = current
            } else {
                g = Graph(format: format)
                graph = g
                NotificationCenter.default.addObserver(self, selector: #selector(configurationChanged(_:)),
                                                       name: .AVAudioEngineConfigurationChange, object: g.engine)
            }
            if !g.engine.isRunning {
                g.engine.prepare()
                try g.engine.start()
            }
            lastError = ""
            return g
        } catch {
            lastError = error.localizedDescription
            throw BridgeError("The iPhone wouldn't play sound: \(error.localizedDescription)")
        }
    }

    /// The engine stopped by itself: let this one go (telling the page its sounds ended) and
    /// build a new one for the next sound.
    @objc private func configurationChanged(_ note: Notification) {
        DispatchQueue.main.async { [weak self] in
            guard let self, let g = self.graph, (note.object as AnyObject?) === g.engine else { return }
            NotificationCenter.default.removeObserver(self, name: .AVAudioEngineConfigurationChange, object: g.engine)
            self.graph = nil
            g.players.forEach { $0.stop() }
            g.engine.stop()
        }
    }

    @objc private func interruption(_ note: Notification) {
        guard let raw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
              AVAudioSession.InterruptionType(rawValue: raw) == .ended else { return }
        DispatchQueue.main.async { _ = try? self.ready() }
    }

    private func makeBuffer(_ pcm: Data, rate: Double) -> AVAudioPCMBuffer? {
        let frames = pcm.count / 2
        guard frames > 0, rate >= 4000, rate <= 192_000,
              let source = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 1),
              let buffer = AVAudioPCMBuffer(pcmFormat: source, frameCapacity: AVAudioFrameCount(frames)),
              let out = buffer.floatChannelData?[0] else { return nil }
        buffer.frameLength = AVAudioFrameCount(frames)
        pcm.withUnsafeBytes { raw in
            for i in 0..<frames {
                out[i] = Float(Int16(littleEndian: raw.loadUnaligned(fromByteOffset: i * 2, as: Int16.self))) / 32768
            }
        }
        if rate == format.sampleRate { return buffer }
        // Anything else is resampled to the players' rate.
        guard let converter = AVAudioConverter(from: source, to: format),
              let converted = AVAudioPCMBuffer(pcmFormat: format,
                                               frameCapacity: AVAudioFrameCount(Double(frames) * format.sampleRate / rate) + 64) else { return nil }
        var fed = false
        var problem: NSError?
        converter.convert(to: converted, error: &problem) { _, status in
            if fed {
                status.pointee = .endOfStream
                return nil
            }
            fed = true
            status.pointee = .haveData
            return buffer
        }
        return problem == nil && converted.frameLength > 0 ? converted : nil
    }

    // MARK: the phone's voices

    static func bestVoice() -> AVSpeechSynthesisVoice? {
        AVSpeechSynthesisVoice.speechVoices()
            .filter { $0.language.hasPrefix("en") && !isPersonal($0) && !isNovelty($0) }
            .max { score($0) < score($1) } ?? AVSpeechSynthesisVoice(language: "en-US")
    }

    private static func score(_ v: AVSpeechSynthesisVoice) -> Int {
        var s = v.quality.rawValue * 10   // default 1, enhanced 2, premium 3
        let here = AVSpeechSynthesisVoice.currentLanguageCode()
        if v.language == here { s += 5 } else if v.language == "en-US" { s += 3 }
        if isNovelty(v) { s -= 100 }
        return s
    }

    private static func quality(_ v: AVSpeechSynthesisVoice) -> String {
        switch v.quality {
        case .premium: return "premium"
        case .enhanced: return "enhanced"
        default: return "default"
        }
    }

    private static func isNovelty(_ v: AVSpeechSynthesisVoice) -> Bool {
        if #available(iOS 17, *) { return v.voiceTraits.contains(.isNoveltyVoice) }
        return false
    }

    private static func isPersonal(_ v: AVSpeechSynthesisVoice) -> Bool {
        if #available(iOS 17, *) { return v.voiceTraits.contains(.isPersonalVoice) }
        return false
    }

    // MARK: AVSpeechSynthesizerDelegate

    func speechSynthesizer(_ synthesizer: AVSpeechSynthesizer, didStart utterance: AVSpeechUtterance) {
        tell("audio.started", utterance, done: false)
    }

    func speechSynthesizer(_ synthesizer: AVSpeechSynthesizer, willSpeakRangeOfSpeechString characterRange: NSRange,
                           utterance: AVSpeechUtterance) {
        tell("audio.word", utterance, done: false)
    }

    func speechSynthesizer(_ synthesizer: AVSpeechSynthesizer, didFinish utterance: AVSpeechUtterance) {
        tell("audio.ended", utterance, done: true)
    }

    func speechSynthesizer(_ synthesizer: AVSpeechSynthesizer, didCancel utterance: AVSpeechUtterance) {
        tell("audio.ended", utterance, done: true)
    }

    private func tell(_ name: String, _ utterance: AVSpeechUtterance, done: Bool) {
        let key = ObjectIdentifier(utterance)
        DispatchQueue.main.async { [weak self] in
            guard let self, let entry = self.utterances[key] else { return }
            if done { self.utterances.removeValue(forKey: key) }
            self.emit?(name, ["id": entry.id])
        }
    }
}
