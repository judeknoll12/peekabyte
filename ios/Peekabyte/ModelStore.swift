import Foundation

/// Downloads the AI brains (GGUF files) and the natural voice (ONNX + voice table) from
/// Hugging Face into the app's own storage, where
/// iOS won't clear them. Interrupted downloads pick up where they left off.
final class ModelStore: NSObject, URLSessionDownloadDelegate {
    var emit: ((String, [String: Any]) -> Void)?

    private lazy var session: URLSession = {
        let config = URLSessionConfiguration.default
        config.timeoutIntervalForRequest = 60
        config.waitsForConnectivity = true
        return URLSession(configuration: config, delegate: self, delegateQueue: .main)
    }()
    private var tasks: [String: URLSessionDownloadTask] = [:]
    private var sources: [String: URL] = [:]
    private var retries: [String: Int] = [:]
    private var lastProgress: [String: Date] = [:]
    private var cancelled: Set<String> = []

    let folder: URL = {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        var dir = base.appendingPathComponent("Brains", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        var values = URLResourceValues()
        values.isExcludedFromBackup = true   // big and re-downloadable: keep them out of iCloud backups
        try? dir.setResourceValues(values)
        return dir
    }()

    var isDownloading: Bool { !tasks.isEmpty }

    static let kinds: Set<String> = ["gguf", "onnx", "bin"]

    /// Only plain file names of the kinds above, so the page can't touch anything else.
    func path(_ file: String) -> URL? {
        let kind = (file as NSString).pathExtension.lowercased()
        guard Self.kinds.contains(kind), !file.contains("/"), !file.contains("\\"), !file.hasPrefix(".") else { return nil }
        return folder.appendingPathComponent(file)
    }

    func files() -> [[String: Any]] {
        let items = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: [.fileSizeKey])) ?? []
        return items.filter { Self.kinds.contains($0.pathExtension.lowercased()) }.map { url in
            let size = (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
            return ["file": url.lastPathComponent, "bytes": Double(size)]
        }
    }

    func freeSpace() -> Double {
        let values = try? folder.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
        return Double(values?.volumeAvailableCapacityForImportantUsage ?? 0)
    }

    func download(url: URL, file: String, bytes: Int64) throws {
        guard let target = path(file) else { throw BridgeError("Bad file name") }
        if FileManager.default.fileExists(atPath: target.path) {
            emit?("llm.downloaded", ["file": file])
            return
        }
        if tasks[file] != nil { return }   // already on its way
        if bytes > 0 && freeSpace() < Double(bytes) + 300_000_000 {
            throw BridgeError("Not enough free space on this iPhone: the brain needs \(bytes / 1_000_000) MB plus a little room.")
        }
        sources[file] = url
        retries[file] = 0
        cancelled.remove(file)
        start(file, resumeFrom: nil)
    }

    func cancel(file: String) {
        cancelled.insert(file)
        tasks.removeValue(forKey: file)?.cancel()
        emit?("llm.failed", ["file": file, "error": "Download cancelled", "cancelled": true])
    }

    func delete(file: String) throws {
        guard let target = path(file) else { throw BridgeError("Bad file name") }
        if FileManager.default.fileExists(atPath: target.path) { try FileManager.default.removeItem(at: target) }
    }

    private func start(_ file: String, resumeFrom data: Data?) {
        let task: URLSessionDownloadTask
        if let data {
            task = session.downloadTask(withResumeData: data)
        } else if let url = sources[file] {
            task = session.downloadTask(with: url)
        } else { return }
        task.taskDescription = file
        tasks[file] = task
        task.resume()
    }

    // MARK: URLSessionDownloadDelegate (on the main queue)

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didWriteData bytesWritten: Int64,
                    totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
        guard let file = downloadTask.taskDescription else { return }
        let now = Date()
        if let last = lastProgress[file], now.timeIntervalSince(last) < 0.25 { return }
        lastProgress[file] = now
        emit?("llm.progress", ["file": file, "loaded": Double(totalBytesWritten), "total": Double(max(0, totalBytesExpectedToWrite))])
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        guard let file = downloadTask.taskDescription, let target = path(file) else { return }
        let status = (downloadTask.response as? HTTPURLResponse)?.statusCode ?? 0
        guard status == 200 || status == 206 else {
            fail(file, "The download server said \(status).")
            return
        }
        do {
            // Must happen before this call returns: iOS deletes `location` right after.
            let part = folder.appendingPathComponent(file + ".part")
            try? FileManager.default.removeItem(at: part)
            try FileManager.default.moveItem(at: location, to: part)
            try? FileManager.default.removeItem(at: target)
            try FileManager.default.moveItem(at: part, to: target)
            tasks.removeValue(forKey: file)
            emit?("llm.downloaded", ["file": file])
        } catch {
            fail(file, "Couldn't save the brain: \(error.localizedDescription)")
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        guard let file = task.taskDescription, let error else { return }
        if cancelled.contains(file) { tasks.removeValue(forKey: file); return }
        let resume = (error as NSError).userInfo[NSURLSessionDownloadTaskResumeData] as? Data
        let tries = (retries[file] ?? 0) + 1
        retries[file] = tries
        if tries <= 8 {
            // Wi-Fi hiccup or the app went to the background: carry on from where it stopped.
            emit?("llm.progress", ["file": file, "retrying": true])
            DispatchQueue.main.asyncAfter(deadline: .now() + Double(min(20, tries * 3))) { [weak self] in
                guard let self, !self.cancelled.contains(file) else { return }
                self.start(file, resumeFrom: resume)
            }
        } else {
            fail(file, error.localizedDescription)
        }
    }

    private func fail(_ file: String, _ message: String) {
        tasks.removeValue(forKey: file)
        emit?("llm.failed", ["file": file, "error": message])
    }
}
