import Foundation
import llama

/// An open-source language model (a GGUF file) running on the iPhone's GPU through llama.cpp.
/// Every llama.cpp call happens on one serial queue; results come back on the main queue.
final class LLM {
    private let queue = DispatchQueue(label: "peekabyte.llm", qos: .userInitiated)
    private var model: OpaquePointer?
    private var context: OpaquePointer?
    private var vocab: OpaquePointer?
    private var file = ""
    private static var backendReady = false

    private let lock = NSLock()
    private var loaded = ""
    private var stopping = false

    var isLoaded: Bool { !loadedFile.isEmpty }
    var loadedFile: String { lock.lock(); defer { lock.unlock() }; return loaded }
    private var shouldStop: Bool { lock.lock(); defer { lock.unlock() }; return stopping }

    func load(path: String, contextSize: Int, done: @escaping (Result<[String: Any], Error>) -> Void) {
        queue.async {
            let result = Result { try self.loadNow(path: path, contextSize: contextSize) }
            DispatchQueue.main.async { done(result) }
        }
    }

    func unload() {
        queue.async { self.freeNow() }
    }

    func stop() {
        lock.lock(); stopping = true; lock.unlock()
    }

    func generate(messages: [[String: String]], maxTokens: Int, temperature: Float, topP: Float, repeatPenalty: Float,
                  onText: @escaping (String) -> Void, done: @escaping (Result<[String: Any], Error>) -> Void) {
        lock.lock(); stopping = false; lock.unlock()
        queue.async {
            let result = Result {
                try self.generateNow(messages: messages, maxTokens: maxTokens, temperature: temperature,
                                     topP: topP, repeatPenalty: repeatPenalty, onText: onText)
            }
            DispatchQueue.main.async { done(result) }
        }
    }

    // MARK: on the queue

    private func loadNow(path: String, contextSize: Int) throws -> [String: Any] {
        let name = (path as NSString).lastPathComponent
        if context != nil && file == name { return describe(ms: 0) }
        freeNow()
        if !LLM.backendReady {
            llama_backend_init()
            LLM.backendReady = true
        }
        let started = Date()
        var modelParams = llama_model_default_params()
        modelParams.n_gpu_layers = 999   // every layer on the GPU (Metal)
        guard let m = llama_model_load_from_file(path, modelParams) else {
            throw BridgeError("Couldn't open the brain file. Delete it and download it again.")
        }
        var contextParams = llama_context_default_params()
        contextParams.n_ctx = UInt32(contextSize)
        contextParams.n_batch = UInt32(min(contextSize, 512))
        let threads = Int32(max(1, min(4, ProcessInfo.processInfo.activeProcessorCount - 2)))
        contextParams.n_threads = threads
        contextParams.n_threads_batch = threads
        guard let c = llama_init_from_model(m, contextParams) else {
            llama_model_free(m)
            throw BridgeError("Not enough memory to start this brain. Try a smaller one.")
        }
        model = m
        context = c
        vocab = llama_model_get_vocab(m)
        file = name
        lock.lock(); loaded = name; lock.unlock()
        return describe(ms: Date().timeIntervalSince(started) * 1000)
    }

    private func freeNow() {
        if let context { llama_free(context) }
        if let model { llama_model_free(model) }
        context = nil
        model = nil
        vocab = nil
        file = ""
        lock.lock(); loaded = ""; lock.unlock()
    }

    private func describe(ms: Double) -> [String: Any] {
        guard let model else { return [:] }
        var buf = [CChar](repeating: 0, count: 256)
        let n = llama_model_desc(model, &buf, buf.count)
        return [
            "file": file, "desc": text(buf, Int(max(0, n))), "ms": Int(ms),
            "params": Double(llama_model_n_params(model)), "bytes": Double(llama_model_size(model)),
        ]
    }

