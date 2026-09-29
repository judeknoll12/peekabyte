import AVFoundation
import SwiftUI
import WebKit

// The iPhone app is a thin native shell around the Peekabyte web app. The shell lends the
// page what a browser can't: Bluetooth that stays connected when the phone locks, and an
// open-source AI that runs on the iPhone's GPU with room to breathe (see Bridge.swift).

@main
struct PeekabyteApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    var body: some Scene {
        WindowGroup {
            RootView()
        }
    }
}

final class AppDelegate: NSObject, UIApplicationDelegate {
    func application(_ application: UIApplication,
                     didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]? = nil) -> Bool {
        // The pet's voice plays even with the ring switch on silent, and mixes with music.
        try? AVAudioSession.sharedInstance().setCategory(.playback, mode: .default, options: [.mixWithOthers])
        try? AVAudioSession.sharedInstance().setActive(true)
        return true
    }
}

/// The launch screen's color: light or dark with the phone (Assets: LaunchBackground).
let launchBackground = UIColor(named: "LaunchBackground") ?? UIColor(red: 13 / 255, green: 14 / 255, blue: 26 / 255, alpha: 1)

struct RootView: View {
    @StateObject private var shell = Shell()

    var body: some View {
        ZStack {
            Color(shell.background).ignoresSafeArea()
            WebView(shell: shell).ignoresSafeArea()
            if shell.splash {
                Splash().transition(.opacity)
            }
            if let problem = shell.problem {
                VStack(spacing: 14) {
                    Image("LaunchLogo")
                    Text("Can't reach Peekabyte").font(.system(.title2, design: .rounded).bold())
                    Text(problem)
                        .multilineTextAlignment(.center)
                        .foregroundColor(.secondary)
                    Button("Try again") { shell.reload() }
                        .buttonStyle(.borderedProminent)
                        .tint(Color(red: 108 / 255, green: 77 / 255, blue: 245 / 255))
                        .padding(.top, 6)
                }
                .padding(32)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Color(shell.background).ignoresSafeArea())
            }
        }
        .preferredColorScheme(shell.scheme)
        .animation(.easeOut(duration: 0.35), value: shell.splash)
    }
}

/// Picks up exactly where the launch screen leaves off (same color, same logo in the same
/// spot) and stays until the page has drawn itself, so opening the app never flashes blank.
struct Splash: View {
    var body: some View {
        ZStack {
            Color(launchBackground)
            Image("LaunchLogo")
        }
        .ignoresSafeArea()
    }
}

struct WebView: UIViewRepresentable {
    let shell: Shell
    func makeUIView(context: Context) -> WKWebView { shell.webView }
    func updateUIView(_ view: WKWebView, context: Context) {}
}

/// Owns the web view that shows the app and the bridge that gives it native powers.
final class Shell: NSObject, ObservableObject, WKNavigationDelegate, WKUIDelegate {
    static let home = URL(string: "https://judeknoll12.github.io/peekabyte/")!

    @Published var problem: String?
    @Published var splash = true
    @Published var scheme: ColorScheme?            // nil: follow the phone
    @Published var background: UIColor = launchBackground
    let bridge = Bridge()
    private(set) lazy var webView: WKWebView = makeWebView()

    override init() {
        super.init()
        bridge.onTheme = { [weak self] dark, color, followsPhone in
            guard let self else { return }
            self.scheme = followsPhone ? nil : (dark ? .dark : .light)
            self.background = color
            self.webView.backgroundColor = color
            self.webView.scrollView.backgroundColor = color
        }
        bridge.onReady = { [weak self] in self?.hideSplash() }
        // Never keep the splash up for long, even if the page can't say it's ready.
        DispatchQueue.main.asyncAfter(deadline: .now() + 8) { [weak self] in self?.hideSplash() }
    }

    func hideSplash() {
        if splash { splash = false }
    }

    private func makeWebView() -> WKWebView {
        let config = WKWebViewConfiguration()
        // Our site is the app's "bound" domain: that unlocks its offline cache (service worker)
        // and keeps the native bridge away from any other page.
        config.limitsNavigationsToAppBoundDomains = true
        config.allowsInlineMediaPlayback = true
        config.mediaTypesRequiringUserActionForPlayback = []
        let scripts = config.userContentController
        scripts.addUserScript(WKUserScript(source: Bridge.bootstrap, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        scripts.add(bridge, name: "peeka")

        let view = WKWebView(frame: .zero, configuration: config)
        view.isOpaque = false
        view.backgroundColor = launchBackground
        view.scrollView.backgroundColor = launchBackground
        view.scrollView.contentInsetAdjustmentBehavior = .never
        view.scrollView.bounces = false
        view.allowsBackForwardNavigationGestures = false
        view.navigationDelegate = self
        view.uiDelegate = self
        if #available(iOS 16.4, *) { view.isInspectable = true }
        bridge.webView = view
        view.load(URLRequest(url: Self.home))
        return view
    }

    func reload() {
        problem = nil
        webView.load(URLRequest(url: Self.home))
    }

    // MARK: navigation

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        problem = nil
        // Older versions of the page don't say when they're ready.
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in self?.hideSplash() }
    }

    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        show(error)
    }

    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        show(error)
    }

    private func show(_ error: Error) {
        let e = error as NSError
        if e.domain == NSURLErrorDomain && e.code == NSURLErrorCancelled { return }
        if e.domain == "WebKitErrorDomain" && e.code == 102 { return }   // navigation handed off elsewhere
        hideSplash()
        problem = "The app loads from the internet the first time (after that it works offline too). Check your connection.\n\n\(e.localizedDescription)"
    }

    // The page ran out of memory and was closed by iOS. Bluetooth and the AI live on in the
    // app itself, so just bring the page back; it reconnects to both.
    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        bridge.pageRestarted()
        webView.load(URLRequest(url: Self.home))
    }

    func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        guard let url = action.request.url else { return decisionHandler(.cancel) }
        if url.host == Self.home.host || url.scheme == "about" || url.scheme == "blob" || url.scheme == "data" {
            return decisionHandler(.allow)
        }
        // Links to anywhere else open in Safari.
        if action.navigationType == .linkActivated || action.targetFrame == nil {
            UIApplication.shared.open(url)
        }
        decisionHandler(.cancel)
    }

    func webView(_ webView: WKWebView, createWebViewWith configuration: WKWebViewConfiguration,
                 for action: WKNavigationAction, windowFeatures: WKWindowFeatures) -> WKWebView? {
        if let url = action.request.url { UIApplication.shared.open(url) }
        return nil
    }
}
