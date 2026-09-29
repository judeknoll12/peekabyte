import Foundation
import SherpaOnnxC

/// The pet's natural voice: Kokoro v1.0 (open source, Apache 2.0) through sherpa-onnx, on the
/// iPhone's CPU. Its pronunciation data ships inside the app (Voice/); the model itself is
/// downloaded once through ModelStore. Every sherpa-onnx call happens on one serial queue.
final class Speech {
    private let queue = DispatchQueue(label: "peekabyte.tts", qos: .userInitiated)
    private var tts: OpaquePointer?
    private var model = ""
    private var slowness = 0.0   // seconds of work per second of speech

    private let lock = NSLock()
    private var loaded = ""
    var loadedModel: String { lock.lock(); defer { lock.unlock() }; return loaded }

    func load(model: URL, voices: URL, done: @escaping (Result<[String: Any], Error>) -> Void) {
        queue.async {
            let result = Result { try self.loadNow(model: model, voices: voices) }
            DispatchQueue.main.async { done(result) }
        }
    }

    func speak(text: String, speaker: Int, speed: Float, done: @escaping (Result<[String: Any], Error>) -> Void) {
        queue.async {
            let result = Result { () -> [String: Any] in
                let started = Date()
                let audio = try self.render(text, speaker: speaker, speed: speed)
                // 16-bit PCM keeps the trip to the page small; the page plays it with its effects.
                var pcm = Data(count: audio.samples.count * 2)
                pcm.withUnsafeMutableBytes { raw in
                    let out = raw.bindMemory(to: Int16.self)
                    for (i, s) in audio.samples.enumerated() {
                        out[i] = Int16(max(-1, min(1, s)) * 32767).littleEndian
                    }
                }
                return ["pcm": pcm.base64EncodedString(), "rate": audio.rate,
                        "ms": Int(Date().timeIntervalSince(started) * 1000)]
            }
            DispatchQueue.main.async { done(result) }
        }
    }

    func unload() {
        queue.async { self.freeNow() }
    }

    // MARK: on the queue

    private func loadNow(model modelURL: URL, voices: URL) throws -> [String: Any] {
        if tts != nil && model == modelURL.lastPathComponent { return ["ms": 0, "slowness": slowness] }
        freeNow()
        guard let data = Bundle.main.url(forResource: "Voice", withExtension: nil) else {
            throw BridgeError("The voice's pronunciation data is missing from the app.")
        }
        let started = Date()
        var strings: [UnsafeMutablePointer<CChar>?] = []
        func text(_ s: String) -> UnsafePointer<CChar>? {
            let p = strdup(s)
            strings.append(p)
            return UnsafePointer(p)
        }
        defer { strings.forEach { free($0) } }

        var config = SherpaOnnxOfflineTtsConfig()
        config.model.kokoro.model = text(modelURL.path)
        config.model.kokoro.voices = text(voices.path)
        config.model.kokoro.tokens = text(data.appendingPathComponent("tokens.txt").path)
        config.model.kokoro.data_dir = text(data.appendingPathComponent("espeak-ng-data").path)
        config.model.kokoro.lexicon = text(data.appendingPathComponent("lexicon-us-en.txt").path)
        config.model.kokoro.lang = text("en-us")
        config.model.kokoro.length_scale = 1.0
        // iPhones have two fast cores and several slow ones; spreading the work onto the slow
        // ones makes every step wait for them, so two threads is quicker than four.
        config.model.num_threads = 2
        config.model.provider = text("cpu")
        config.model.debug = 0
        config.max_num_sentences = 1
        guard let created = SherpaOnnxCreateOfflineTts(&config) else {
            throw BridgeError("Couldn't start the natural voice. Delete it and download it again.")
        }
        tts = created
        model = modelURL.lastPathComponent
        lock.lock(); loaded = model; lock.unlock()

        // The first sentence is always slow; get it out of the way, and measure the speed.
        let warm = Date()
        let sample = try render("Hi there.", speaker: 3, speed: 1)
        slowness = sample.seconds > 0 ? Date().timeIntervalSince(warm) / sample.seconds : 0
        return [
            "ms": Int(Date().timeIntervalSince(started) * 1000), "slowness": slowness,
            "rate": Int(SherpaOnnxOfflineTtsSampleRate(created)),
        ]
    }

    private func render(_ text: String, speaker: Int, speed: Float) throws -> (samples: [Float], rate: Int, seconds: Double) {
        guard let tts else { throw BridgeError("The natural voice isn't loaded.") }
        var gen = SherpaOnnxGenerationConfig()
        gen.sid = Int32(speaker)
        gen.speed = speed
        gen.silence_scale = 0.2
        guard let audio = SherpaOnnxOfflineTtsGenerateWithConfig(tts, text, &gen, nil, nil) else {
            throw BridgeError("The voice couldn't say that.")
        }
        defer { SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio) }
        let count = Int(audio.pointee.n)
        let rate = Int(audio.pointee.sample_rate)
        guard count > 0, rate > 0, let samples = audio.pointee.samples else {
            throw BridgeError("The voice made no sound.")
        }
        return (Array(UnsafeBufferPointer(start: samples, count: count)), rate, Double(count) / Double(rate))
    }

    private func freeNow() {
        if let tts { SherpaOnnxDestroyOfflineTts(tts) }
        tts = nil
        model = ""
        lock.lock(); loaded = ""; lock.unlock()
    }
}
