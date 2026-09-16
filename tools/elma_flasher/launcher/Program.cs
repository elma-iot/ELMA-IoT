using System;
using System.IO;
using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace Elma.IoT.Flasher.Launcher;

internal static class Program
{
    private const string PayloadResource = "ELMA.Flasher.Payload.zip";
    private const string PayloadFingerprintResource = "ELMA.Flasher.Payload.sha256";
    private const string CoreExecutable = "ELMA-Flasher-Core.exe";
    private const int SwRestore = 9;

    private delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr parameter);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextW(IntPtr window, char[] text, int maximumCount);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    private static extern bool BringWindowToTop(IntPtr window);

    [DllImport("user32.dll")]
    private static extern bool AllowSetForegroundWindow(int processId);

    [STAThread]
    private static int Main(string[] args)
    {
        string version = Assembly.GetExecutingAssembly().GetName().Version?.ToString(3) ?? "0.0.0";
        string mutexName = $"Local\\ELMA.IoT.Flasher.v{version}.SingleInstance";
        using var instanceMutex = new Mutex(true, mutexName, out bool ownsInstance);
        if (!ownsInstance)
        {
            if (!args.Any(argument => argument.EndsWith("-test", StringComparison.OrdinalIgnoreCase)))
            {
                ActivateExistingWindow();
            }
            return 0;
        }

        try
        {
            if (args.Contains("--launcher-instance-hold-test", StringComparer.OrdinalIgnoreCase))
            {
                Thread.Sleep(5000);
                return 0;
            }
            string launcherPath = Environment.ProcessPath ?? throw new InvalidOperationException("The launcher path is unavailable.");
            string portableHome = Path.GetDirectoryName(launcherPath) ?? Environment.CurrentDirectory;
            string? corePath = FindReadyPayload(version);
            if (corePath is null)
            {
                corePath = new StartupWindow(version).Run(progress => EnsurePayload(version, progress));
            }
            if (corePath is null)
            {
                throw new InvalidOperationException("The cached ELMA runtime is unavailable.");
            }
            var start = new ProcessStartInfo(corePath)
            {
                UseShellExecute = false,
                WorkingDirectory = portableHome,
            };
            foreach (string argument in args)
            {
                start.ArgumentList.Add(argument);
            }
            start.Environment["ELMA_PORTABLE_HOME"] = portableHome;
            start.Environment["ELMA_LAUNCHED_PORTABLY"] = "1";
            using Process child = Process.Start(start) ?? throw new InvalidOperationException("ELMA Flasher could not be started.");
            bool isTestRun = args.Any(argument => argument.EndsWith("-test", StringComparison.OrdinalIgnoreCase));
            if (!isTestRun)
            {
                // The launcher is the process Windows grants foreground rights to.
                // Pass those rights to the Qt child, then raise its real window once
                // it exists instead of leaving it behind the previously active app.
                AllowSetForegroundWindow(child.Id);
                ActivateExistingWindow();
            }
            child.WaitForExit();
            return child.ExitCode;
        }
        catch (Exception error)
        {
            MessageBoxW(IntPtr.Zero, error.Message, "ELMA Flasher could not start", 0x10);
            return 70;
        }
        finally
        {
            instanceMutex.ReleaseMutex();
        }
    }

    private static string RuntimeRoot(string version)
    {
        string cacheBase = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return Path.Combine(cacheBase, "ELMA IoT", "Flasher", $"runtime-v{version}");
    }

    private static string? FindReadyPayload(string version)
    {
        string runtimeRoot = RuntimeRoot(version);
        string expectedFingerprint = PayloadFingerprint();
        string parent = Path.GetDirectoryName(runtimeRoot)!;
        var candidates = new[] { runtimeRoot }.Concat(Directory.Exists(parent)
            ? Directory.EnumerateDirectories(parent, Path.GetFileName(runtimeRoot) + ".extracting-*")
            : Array.Empty<string>());
        foreach (string candidate in candidates)
        {
        string corePath = Path.Combine(candidate, CoreExecutable);
        string marker = Path.Combine(candidate, ".elma-runtime-ready");
        if (!File.Exists(corePath) || !File.Exists(marker)) continue;
        try
        {
            if (string.Equals(File.ReadAllText(marker).Trim(), expectedFingerprint, StringComparison.OrdinalIgnoreCase)) return corePath;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            // A locked old cache must not prevent trying another completed cache.
        }
        }
        return null;
    }

    private static string PayloadFingerprint()
    {
        using Stream fingerprint = Assembly.GetExecutingAssembly().GetManifestResourceStream(PayloadFingerprintResource)
            ?? throw new InvalidOperationException("The embedded runtime fingerprint is missing.");
        using var reader = new StreamReader(fingerprint);
        return reader.ReadToEnd().Trim();
    }

    private static string EnsurePayload(string version, IProgress<StartupProgress> progress)
    {
        string runtimeRoot = RuntimeRoot(version);
        string corePath = Path.Combine(runtimeRoot, CoreExecutable);
        string? ready = FindReadyPayload(version);
        if (ready is not null) return ready;

        string parent = Path.GetDirectoryName(runtimeRoot)!;
        Directory.CreateDirectory(parent);
        string staging = runtimeRoot + $".extracting-{Guid.NewGuid():N}";
        Directory.CreateDirectory(staging);

        progress.Report(new StartupProgress(1, "Reading embedded portable components"));
        using Stream payload = Assembly.GetExecutingAssembly().GetManifestResourceStream(PayloadResource)
            ?? throw new InvalidOperationException("The embedded ELMA runtime is missing.");
        using (var archive = new ZipArchive(payload, ZipArchiveMode.Read, false))
        {
            long totalBytes = Math.Max(1, archive.Entries.Where(entry => !string.IsNullOrEmpty(entry.Name)).Sum(entry => entry.Length));
            long completedBytes = 0;
            byte[] buffer = new byte[1024 * 1024];
            string stagingPrefix = Path.GetFullPath(staging) + Path.DirectorySeparatorChar;
            foreach (ZipArchiveEntry entry in archive.Entries)
            {
                string destination = Path.GetFullPath(Path.Combine(staging, entry.FullName.Replace('/', Path.DirectorySeparatorChar)));
                if (!destination.StartsWith(stagingPrefix, StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidDataException("The embedded runtime contains an unsafe path.");
                }
                if (string.IsNullOrEmpty(entry.Name))
                {
                    Directory.CreateDirectory(destination);
                    continue;
                }
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                using Stream input = entry.Open();
                using var output = new FileStream(destination, FileMode.Create, FileAccess.Write, FileShare.None, buffer.Length, FileOptions.SequentialScan);
                int count;
                while ((count = input.Read(buffer, 0, buffer.Length)) > 0)
                {
                    output.Write(buffer, 0, count);
                    completedBytes += count;
                    double percent = 2 + (completedBytes * 94.0 / totalBytes);
                    progress.Report(new StartupProgress(percent, "Extracting compiler, designer and ESP support"));
                }
                File.SetLastWriteTime(destination, entry.LastWriteTime.LocalDateTime);
            }
        }
        progress.Report(new StartupProgress(97, "Validating the portable runtime"));
        string stagedCore = Path.Combine(staging, CoreExecutable);
        if (!File.Exists(stagedCore))
        {
            throw new InvalidDataException("The embedded ELMA runtime is incomplete.");
        }
        File.WriteAllText(Path.Combine(staging, ".elma-runtime-ready"), PayloadFingerprint());
        // The ready marker commits this immutable generation. Antivirus/indexers
        // can hold directory handles after extraction, making rename/delete fail.
        // Run in place and retain older generations that may still be in use.
        progress.Report(new StartupProgress(99, "Starting the verified runtime"));
        return stagedCore;
    }

    private static void ActivateExistingWindow()
    {
        for (int attempt = 0; attempt < 600; attempt++)
        {
            IntPtr window = FindElmaWindow();
            if (window != IntPtr.Zero)
            {
                ShowWindow(window, SwRestore);
                BringWindowToTop(window);
                SetForegroundWindow(window);
                return;
            }
            Thread.Sleep(100);
        }
    }

    private static IntPtr FindElmaWindow()
    {
        IntPtr match = IntPtr.Zero;
        EnumWindows((window, _) =>
        {
            var title = new char[256];
            int length = GetWindowTextW(window, title, title.Length);
            if (length > 0 && new string(title, 0, length).Contains("ELMA Flasher v", StringComparison.Ordinal))
            {
                match = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return match;
    }

    private readonly record struct StartupProgress(double Percent, string Status);

    private sealed class StartupWindow
    {
        private const uint WmPaint = 0x000F;
        private const uint WmClose = 0x0010;
        private const uint WmDestroy = 0x0002;
        private const uint WmEraseBackground = 0x0014;
        private const uint WmTimer = 0x0113;
        private const uint WmProgress = 0x8001;
        private const uint WsCaption = 0x00C00000;
        private const uint WsSysMenu = 0x00080000;
        private const uint WsMinimizeBox = 0x00020000;
        private const int SwShow = 5;
        private const int Transparent = 1;
        private const int Srccopy = 0x00CC0020;
        private const int NullPen = 8;
        private const int DefaultGuiFont = 17;
        private static readonly WindowProcedure WindowCallback = WindowProc;
        private static StartupWindow? current;

        private readonly object stateLock = new();
        private readonly string version;
        private IntPtr window;
        private double targetPercent;
        private double displayedPercent;
        private string status = "Preparing portable components";
        private volatile bool extractionComplete;
        private volatile bool extractionFailed;

        public StartupWindow(string version)
        {
            this.version = version;
        }

        public string Run(Func<IProgress<StartupProgress>, string> extractor)
        {
            CreateNativeWindow();
            var progress = new DirectProgress(this);
            Task<string> extraction = Task.Run(() => extractor(progress));
            extraction.ContinueWith(task =>
            {
                extractionFailed = task.IsFaulted || task.IsCanceled;
                extractionComplete = true;
                Report(extractionFailed
                    ? new StartupProgress(targetPercent, "Portable runtime preparation failed")
                    : new StartupProgress(100, "ELMA Flasher is ready"));
                if (extractionFailed) PostMessageW(window, WmClose, IntPtr.Zero, IntPtr.Zero);
            }, TaskScheduler.Default);

            while (GetMessageW(out Message message, IntPtr.Zero, 0, 0) > 0)
            {
                TranslateMessage(ref message);
                DispatchMessageW(ref message);
            }
            current = null;
            return extraction.GetAwaiter().GetResult();
        }

        private void Report(StartupProgress update)
        {
            lock (stateLock)
            {
                targetPercent = Math.Clamp(update.Percent, targetPercent, 100);
                status = update.Status;
            }
            if (window != IntPtr.Zero) PostMessageW(window, WmProgress, IntPtr.Zero, IntPtr.Zero);
        }

        private void CreateNativeWindow()
        {
            current = this;
            string className = $"ELMAFlasherStartup{version.Replace('.', '_')}";
            IntPtr instance = GetModuleHandleW(null);
            var windowClass = new WindowClass
            {
                Size = (uint)Marshal.SizeOf<WindowClass>(),
                Style = 3,
                Procedure = WindowCallback,
                Instance = instance,
                Cursor = LoadCursorW(IntPtr.Zero, (IntPtr)32512),
                ClassName = className,
            };
            IntPtr[] largeIcons = new IntPtr[1];
            IntPtr[] smallIcons = new IntPtr[1];
            ExtractIconExW(Environment.ProcessPath!, 0, largeIcons, smallIcons, 1);
            windowClass.Icon = largeIcons[0];
            windowClass.SmallIcon = smallIcons[0];
            if (RegisterClassExW(ref windowClass) == 0) throw new InvalidOperationException("The ELMA startup window could not be registered.");
            int width = 620;
            int height = 250;
            int left = Math.Max(0, (GetSystemMetrics(0) - width) / 2);
            int top = Math.Max(0, (GetSystemMetrics(1) - height) / 2);
            window = CreateWindowExW(0, className, $"ELMA Flasher v{version}", WsCaption | WsSysMenu | WsMinimizeBox,
                left, top, width, height, IntPtr.Zero, IntPtr.Zero, instance, IntPtr.Zero);
            if (window == IntPtr.Zero) throw new InvalidOperationException("The ELMA startup window could not be created.");
            ShowWindow(window, SwShow);
            UpdateWindow(window);
            SetTimer(window, 1, 16, IntPtr.Zero);
        }

        private static IntPtr WindowProc(IntPtr window, uint message, IntPtr word, IntPtr data)
        {
            StartupWindow? startup = current;
            if (startup is null) return DefWindowProcW(window, message, word, data);
            switch (message)
            {
                case WmTimer:
                    startup.AnimateProgress();
                    return IntPtr.Zero;
                case WmProgress:
                    InvalidateRect(window, IntPtr.Zero, false);
                    return IntPtr.Zero;
                case WmPaint:
                    startup.Paint(window);
                    return IntPtr.Zero;
                case WmEraseBackground:
                    return (IntPtr)1;
                case WmClose:
                    if (startup.extractionFailed) DestroyWindow(window);
                    return IntPtr.Zero;
                case WmDestroy:
                    KillTimer(window, 1);
                    PostQuitMessage(0);
                    return IntPtr.Zero;
            }
            return DefWindowProcW(window, message, word, data);
        }

        private void AnimateProgress()
        {
            lock (stateLock)
            {
                double remaining = targetPercent - displayedPercent;
                if (remaining > 0.01)
                {
                    displayedPercent += Math.Max(0.12, remaining * 0.14);
                    displayedPercent = Math.Min(displayedPercent, targetPercent);
                }
            }
            InvalidateRect(window, IntPtr.Zero, false);
            if (extractionComplete && !extractionFailed && displayedPercent >= 99.95)
            {
                displayedPercent = 100;
                UpdateWindow(window);
                Thread.Sleep(180);
                DestroyWindow(window);
            }
        }

        private void Paint(IntPtr targetWindow)
        {
            IntPtr device = BeginPaint(targetWindow, out PaintState paint);
            GetClientRect(targetWindow, out Rectangle rectangle);
            IntPtr memory = CreateCompatibleDC(device);
            IntPtr bitmap = CreateCompatibleBitmap(device, rectangle.Right, rectangle.Bottom);
            IntPtr previousBitmap = SelectObject(memory, bitmap);
            IntPtr dark = CreateSolidBrush(Rgb(42, 41, 38));
            IntPtr orange = CreateSolidBrush(Rgb(239, 139, 0));
            IntPtr white = CreateSolidBrush(Rgb(255, 253, 248));
            IntPtr muted = CreateSolidBrush(Rgb(205, 201, 193));
            IntPtr track = CreateSolidBrush(Rgb(79, 76, 70));
            FillRect(memory, ref rectangle, dark);
            SelectObject(memory, GetStockObject(NullPen));
            SelectObject(memory, orange);
            Ellipse(memory, 28, 24, 86, 82);
            DrawText(memory, "E", 43, 29, 29, 700, Rgb(255, 253, 248));
            DrawText(memory, "ELMA Flasher", 103, 22, 29, 700, Rgb(255, 253, 248));
            DrawText(memory, "Portable ESP device designer, compiler and USB flasher", 106, 61, 15, 400, Rgb(205, 201, 193));
            string currentStatus;
            double currentPercent;
            lock (stateLock)
            {
                currentStatus = status;
                currentPercent = displayedPercent;
            }
            DrawText(memory, currentStatus, 31, 112, 16, 400, Rgb(255, 253, 248));
            string percent = $"{Math.Floor(currentPercent):0}%";
            IntPtr percentFont = CreateFontW(-16, 0, 0, 0, 700, 0, 0, 0, 1, 0, 0, 5, 0, "Segoe UI");
            IntPtr oldFont = SelectObject(memory, percentFont);
            GetTextExtentPoint32W(memory, percent, percent.Length, out TextSize extent);
            SetTextColor(memory, Rgb(255, 253, 248));
            SetBkMode(memory, Transparent);
            TextOutW(memory, 588 - extent.Width, 112, percent, percent.Length);
            SelectObject(memory, oldFont);
            DeleteObject(percentFont);
            SelectObject(memory, track);
            RoundRect(memory, 31, 150, 588, 167, 17, 17);
            int fillRight = 31 + (int)(557 * Math.Clamp(currentPercent, 0, 100) / 100.0);
            if (fillRight > 32)
            {
                SelectObject(memory, orange);
                RoundRect(memory, 31, 150, fillRight, 167, 17, 17);
            }
            DrawText(memory, "First launch prepares a verified local cache; later starts reuse it.", 31, 180, 15, 400, Rgb(205, 201, 193));
            BitBlt(device, 0, 0, rectangle.Right, rectangle.Bottom, memory, 0, 0, Srccopy);
            SelectObject(memory, previousBitmap);
            DeleteObject(bitmap);
            DeleteObject(dark);
            DeleteObject(orange);
            DeleteObject(white);
            DeleteObject(muted);
            DeleteObject(track);
            DeleteDC(memory);
            EndPaint(targetWindow, ref paint);
        }

        private static void DrawText(IntPtr device, string text, int x, int y, int size, int weight, uint color)
        {
            IntPtr font = CreateFontW(-size, 0, 0, 0, weight, 0, 0, 0, 1, 0, 0, 5, 0, "Segoe UI");
            IntPtr previous = SelectObject(device, font);
            SetTextColor(device, color);
            SetBkMode(device, Transparent);
            TextOutW(device, x, y, text, text.Length);
            SelectObject(device, previous);
            DeleteObject(font);
        }

        private static uint Rgb(byte red, byte green, byte blue) => (uint)(red | (green << 8) | (blue << 16));

        private sealed class DirectProgress : IProgress<StartupProgress>
        {
            private readonly StartupWindow owner;
            public DirectProgress(StartupWindow owner) => this.owner = owner;
            public void Report(StartupProgress value) => owner.Report(value);
        }

        private delegate IntPtr WindowProcedure(IntPtr window, uint message, IntPtr word, IntPtr data);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WindowClass
        {
            public uint Size, Style;
            public WindowProcedure Procedure;
            public int ClassExtra, WindowExtra;
            public IntPtr Instance, Icon, Cursor, Background;
            public string? MenuName, ClassName;
            public IntPtr SmallIcon;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct Message { public IntPtr Window; public uint Value; public IntPtr Word, Data; public uint Time; public int X, Y; }
        [StructLayout(LayoutKind.Sequential)]
        private struct Rectangle { public int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)]
        private struct TextSize { public int Width, Height; }
        [StructLayout(LayoutKind.Sequential)]
        private struct PaintState { public IntPtr Device; public int Erase; public Rectangle Area; public int Restore, Update; public int Reserved0, Reserved1, Reserved2, Reserved3, Reserved4, Reserved5, Reserved6, Reserved7; }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr GetModuleHandleW(string? name);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern ushort RegisterClassExW(ref WindowClass windowClass);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr CreateWindowExW(uint extended, string className, string title, uint style, int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr parameter);
        [DllImport("user32.dll")] private static extern IntPtr DefWindowProcW(IntPtr window, uint message, IntPtr word, IntPtr data);
        [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr window, int command);
        [DllImport("user32.dll")] private static extern bool UpdateWindow(IntPtr window);
        [DllImport("user32.dll")] private static extern int GetSystemMetrics(int index);
        [DllImport("user32.dll")] private static extern IntPtr LoadCursorW(IntPtr instance, IntPtr cursor);
        [DllImport("shell32.dll", CharSet = CharSet.Unicode)] private static extern uint ExtractIconExW(string file, int index, IntPtr[] large, IntPtr[] small, uint count);
        [DllImport("user32.dll")] private static extern IntPtr SetTimer(IntPtr window, nuint id, uint milliseconds, IntPtr callback);
        [DllImport("user32.dll")] private static extern bool KillTimer(IntPtr window, nuint id);
        [DllImport("user32.dll")] private static extern bool PostMessageW(IntPtr window, uint message, IntPtr word, IntPtr data);
        [DllImport("user32.dll")] private static extern int GetMessageW(out Message message, IntPtr window, uint minimum, uint maximum);
        [DllImport("user32.dll")] private static extern bool TranslateMessage(ref Message message);
        [DllImport("user32.dll")] private static extern IntPtr DispatchMessageW(ref Message message);
        [DllImport("user32.dll")] private static extern void PostQuitMessage(int code);
        [DllImport("user32.dll")] private static extern bool DestroyWindow(IntPtr window);
        [DllImport("user32.dll")] private static extern bool InvalidateRect(IntPtr window, IntPtr rectangle, bool erase);
        [DllImport("user32.dll")] private static extern IntPtr BeginPaint(IntPtr window, out PaintState paint);
        [DllImport("user32.dll")] private static extern bool EndPaint(IntPtr window, ref PaintState paint);
        [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr window, out Rectangle rectangle);
        [DllImport("user32.dll")] private static extern int FillRect(IntPtr device, ref Rectangle rectangle, IntPtr brush);
        [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleDC(IntPtr device);
        [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleBitmap(IntPtr device, int width, int height);
        [DllImport("gdi32.dll")] private static extern IntPtr SelectObject(IntPtr device, IntPtr item);
        [DllImport("gdi32.dll")] private static extern IntPtr CreateSolidBrush(uint color);
        [DllImport("gdi32.dll")] private static extern IntPtr GetStockObject(int item);
        [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr item);
        [DllImport("gdi32.dll")] private static extern bool DeleteDC(IntPtr device);
        [DllImport("gdi32.dll")] private static extern bool Ellipse(IntPtr device, int left, int top, int right, int bottom);
        [DllImport("gdi32.dll")] private static extern bool RoundRect(IntPtr device, int left, int top, int right, int bottom, int width, int height);
        [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr CreateFontW(int height, int width, int escapement, int orientation, int weight, uint italic, uint underline, uint strikeout, uint charset, uint outputPrecision, uint clipPrecision, uint quality, uint pitchFamily, string face);
        [DllImport("gdi32.dll")] private static extern uint SetTextColor(IntPtr device, uint color);
        [DllImport("gdi32.dll")] private static extern int SetBkMode(IntPtr device, int mode);
        [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] private static extern bool TextOutW(IntPtr device, int x, int y, string text, int length);
        [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] private static extern bool GetTextExtentPoint32W(IntPtr device, string text, int length, out TextSize size);
        [DllImport("gdi32.dll")] private static extern bool BitBlt(IntPtr target, int x, int y, int width, int height, IntPtr source, int sourceX, int sourceY, int operation);
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBoxW(IntPtr owner, string text, string caption, uint type);
}