    private func generateNow(messages: [[String: String]], maxTokens: Int, temperature: Float, topP: Float,
                             repeatPenalty: Float, onText: (String) -> Void) throws -> [String: Any] {
        guard let model, let context, let vocab else { throw BridgeError("The brain isn't loaded.") }
        let started = Date()
        guard let prompt = format(model, messages) else { throw BridgeError("Couldn't format the conversation.") }
        var tokens = tokenize(vocab, prompt)
        if tokens.isEmpty { throw BridgeError("Nothing to say.") }
        if tokens.count > Int(llama_n_ctx(context)) - maxTokens - 8 {
            throw BridgeError("The conversation got too long for this brain.")
        }

        // Every request carries the whole conversation, so start from a clean slate.
        llama_memory_clear(llama_get_memory(context), true)
        let batchSize = Int(llama_n_batch(context))
        var start = 0
        while start < tokens.count {
            let n = min(batchSize, tokens.count - start)
            let rc = tokens.withUnsafeMutableBufferPointer { buf -> Int32 in
                llama_decode(context, llama_batch_get_one(buf.baseAddress! + start, Int32(n)))
            }
            if rc != 0 { throw BridgeError("The brain couldn't read the prompt (code \(rc)).") }
            start += n
        }
        let promptDone = Date()

        guard let sampler = llama_sampler_chain_init(llama_sampler_chain_default_params()) else {
            throw BridgeError("Couldn't start the brain's sampler.")
        }
        defer { llama_sampler_free(sampler) }
        llama_sampler_chain_add(sampler, llama_sampler_init_penalties(llama_vocab_n_tokens(vocab), 64, repeatPenalty, 0, 0))
        llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40))
        llama_sampler_chain_add(sampler, llama_sampler_init_top_p(topP, 1))
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(temperature))
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(UInt32.random(in: 0...UInt32.max)))

        var output = ""
        var partial: [CChar] = []   // bytes of a character split across tokens
        var produced = 0
        while produced < maxTokens && !shouldStop {
            var token = llama_sampler_sample(sampler, context, -1)
            if llama_vocab_is_eog(vocab, token) { break }
            partial += piece(vocab, token)
            if let s = String(validatingUTF8: partial + [0]) {
                partial.removeAll()
                output += s
                onText(s)
            }
            produced += 1
            if llama_decode(context, llama_batch_get_one(&token, 1)) != 0 { break }
        }
        let finished = Date()
        return [
            "text": output, "tokens": produced, "promptTokens": tokens.count,
            "promptMs": Int(promptDone.timeIntervalSince(started) * 1000),
            "ms": Int(finished.timeIntervalSince(started) * 1000),
        ]
    }

    /// Lays the conversation out the way this model was trained to read it (its chat template).
    private func format(_ model: OpaquePointer, _ messages: [[String: String]]) -> String? {
        let roles = messages.map { strdup($0["role"] ?? "user") }
        let contents = messages.map { strdup($0["content"] ?? "") }
        defer {
            roles.forEach { free($0) }
            contents.forEach { free($0) }
        }
        var chat: [llama_chat_message] = []
        for i in 0..<messages.count {
            chat.append(llama_chat_message(role: UnsafePointer(roles[i]), content: UnsafePointer(contents[i])))
        }
        let template = llama_model_chat_template(model, nil)
        var size = messages.reduce(0) { $0 + ($1["content"]?.utf8.count ?? 0) } * 2 + 1024
        for attempt in 0..<3 {
            var buf = [CChar](repeating: 0, count: size)
            var n: Int32
            if template != nil && attempt < 2 {
                n = llama_chat_apply_template(template, &chat, chat.count, true, &buf, Int32(buf.count))
            } else {
                n = llama_chat_apply_template("chatml", &chat, chat.count, true, &buf, Int32(buf.count))
            }
            if n < 0 && template != nil && attempt < 2 {   // a template llama.cpp doesn't know: use ChatML
                n = llama_chat_apply_template("chatml", &chat, chat.count, true, &buf, Int32(buf.count))
            }
            if n < 0 { return nil }
            if Int(n) <= buf.count { return text(buf, Int(n)) }
            size = Int(n) + 16
        }
        return nil
    }

    private func tokenize(_ vocab: OpaquePointer, _ text: String) -> [llama_token] {
        let bytes = Int32(text.utf8.count)
        var tokens = [llama_token](repeating: 0, count: Int(bytes) + 16)
        var n = llama_tokenize(vocab, text, bytes, &tokens, Int32(tokens.count), true, true)
        if n < 0 {
            tokens = [llama_token](repeating: 0, count: Int(-n))
            n = llama_tokenize(vocab, text, bytes, &tokens, Int32(tokens.count), true, true)
        }
        return Array(tokens.prefix(Int(max(0, n))))
    }

    private func piece(_ vocab: OpaquePointer, _ token: llama_token) -> [CChar] {
        var buf = [CChar](repeating: 0, count: 32)
        var n = llama_token_to_piece(vocab, token, &buf, Int32(buf.count), 0, false)
        if n < 0 {
            buf = [CChar](repeating: 0, count: Int(-n))
            n = llama_token_to_piece(vocab, token, &buf, Int32(buf.count), 0, false)
        }
        return n > 0 ? Array(buf.prefix(Int(n))) : []
    }

    private func text(_ buf: [CChar], _ count: Int) -> String {
        String(decoding: buf.prefix(count).map { UInt8(bitPattern: $0) }, as: UTF8.self)
    }
}
