using Json;

using OpenHardwareMonitor.Hardware;

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Documents;

namespace EspMon
{
    
    internal struct Screen
    {
        internal struct SubEntry
        {
            public string Suffix { get; set; }
            public bool IsGradient { get; set; }
            public ushort Max { get; set; }
            public Color Color { get; set; }
            public string Match { get; set; }    
        }
        internal struct Entry
        {
            public string Label { get; set; }
            public Color Color { get; set; }
            public SubEntry Value1 { get; set; }
            public SubEntry Value2 { get; set; }
        }
        public Entry Top { get; set; }  
        public Entry Bottom { get; set; }
        private static void ColorToBytes(Color color, byte[] array, int startIndex)
        {
            array[startIndex] = color.R;
            array[startIndex + 1] = color.G;
            array[startIndex + 2] = color.B;
            array[startIndex + 3] = color.A;
        }
        static Color ParseColor(string color)
        {

            if (color.StartsWith("#"))
            {
                var r = byte.Parse(color.Substring(1, 2), NumberStyles.HexNumber);
                var g = byte.Parse(color.Substring(3, 2), NumberStyles.HexNumber);
                var b = byte.Parse(color.Substring(5, 2), NumberStyles.HexNumber);
                byte a = 0xFF;
                if (color.Length > 7)
                {
                    a = byte.Parse(color.Substring(7, 2), NumberStyles.HexNumber);
                }
                return Color.FromArgb(a, r, g, b);
            }
            var sb = new StringBuilder(color.Length);
            for (var i = 0; i < color.Length; i++)
            {
                if (char.IsLetterOrDigit(color[i]))
                {
                    sb.Append(color[i]);
                }
            }
            var props = typeof(Color).GetProperties(BindingFlags.Static | BindingFlags.Public);
            foreach (var prop in props)
            {
                if(0==string.Compare(prop.Name,sb.ToString(), StringComparison.OrdinalIgnoreCase))
                {
                    return (Color)prop.GetValue(null);
                }
            }
            throw new Exception("Color not found");
        }
        static UInt16 ToUint16(float val)
        {
            if (float.IsNaN(val)) { return 0; }
            return (UInt16)Math.Round(val);
        }
        public void ToDataPacket(byte[] destination, int destinationIndex, ConcurrentDictionary<string,float> matchCache)
        {
            float top1 = float.NaN;
            float top2 = float.NaN;
            float bottom1 = float.NaN;
            float bottom2 = float.NaN;
            if (string.IsNullOrEmpty(Top.Value1.Match) || !matchCache.TryGetValue(Top.Value1.Match, out top1)) { top1 = float.NaN; }
            if (string.IsNullOrEmpty(Top.Value2.Match) || !matchCache.TryGetValue(Top.Value2.Match, out top2)) { top2 = float.NaN; }
            if (string.IsNullOrEmpty(Bottom.Value1.Match) || !matchCache.TryGetValue(Bottom.Value1.Match, out bottom1)) { bottom1 = float.NaN; }
            if (string.IsNullOrEmpty(Bottom.Value2.Match) || !matchCache.TryGetValue(Bottom.Value2.Match, out bottom2)) { bottom2 = float.NaN; }

            /*
            typedef struct { // 8 bytes on the wire
                uint8_t index;
                uint16_t top_value1;
                uint16_t top_value2;
                uint16_t bottom_value1;
                uint16_t bottom_value2;
            } response_data_t;
            */
            var tmp = BitConverter.GetBytes(ToUint16(top1));
            if(!BitConverter.IsLittleEndian) { Array.Reverse(tmp); }
            tmp.CopyTo(destination, destinationIndex);
            destinationIndex += 2;
            tmp = BitConverter.GetBytes(ToUint16(top2));
            if (!BitConverter.IsLittleEndian) { Array.Reverse(tmp); }
            tmp.CopyTo(destination, destinationIndex);
            destinationIndex += 2;

            tmp = BitConverter.GetBytes(ToUint16(bottom1));
            if (!BitConverter.IsLittleEndian) { Array.Reverse(tmp); }
            tmp.CopyTo(destination, destinationIndex);
            destinationIndex += 2;
            tmp = BitConverter.GetBytes(ToUint16(bottom2));
            if (!BitConverter.IsLittleEndian) { Array.Reverse(tmp); }
            tmp.CopyTo(destination, destinationIndex);

           
        }
        public void ToScreenPacket(byte[] destination, int destinationIndex, int screenIndex)
        {
            /*
            typedef struct { // 74 bytes on the wire
                int8_t index; // written by caller
                uint8_t flags; // bit 0 top 1 is gradient, bit 1 top 2 is gradient, bit 2 bottom 1 is gradient, bit 3 bottom 2 is gradient
                char top_label[12];
                uint8_t top_label_color[4];
                uint8_t top_color1[4];
                char top_suffix1[4];
                uint16_t top_max1;
                uint8_t top_color2[4];
                char top_suffix2[4];
                uint16_t top_max2;
                char bottom_label[12];
                uint8_t bottom_label_color[4];
                uint8_t bottom_color1[4];
                char bottom_suffix1[4];
                uint16_t bottom_max1;
                uint8_t bottom_color2[4];
                char bottom_suffix2[4];
                uint16_t bottom_max2;
            } response_screen_t;
             */
            destination[destinationIndex++] =(byte)screenIndex;
            byte flags = 0;
            if (Top.Value1.IsGradient) flags |= (1 << 0);
            if (Top.Value2.IsGradient) flags |= (1 << 1);
            if (Bottom.Value1.IsGradient) flags |= (1 << 2);
            if (Bottom.Value2.IsGradient) flags |= (1 << 3);
            destination[destinationIndex++]=flags;
            var tmp = Encoding.UTF8.GetBytes(Top.Label);
            var size = Math.Min(tmp.Length, 11);
            Array.Copy(tmp, 0, destination, destinationIndex, size);
            destinationIndex += 12;
            ColorToBytes(Top.Color,destination,destinationIndex);
            destinationIndex += 4;
            ColorToBytes(Top.Value1.Color, destination, destinationIndex);
            destinationIndex += 4;
            tmp = Encoding.UTF8.GetBytes(Top.Value1.Suffix);
            size = Math.Min(tmp.Length, 3);
            Array.Copy(tmp, 0, destination, destinationIndex, size);
            destinationIndex += 4;
            tmp = BitConverter.GetBytes(Top.Value1.Max);
            if(!BitConverter.IsLittleEndian)
            {
                Array.Reverse(tmp);
            }
            Array.Copy(tmp, 0, destination, destinationIndex, 2);
            destinationIndex += 2;
            ColorToBytes(Top.Value2.Color, destination, destinationIndex);
            destinationIndex += 4;
            tmp = Encoding.UTF8.GetBytes(Top.Value2.Suffix);
            size = Math.Min(tmp.Length, 3);
            Array.Copy(tmp, 0, destination, destinationIndex, size);
            destinationIndex += 4;
            tmp = BitConverter.GetBytes(Top.Value2.Max);
            if (!BitConverter.IsLittleEndian)
            {
                Array.Reverse(tmp);
            }
            Array.Copy(tmp, 0, destination, destinationIndex, 2);
            destinationIndex += 2;

            tmp = Encoding.UTF8.GetBytes(Bottom.Label);
            size = Math.Min(tmp.Length, 11);
            Array.Copy(tmp, 0, destination, destinationIndex, size);
            destinationIndex += 12;
            ColorToBytes(Bottom.Color, destination, destinationIndex);
            destinationIndex += 4;
            ColorToBytes(Bottom.Value1.Color, destination, destinationIndex);
            destinationIndex += 4;
            tmp = Encoding.UTF8.GetBytes(Bottom.Value1.Suffix);
            size = Math.Min(tmp.Length, 3);
            Array.Copy(tmp, 0, destination, destinationIndex, size);
            destinationIndex += 4;
            tmp = BitConverter.GetBytes(Bottom.Value1.Max);
            if (!BitConverter.IsLittleEndian)
            {
                Array.Reverse(tmp);
            }
            Array.Copy(tmp, 0, destination, destinationIndex, 2);
            destinationIndex += 2;
            ColorToBytes(Bottom.Value2.Color, destination, destinationIndex);
            destinationIndex += 4;
            tmp = Encoding.UTF8.GetBytes(Bottom.Value2.Suffix);
            size = Math.Min(tmp.Length, 3);
            Array.Copy(tmp, 0, destination, destinationIndex, size);
            destinationIndex += 4;
            tmp = BitConverter.GetBytes(Bottom.Value2.Max);
            if (!BitConverter.IsLittleEndian)
            {
                Array.Reverse(tmp);
            }
            Array.Copy(tmp, 0, destination, destinationIndex, 2);

        }
        public static Screen[] Read(TextReader jsonData)
        {
            var result = new List<Screen>();
            dynamic json = JsonObject.Parse(jsonData);
            foreach(dynamic scr in json)
            {
                Entry top = new Entry();
                top.Label = scr.top.label;
                top.Color = ParseColor(scr.top.color);
                SubEntry top1 = new SubEntry();
                top1.Color = ParseColor(scr.top.value1.color);
                top1.IsGradient = false;
                try
                {
                    top1.IsGradient = scr.top.value1.gradient;
                }
                catch { }
                top1.Max = (ushort)Math.Round(scr.top.value1.max);
                top1.Suffix = scr.top.value1.suffix;
                top1.Match = scr.top.value1.match;
                top.Value1 = top1;
                
                SubEntry top2 = new SubEntry();
                top2.Color = ParseColor(scr.top.value2.color);
                top2.IsGradient = false;
                try
                {
                    top2.IsGradient = scr.top.value2.gradient;
                }
                catch { }
                top2.Max = (ushort)Math.Round(scr.top.value2.max);
                top2.Suffix = scr.top.value2.suffix;
                top2.Match = scr.top.value2.match;
                top.Value2 = top2;

                Entry bottom = new Entry();
                bottom.Label = scr.bottom.label;
                bottom.Color = ParseColor(scr.bottom.color);
                SubEntry bottom1 = new SubEntry();
                bottom1.Color = ParseColor(scr.bottom.value1.color);
                bottom1.IsGradient = false;
                try
                {
                    bottom1.IsGradient = scr.bottom.value1.gradient;
                }
                catch { }

                bottom1.Max = (ushort)Math.Round( scr.bottom.value1.max);
                bottom1.Suffix = scr.bottom.value1.suffix;
                bottom1.Match = scr.bottom.value1.match;
                bottom.Value1 = bottom1;

                SubEntry bottom2 = new SubEntry();
                bottom2.Color = ParseColor(scr.bottom.value2.color);
                bottom2.IsGradient = false;
                try
                {
                    bottom2.IsGradient = scr.bottom.value2.gradient;
                }
                catch { }
                bottom2.Max = (ushort)Math.Round(scr.bottom.value2.max);
                bottom2.Suffix = scr.bottom.value2.suffix;
                bottom2.Match = scr.bottom.value2.match;
                bottom.Value2 = bottom2;

                Screen screen = new Screen();
                screen.Top = top;
                screen.Bottom = bottom;

                result.Add(screen);
            }
            return result.ToArray();
        }
    }
}
