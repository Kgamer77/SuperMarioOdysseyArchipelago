﻿using System.Net;
using Newtonsoft.Json;
using Newtonsoft.Json.Converters;
using Newtonsoft.Json.Serialization;
using Shared;

namespace Server;

public class Settings {
    public static Settings Instance = new Settings();
    private static readonly Logger Logger = new Logger("Settings");
    public static Action? LoadHandler;

    static Settings() {
        LoadSettings();
    }

    public static void LoadSettings() {
        if (File.Exists("settings.json")) {
            string text = File.ReadAllText("settings.json");
            try {
                Instance = JsonConvert.DeserializeObject<Settings>(text, new StringEnumConverter(new CamelCaseNamingStrategy())) ?? Instance;
                Logger.Info("Loaded settings from settings.json");
            }
            catch (Exception e) {
                Logger.Warn($"Failed to load settings.json: {e}");
            }
        }
        SaveSettings();
        LoadHandler?.Invoke();
    }

    public static void SaveSettings(bool silent = false) {
        try {
            File.WriteAllText("settings.json", JsonConvert.SerializeObject(Instance, Formatting.Indented, new StringEnumConverter(new CamelCaseNamingStrategy())));
            if (!silent) { Logger.Info("Saved settings to settings.json"); }
        }
        catch (Exception e) {
            Logger.Error($"Failed to save settings.json {e}");
        }
    }

    public ServerTable Server { get; set; } = new ServerTable();
    public FlipTable Flip { get; set; } = new FlipTable();
    public ScenarioTable Scenario { get; set; } = new ScenarioTable();
    public BanListTable BanList { get; set; } = new BanListTable();
    public DiscordTable Discord { get; set; } = new DiscordTable();
    public ShineTable Shines { get; set; } = new ShineTable();
    public ArchipelagoTable Archipelago { get; set; } = new ArchipelagoTable();

    public class ServerTable {
        public string Address { get; set; } = IPAddress.Any.ToString();
        public ushort Port { get; set; } = 1027;
        public ushort MaxPlayers { get; set; } = 8;
    }

    public class ScenarioTable {
        public bool MergeEnabled { get; set; } = false;
    }

    public class BanListTable {
        public bool Enabled { get; set; } = false;
        public ISet<Guid> Players { get; set; } = new SortedSet<Guid>();
        public ISet<string> IpAddresses { get; set; } = new SortedSet<string>();
        public ISet<string> Stages { get; set; } = new SortedSet<string>();
    }

    public class FlipTable {
        public bool Enabled { get; set; } = true;
        public ISet<Guid> Players { get; set; } = new SortedSet<Guid>();
        public FlipOptions Pov { get; set; } = FlipOptions.Both;
    }

    public class DiscordTable {
        public string? Token { get; set; }
        public string Prefix { get; set; } = "$";
        public string? CommandChannel { get; set; }
        public string? LogChannel { get; set; }
    }

    public class ShineTable {
        public bool Enabled { get; set; } = true;
    }

    public class ArchipelagoTable
    {
        public string Server { get; set; } = "localhost";
        public ushort Port { get; set; } = 38281;
        public string Slot { get; set; } = "Super Mario Odyssey";
        public string Password { get; set; } = "";
        public string FillerIndexes { get; set; } = "./recievedIndexes.json";

    }
}
