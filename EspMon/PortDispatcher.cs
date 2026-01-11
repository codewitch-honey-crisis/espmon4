using Json;

using OpenHardwareMonitor.Hardware;

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace EspMon
{
    internal class RefreshPortsEventArgs
    {
        public RefreshPortsEventArgs() { }
        public List<string> PortNames { get; } = new List<string>();
    }
    internal delegate void RefreshPortsEventHandler(object sender, RefreshPortsEventArgs e);

    internal class PortDispatcher : IDisposable
    {
        public const int BaudRate = 115200;
        internal class OhwmUpdateVisitor : IVisitor
        {
            public bool CollectPaths { get; set; } = false;
            public Dictionary<string, object> Paths = new Dictionary<string, object>();
            public void VisitComputer(IComputer computer)
            {
                computer.Traverse(this);
            }
            public void VisitHardware(IHardware hardware)
            {
                hardware.Update();
                foreach (ISensor sensor in hardware.Sensors)
                {
                    sensor.Accept(this);
                }
                foreach (IHardware subHardware in hardware.SubHardware)
                    subHardware.Accept(this);
                
            }
            public void VisitSensor(ISensor sensor) {
                if (CollectPaths)
                {
                    List<string> path = new List<string>();
                    //path.Add(sensor.Identifier.ToString());
                    //var parent = sensor.Hardware;
                    //path.Add(parent.Identifier.ToString());
                    //while (parent != null)
                    //{
                    //    path.Add(parent.Identifier.ToString());
                    //    parent = parent.Parent;
                    //}
                    //path.Reverse();
                    //Paths.Add(string.Join("/", path), sensor);
                    Paths.Add(sensor.Identifier.ToString(),sensor);
                }
                foreach (IParameter param in sensor.Parameters)
                {
                    param.Accept(this);
                }
            }
            public void VisitParameter(IParameter parameter) {
                if (CollectPaths)
                {
                    //List<string> path = new List<string>();
                    //path.Add(parameter.Identifier.ToString());
                    //var sensor = parameter.Sensor;
                    //path.Add(sensor.Identifier.ToString());
                    //var parent = sensor.Hardware;
                    //path.Add(parent.Identifier.ToString());
                    //while (parent != null)
                    //{
                    //    path.Add(parent.Identifier.ToString());
                    //    parent = parent.Parent;
                    //}
                    //path.Reverse();
                    //Paths.Add(string.Join("/", path), parameter);
                    Paths.Add(parameter.Identifier.ToString(),parameter);
                }
            }
        }
        public static PortDispatcher Instance { get; private set; }
        public event RefreshPortsEventHandler RefreshPortsRequested;
        Thread _ohwmThread = null;
        MessagingSynchronizationContext _ohwmSyncContext = null;
        CancellationTokenSource _ohwmCancelSource;
        CancellationToken _ohwmCancel;
        Computer _computer = null;
        Timer _refreshTimer = null;
        static ConcurrentDictionary<string, float> _matchCache = new ConcurrentDictionary<string, float>();
        static ConcurrentDictionary<string, object> _matchObjectCache = new ConcurrentDictionary<string, object>();
        static ConcurrentDictionary<string, Regex> _regexCache = new ConcurrentDictionary<string, Regex>();
        static ConcurrentDictionary<string, Screen[]> _screensByPort = new ConcurrentDictionary<string, Screen[]>();
        OhwmUpdateVisitor _updateVisitor = null;
        volatile bool _started = false;
        
        ConcurrentDictionary<string, SerialPort> _regPorts = null;
        private bool _disposed;

        private static void OhwmThreadProc(object state)
        {
            PortDispatcher _this = (PortDispatcher)state;
            _this._started = true;
            try
            {
                _this._ohwmSyncContext.Start(_this._ohwmCancel);
            }
            catch (OperationCanceledException)
            {

            }
            _this._started = false;
        }
        public PortDispatcher()
        {
            if (Instance != null)
            {
                throw new InvalidOperationException("A port dispatcher already exists");
            }
            Instance = this;
            _ohwmCancelSource = new CancellationTokenSource();
            _ohwmCancel = _ohwmCancelSource.Token;
            _ohwmThread = new Thread(new ParameterizedThreadStart(OhwmThreadProc));
            _ohwmSyncContext = new MessagingSynchronizationContext();
            _computer = new Computer();
            _computer.CPUEnabled = true;
            _computer.GPUEnabled = true;
            _computer.RAMEnabled = true;
            _computer.HDDEnabled = true;
            _computer.MainboardEnabled = true;
            _updateVisitor = new OhwmUpdateVisitor();
            _started = false;
            _regPorts = new ConcurrentDictionary<string, SerialPort>(StringComparer.InvariantCultureIgnoreCase);
        }
        
        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                _disposed = true;
            }
            Instance = null;
        }

        private static void Port_DataReceived(object sender, SerialDataReceivedEventArgs e)
        {
            try
            {
                SerialPort _port = (SerialPort)sender;
                int i = _port.ReadByte();
                if (i == 0)
                {
                    int scr = _port.ReadByte();
                    var screens = _screensByPort[_port.PortName];
                    scr = scr % screens.Length;
                    var packet = new byte[75];
                    packet[0] = 0;
                    screens[scr].ToScreenPacket(packet, 1, scr);
                    _port.Write(packet,0,packet.Length);
                    _port.BaseStream.Flush();
                }
                else if (i == 1)
                {
                    int scr = _port.ReadByte();
                    var packet = new byte[9];
                    packet[0] = 1;
                    var screens = _screensByPort[_port.PortName];
                    scr = scr % screens.Length;
                    screens[scr].ToDataPacket(packet, 1, _matchCache);
                    _port.Write(packet, 0, packet.Length);
                    _port.BaseStream.Flush();
                }
                else
                {
                    _port.ReadExisting();
                }
            }
            catch
            {

            }
        }
        private static void RebuildMatchCache()
        {
            _regexCache.Clear();
            _matchCache.Clear();
            _matchObjectCache.Clear();
            var matchStrings = new HashSet<string>();
            foreach (var screens in _screensByPort.Values)
            {
                foreach (var screen in screens)
                {
                    if (matchStrings.Add(screen.Top.Value1.Match))
                    {
                        _matchCache[screen.Top.Value1.Match] = float.NaN;
                    }
                    if (matchStrings.Add(screen.Top.Value2.Match))
                    {
                        _matchCache[screen.Top.Value2.Match] = float.NaN;
                    }
                    if (matchStrings.Add(screen.Bottom.Value1.Match))
                    {
                        _matchCache[screen.Bottom.Value1.Match] = float.NaN;
                    }
                    if (matchStrings.Add(screen.Bottom.Value2.Match))
                    {
                        _matchCache[screen.Bottom.Value2.Match] = float.NaN;
                    }
                }
            }
            foreach(var match in matchStrings)
            {
                _regexCache[match]=new Regex(match,RegexOptions.Singleline|RegexOptions.ExplicitCapture|RegexOptions.CultureInvariant);
            }
        }
        private void FetchHardwareInfo()
        {
            if (_computer == null || _screensByPort==null)
            {
                // TODO: Event log
                return;
            }
            // use OpenHardwareMonitorLib to collect the system info

            //_updateVisitor.Paths.Clear();
            _updateVisitor.CollectPaths = _updateVisitor.Paths.Count == 0;
            _computer.Accept(_updateVisitor);
            if(_matchCache.Count==0)
            {
                RebuildMatchCache();
            }
            if (_updateVisitor.CollectPaths)
            {
                try
                {
                    File.Delete("paths.map.txt");
                }
                catch { }
                using (var writer = File.CreateText("paths.map.txt"))
                {
                    foreach (var de in _updateVisitor.Paths)
                    {
                        writer.WriteLine(de.Key);
                        System.Diagnostics.Debug.WriteLine("{0}: {1} ({2})", de.Key, de.Value, de.Value.GetType().Name);
                    }
                }
                _matchObjectCache.Clear();
            }
            if(_matchObjectCache.Count==0)
            {
                foreach (var regex in _regexCache)
                {
                    foreach (var de in _updateVisitor.Paths)
                    {
                        if (regex.Value.IsMatch(de.Key))
                        {
                            _matchObjectCache[regex.Key] = de.Value;
                        }
                    }
                }
            }
            foreach (var de in _matchObjectCache)
            {
                var obj = de.Value;
                float v = float.NaN;
                if(obj is ISensor sensor)
                {
                    v=sensor.Value.GetValueOrDefault();
                } else if(obj is IParameter parameter) {
                    v = parameter.Value;
                }
                _matchCache[de.Key] = v;
            }
        }
        ~PortDispatcher()
        {
            Dispose(false);
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        public void Start()
        {
            if (_started) return;
            _ohwmThread.Start(this);
            // wait for the message pump to start
            do
            {
                Thread.Sleep(50);
            } while (!_started);
            _ohwmSyncContext.Send(new SendOrPostCallback((object state) =>
            {
                _computer.Open();

            }), null);
            _refreshTimer = new Timer(new TimerCallback(UpdateTimerProc), this, 0, 100);
        }
        private static void UpdateTimerProc(object state)
        {
            PortDispatcher _this = (PortDispatcher)state;
            _this._ohwmSyncContext.Post(new SendOrPostCallback((object st) =>
            {
                ((PortDispatcher)st).FetchHardwareInfo();
            }), _this);

            var args = new RefreshPortsEventArgs();
            _this.RefreshPortsRequested?.Invoke(_this, args);
            var portNames = args.PortNames.ToArray();

            for (int i = 0; i < portNames.Length; i++)
            {
                var portName = portNames[i];
                SerialPort p;
                if (!_this._regPorts.TryGetValue(portName, out p))
                {
                    var port = new SerialPort(portName, BaudRate);
                    try
                    {
                        port.Open();
                        port.DataReceived += Port_DataReceived;
                    }
                    catch
                    {
                        // Don't log here. We'll keep retrying later
                    }
                    // shouldn't fail
                    _this._regPorts.TryAdd(portName, port);
                    p = port;
                }
                else
                {
                    if (!p.IsOpen)
                    {
                        try
                        {
                            p.Open();
                            p.DataReceived += Port_DataReceived;
                        }
                        catch
                        {
                        }
                    }
                }
                if (p.IsOpen)
                {
                    Screen[] screens = null;
                    if (!_screensByPort.ContainsKey(portName))
                    {
                        var file = $"{portName.ToLower()}.screens.json";

                        // load the screens for this port
                        try
                        {
                            using (var reader = File.OpenText(file))
                            {
                                screens = Screen.Read(reader);
                                _screensByPort[p.PortName] = screens;
                            }
                        }
                        catch { }

                        if (screens == null)
                        {
                            using (var stm = Assembly.GetExecutingAssembly().GetManifestResourceStream("EspMon.default.screens.json"))
                            {
                                var reader = new StreamReader(stm);
                                screens = Screen.Read(reader);
                                _screensByPort[p.PortName] = screens;
                            }
                        }
                    }
                }
            }
            var toRemove = new List<string>(_this._regPorts.Count);
            // now clean out the ports that are no longer in the registry
            foreach (var kvp in _this._regPorts)
            {
                if (!portNames.Contains(kvp.Key, StringComparer.InvariantCultureIgnoreCase))
                {
                    if (kvp.Value.IsOpen)
                    {
                        try
                        {
                            kvp.Value.Close();
                        }
                        catch { }
                        kvp.Value.DataReceived -= Port_DataReceived;
                        toRemove.Add(kvp.Key);
                    }
                }
            }
            foreach (var key in toRemove)
            {
                SerialPort p;
                _this._regPorts.TryRemove(key, out p);
            }
            toRemove.Clear();
            toRemove = null;
            portNames = null;
        }
        public void Stop()
        {
            if (!_started)
            {
                return;
            }
            _refreshTimer?.Dispose();
            _refreshTimer = null;
            foreach (var kvp in _regPorts)
            {
                try
                {
                    kvp.Value.Close();
                }
                catch
                {

                }
                kvp.Value.DataReceived -= Port_DataReceived;
            }
            _regPorts.Clear();
            _ohwmCancelSource.Cancel();
            _ohwmThread.Join();
            _ohwmCancelSource.Dispose();
            _ohwmCancelSource = new CancellationTokenSource();
            _ohwmCancel = _ohwmCancelSource.Token;
            _ohwmThread = new Thread(new ParameterizedThreadStart(OhwmThreadProc));

        }
        public bool IsStarted
        {
            get
            {
                return _started;
            }
        }
    }


}
