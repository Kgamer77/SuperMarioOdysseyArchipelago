using System.Collections.Concurrent;
using System.Net;
using System.Numerics;
using System.Text;
using System.Text.Json;
using Server;
using Shared;
using Shared.Packet.Packets;
using Timer = System.Timers.Timer;
using Open.Nat;
using Archipelago.MultiClient.Net;
using System.Reflection;

Server.Server server = new Server.Server();
Server.APClient apClient = new APClient();
HashSet<int> shineBag = new HashSet<int>();
HashSet<int> outfitBag = new HashSet<int>();
HashSet<int> fillerIndex = new HashSet<int>();
Dictionary<int, int> IndexToFiller = new Dictionary<int, int>();
Queue<string> chatMessages = new Queue<string>();
CancellationTokenSource cts = new CancellationTokenSource();
bool restartRequested = false;
Logger consoleLogger = new Logger("Console");
DiscordBot bot = new DiscordBot();
await bot.Run();

Dictionary<int, bool> giftMoons = new Dictionary<int, bool>()
        {
            { 227 , false },
            { 204 , false },
            { 692 , false },
            { 403 , false },
            { 133 , false },
            { 372 , false },
            { 46 , false },
            { 443 , false },
            { 694 , false },
            { 265 , false },
            { 318 , false },
            { 1000 , false },
            { 605 , false },
            { 1122 , false },
            { 1059 , false },
            { 501 , false },
            { 414 , false },
            { 42 , false },
            { 41 , false },
            { 44 , false },
            { 43 , false },
            { 910 , false },
            { 905 , false },
            { 904 , false },
            { 907 , false },
            { 913 , false },
            { 50 , false },
            { 578 , false },
            { 714 , false },
            { 877 , false },
            { 878 , false },
            { 533 , false },
            { 712 , false },
            { 498 , false },
            { 457 , false },
            { 580 , false },
            { 874 , false },
            { 474 , false },
            { 973 , false },
            { 409 , false },
            { 1001 , false },
            { 943 , false },
            { 941 , false },
            { 230 , false },
            { 211 , false },
            { 565 , false },
            { 430 , false },
            { 138 , false },
            { 398 , false },
            { 101 , false },
            { 460 , false },
            { 868 , false },
            { 294 , false },
            { 360 , false },
            { 1157 , false },
            { 933 , false },
            { 209 , false },
            { 544 , false },
            { 408 , false },
            { 155 , false },
            { 1118 , false },
            { 394 , false },
            { 75 , false },
            { 477 , false },
            { 19 , false },
            { 268 , false },
            { 1117 , false },
            { 338 , false },
            { 581 , false },
            { 1119 , false },
            { 539 , false },
            { 496 , false },
            { 129 , false }
        };

async Task PersistIndexes()
{
    try
    {
        string fillerJson = JsonSerializer.Serialize(fillerIndex);
        await File.WriteAllTextAsync(Settings.Instance.Archipelago.FillerIndexes, fillerJson);
    }
    catch (Exception ex)
    {
        consoleLogger.Error(ex);
    }
}

async Task LoadFiller()
{
    Settings.Instance.Archipelago.FillerIndexes = "recievedIndexes" + apClient.session.RoomState.Seed + ".json";
    Settings.SaveSettings();
    try
    {
        
        string fillerJson = await File.ReadAllTextAsync(Settings.Instance.Archipelago.FillerIndexes);
        var loadedIndexes = JsonSerializer.Deserialize<HashSet<int>>(fillerJson);

        if (loadedIndexes is not null) fillerIndex = loadedIndexes;
    }
    catch (FileNotFoundException)
    {
        await PersistIndexes();
    }
    catch (Exception ex)
    {
        consoleLogger.Error(ex);
    }
}

server.ClientJoined += (c, _) => {
    c.Metadata["shineSync"] = new ConcurrentBag<int>();
    c.Metadata["itemSync"] = new ConcurrentBag<int>();
    c.Metadata["fillerSync"] = new ConcurrentBag<int>();
    c.Metadata["messageLog"] = new List<string>();
    c.Metadata["loadedSave"] = false;
    c.Metadata["scenario"] = (byte?) 0;
    c.Metadata["2d"] = false;
    c.Metadata["speedrun"] = false;
};

async Task ClientSyncShineBag(Client client) {
    try {
        if ((bool?) client.Metadata["speedrun"] ?? false) return;
        ConcurrentBag<int> clientBag = (ConcurrentBag<int>) (client.Metadata["shineSync"] ??= new ConcurrentBag<int>());
        foreach (int shine in shineBag.Except(clientBag).ToArray()) {
            if (!client.Connected) return;
            await client.Send(new ShinePacket {
                ShineId = shine
            });
            clientBag.Add(shine);
        }
    } catch {
        // errors that can happen when sending will crash the server :)
    }
}

async void SyncShineBag() {
    try {

        await Parallel.ForEachAsync(server.ClientsConnected.ToArray(), async (client, _) => await ClientSyncShineBag(client));
    } catch {
        // errors that can happen shines change will crash the server :)
    }
}

async Task ClientSyncItem(Client client)
{
    try
    {
        ConcurrentBag<int> clientBag = (ConcurrentBag<int>)(client.Metadata["itemSync"] ??= new ConcurrentBag<int>());

        foreach (int item in outfitBag.Except(clientBag).ToArray())
        {
            string s = APClient.inverseShopItems[item];
            int incomingType = 0;

            if (!s.Contains("Sticker"))
            {
                if (s.Contains("Cap"))
                {
                    s = s.Replace("Cap", "");
                    incomingType = 1;
                }
                if (s.Contains("Clothes"))
                {
                    s = s.Replace("Clothes", "");
                    incomingType = 0;
                }
            }

            if (s.Contains("Sticker"))
            {
                incomingType = 3;
            }
            if (s.Contains("Souvenir"))
            {
                incomingType = 2;
            }
            //consoleLogger.Info($"Parsed Item {s } {incomingType}");

            if (!client.Connected) return;
            await client.Send(new ItemPacket
            {
                name = s,
                itemType = incomingType
            });
            clientBag.Add(item);
        }

    }
    catch
    {
        // errors that can happen when sending will crash the server :)
    }
}

async void SyncItem()
{
    try
    {
        await Parallel.ForEachAsync(server.ClientsConnected.ToArray(), async (client, _) => await ClientSyncItem(client));
    }
    catch
    {
        // errors that can happen shines change will crash the server :)
    }
}

async Task ClientSyncFillerItem(Client client)
{
    try
    {
        ConcurrentBag<int> clientBag = (ConcurrentBag<int>)(client.Metadata["fillerSync"] ??= new ConcurrentBag<int>());

        foreach (int item in fillerIndex.Except(clientBag).ToArray())
        {
            if (!client.Connected) return;
            await client.Send(new FillerPacket
            {
                itemType = IndexToFiller[item] - 9990
            });
            clientBag.Add(item);
        }
        
    }
    catch
    {
        // errors that can happen when sending will crash the server :)
    }
}

async void SyncFillerItem()
{
    try
    {
        await PersistIndexes();
        await Parallel.ForEachAsync(server.ClientsConnected.ToArray(), async (client, _) => await ClientSyncFillerItem(client));
    }
    catch
    {
        // errors that can happen shines change will crash the server :)
    }
}

async Task ClientSendLogMessage(Client client)
{
    try
    {
        List<string> clientLog = (List<string>)(client.Metadata["messageLog"] ??= new List<string>());

        if (!client.Connected) return;

        if (chatMessages.Count == 0)
        {
            clientLog.Clear();
        }

        if (chatMessages.Count > 0 && clientLog.Count < 3)
            clientLog.Add(chatMessages.Dequeue());

        switch (clientLog.Count)
        {
            case 0:
                await client.Send(new ArchipelagoChatMessage
                {
                    message3 = "",
                    message2 = "",
                    message1 = ""
                });
                break;
            case 1:
                await client.Send(new ArchipelagoChatMessage
                {
                    message3 = clientLog[0],
                    message2 = "",
                    message1 = ""
                });
                break;
            case 2:
                await client.Send(new ArchipelagoChatMessage
                {
                    message3 = clientLog[1],
                    message2 = clientLog[0],
                    message1 = ""
                });
                break;
            case 3:
                await client.Send(new ArchipelagoChatMessage
                {
                    message3 = clientLog[2],
                    message2 = clientLog[1],
                    message1 = clientLog[0]
                });
                break;

        }

        if (clientLog.Count == 3 && chatMessages.Count > 0)
        {
            clientLog.RemoveAt(0);
        }

    }
    catch
    {
        // errors that can happen when sending will crash the server :)
    }
}

async void SendLogMessage()
{
    try
    {
        await Parallel.ForEachAsync(server.ClientsConnected.ToArray(), async (client, _) => await ClientSendLogMessage(client));
    }
    catch
    {
        // errors that can happen shines change will crash the server :)
    }
}

Timer timer = new Timer(120000);
timer.AutoReset = true;
timer.Enabled = true;
timer.Elapsed += (_, _) => { SyncShineBag(); };
timer.Start();

Timer messageTimer = new Timer(4000);
messageTimer.AutoReset = true;
messageTimer.Enabled = true;
messageTimer.Elapsed += (_, _) => { SendLogMessage(); };
messageTimer.Start();

Timer grandTimer = new Timer(35000);
grandTimer.AutoReset = false;
grandTimer.Enabled = true;
grandTimer.Elapsed += (_, _) => { SyncShineBag(); };

float MarioSize(bool is2d) => is2d ? 180 : 160;

void flipPlayer(Client c, ref PlayerPacket pp) {
    pp.Position += Vector3.UnitY * MarioSize((bool) c.Metadata["2d"]!);
    pp.Rotation *= (
        Quaternion.CreateFromRotationMatrix(Matrix4x4.CreateRotationX(MathF.PI))
      * Quaternion.CreateFromRotationMatrix(Matrix4x4.CreateRotationY(MathF.PI))
    );
};

void logError(Task x) {
    if (x.Exception != null) {
        consoleLogger.Error(x.Exception.ToString());
    }
};

server.PacketHandler = (c, p) => {
    switch (p) {
        case GamePacket gamePacket: {
            if (BanLists.Enabled && BanLists.IsStageBanned(gamePacket.Stage)) {
                c.Logger.Warn($"Crashing player for entering banned stage {gamePacket.Stage}.");
                BanLists.Crash(c, false, false, 500);
                return false;
            }
            c.Logger.Info($"Got game packet {gamePacket.Stage}->{gamePacket.ScenarioNum}");

            // reset lastPlayerPacket on stage changes
            object? old = null;
            c.Metadata.TryGetValue("lastGamePacket", out old);
            if (old != null && ((GamePacket) old).Stage != gamePacket.Stage) {
                c.Metadata["lastPlayerPacket"] = null;
            }

            c.Metadata["scenario"] = gamePacket.ScenarioNum;
            c.Metadata["2d"] = gamePacket.Is2d;
            c.Metadata["lastGamePacket"] = gamePacket;

            switch (gamePacket.Stage) {
                case "CapWorldHomeStage" when gamePacket.ScenarioNum == 0:
                    c.Metadata["speedrun"] = true;
                    ((ConcurrentBag<int>) (c.Metadata["shineSync"] ??= new ConcurrentBag<int>())).Clear();
                    shineBag.Clear();
                    c.Logger.Info("Entered Cap on new save, preventing moon sync until Cascade");
                    break;
                case "WaterfallWorldHomeStage":
                    bool wasSpeedrun = (bool) c.Metadata["speedrun"]!;
                    c.Metadata["speedrun"] = false;
                    if (wasSpeedrun)
                        Task.Run(async () => {
                            c.Logger.Info("Entered Cascade with moon sync disabled, enabling moon sync");
                            await Task.Delay(15000);
                            await ClientSyncShineBag(c);
                        });
                    break;
                case "ClashWorldHomeStage":
                    apClient.send_location(2501);
                    break;
                case "PeachWorldHomeStage":
                    if (gamePacket.ScenarioNum > 1)
                        apClient.send_location(2500);
                    break;
            }

            if (Settings.Instance.Scenario.MergeEnabled) {
                server.BroadcastReplace(gamePacket, c, (from, to, gp) => {
                    gp.ScenarioNum = (byte?) to.Metadata["scenario"] ?? 200;
#pragma warning disable CS4014
                    to.Send(gp, from).ContinueWith(logError);
#pragma warning restore CS4014
                });
                return false;
            }

            break;
        }

        case TagPacket tagPacket: {
            // c.Logger.Info($"Got tag packet: {tagPacket.IsIt}");
            c.Metadata["lastTagPacket"] = tagPacket;
            if ((tagPacket.UpdateType & TagPacket.TagUpdate.State) != 0) c.Metadata["seeking"] = tagPacket.IsIt;
            if ((tagPacket.UpdateType & TagPacket.TagUpdate.Time) != 0)
                c.Metadata["time"] = new Time(tagPacket.Minutes, tagPacket.Seconds, DateTime.Now);
            break;
        }

        case CapturePacket capturePacket: {
            // c.Logger.Info($"Got capture packet: {capturePacket.ModelName}");
            c.Metadata["lastCapturePacket"] = capturePacket;
            break;
        }

        case CostumePacket costumePacket:
            c.Logger.Info($"Got costume packet: {costumePacket.BodyName}, {costumePacket.CapName}");
            c.Metadata["lastCostumePacket"] = costumePacket;
            c.CurrentCostume = costumePacket;
#pragma warning disable CS4014
            ClientSyncShineBag(c); //no point logging since entire def has try/catch
#pragma warning restore CS4014
            c.Metadata["loadedSave"] = true;
            break;

        case ShinePacket shinePacket: {
            if (!Settings.Instance.Shines.Enabled) return false;
            if (c.Metadata["loadedSave"] is false) break;
            c.Logger.Info($"Got moon location {shinePacket.ShineId}");
            if (shinePacket.ShineId != 1123)
                apClient.send_location(shinePacket.ShineId);
            else
            {
                object? old = null;
                c.Metadata.TryGetValue("lastGamePacket", out old);
                if (old != null)
                    apClient.send_location(APClient.darkSideHintArts[((GamePacket)old).Stage]);
            }
            if (giftMoons.ContainsKey(shinePacket.ShineId))
                if (giftMoons[shinePacket.ShineId])
                    shineBag.Add(shinePacket.ShineId);
                else { giftMoons.Remove(shinePacket.ShineId); }

            break;
        }

        case ItemPacket itemPacket: {
                c.Logger.Info($"Got Item {itemPacket.name} {itemPacket.itemType}");
                string item = itemPacket.name;
                if (itemPacket.itemType == 0)
                    item += "Clothes";
                if (itemPacket.itemType == 1)
                    item += "Cap";
                apClient.send_location(APClient.shopItems[item]);
                break;
        }

        case PlayerPacket playerPacket: {
            c.Metadata["lastPlayerPacket"] = playerPacket;
            // flip for all
            if (   Settings.Instance.Flip.Enabled
                && Settings.Instance.Flip.Pov is FlipOptions.Both or FlipOptions.Others
                && Settings.Instance.Flip.Players.Contains(c.Id)
            ) {
                flipPlayer(c, ref playerPacket);
#pragma warning disable CS4014
                server.Broadcast(playerPacket, c).ContinueWith(logError);
#pragma warning restore CS4014
                return false;
            }
            // flip only for specific clients
            if (   Settings.Instance.Flip.Enabled
                && Settings.Instance.Flip.Pov is FlipOptions.Both or FlipOptions.Self
                && !Settings.Instance.Flip.Players.Contains(c.Id)
            ) {
                server.BroadcastReplace(playerPacket, c, (from, to, sp) => {
                    if (Settings.Instance.Flip.Players.Contains(to.Id)) {
                        flipPlayer(c, ref sp);
                    }
#pragma warning disable CS4014
                    to.Send(sp, from).ContinueWith(logError);
#pragma warning restore CS4014
                });
                return false;
            }
            break;
        }
    }

    return true; // Broadcast packet to all other clients
};

(HashSet<string> failToFind, HashSet<Client> toActUpon, List<(string arg, IEnumerable<string> amb)> ambig) MultiUserCommandHelper(string[] args) {
    HashSet<string> failToFind = new();
    HashSet<Client> toActUpon;
    List<(string arg, IEnumerable<string> amb)> ambig = new();
    if (args[0] == "*") {
        toActUpon = new(server.Clients.Where(c => c.Connected));
    }
    else {
        toActUpon = args[0] == "!*" ? new(server.Clients.Where(c => c.Connected)) : new();
        for (int i = (args[0] == "!*" ? 1 : 0); i < args.Length; i++) {
            string arg = args[i];
            IEnumerable<Client> search = server.Clients.Where(c => c.Connected && (
                c.Name.ToLower().StartsWith(arg.ToLower())
                || (Guid.TryParse(arg, out Guid res) && res == c.Id)
                || (IPAddress.TryParse(arg, out IPAddress? ip) && ip.Equals(((IPEndPoint) c.Socket!.RemoteEndPoint!).Address))
            ));
            if (!search.Any()) {
                failToFind.Add(arg); //none found
            }
            else if (search.Count() > 1) {
                Client? exact = search.FirstOrDefault(x => x.Name == arg);
                if (!ReferenceEquals(exact, null)) {
                    //even though multiple matches, since exact match, it isn't ambiguous
                    if (args[0] == "!*") {
                        toActUpon.Remove(exact);
                    }
                    else {
                        toActUpon.Add(exact);
                    }
                }
                else {
                    if (!ambig.Any(x => x.arg == arg)) {
                        ambig.Add((arg, search.Select(x => x.Name))); //more than one match
                    }
                    foreach (var rem in search.ToList()) { //need copy because can't remove from list while iterating over it
                        toActUpon.Remove(rem);
                    }
                }
            }
            else {
                //only one match, so autocomplete
                if (args[0] == "!*") {
                    toActUpon.Remove(search.First());
                }
                else {
                    toActUpon.Add(search.First());
                }
            }
        }
    }
    return (failToFind, toActUpon, ambig);
}

CommandHandler.RegisterCommand("rejoin", args => {
    if (args.Length == 0) {
        return "Usage: rejoin <* | !* (usernames to not rejoin...) | (usernames to rejoin...)>";
    }

    var res = MultiUserCommandHelper(args);

    StringBuilder sb = new StringBuilder();
    sb.Append(res.toActUpon.Count > 0 ? "Rejoined: " + string.Join(", ", res.toActUpon.Select(x => $"\"{x.Name}\"")) : "");
    sb.Append(res.failToFind.Count > 0 ? "\nFailed to find matches for: " + string.Join(", ", res.failToFind.Select(x => $"\"{x.ToLower()}\"")) : "");
    if (res.ambig.Count > 0) {
        res.ambig.ForEach(x => {
            sb.Append($"\nAmbiguous for \"{x.arg}\": {string.Join(", ", x.amb.Select(x => $"\"{x}\""))}");
        });
    }

    foreach (Client user in res.toActUpon) {
        user.Dispose();
    }

    return sb.ToString();
});

CommandHandler.RegisterCommand("crash", args => {
    if (args.Length == 0) {
        return "Usage: crash <* | !* (usernames to not crash...) | (usernames to crash...)>";
    }

    var res = MultiUserCommandHelper(args);

    StringBuilder sb = new StringBuilder();
    sb.Append(res.toActUpon.Count > 0 ? "Crashed: " + string.Join(", ", res.toActUpon.Select(x => $"\"{x.Name}\"")) : "");
    sb.Append(res.failToFind.Count > 0 ? "\nFailed to find matches for: " + string.Join(", ", res.failToFind.Select(x => $"\"{x.ToLower()}\"")) : "");
    if (res.ambig.Count > 0) {
        res.ambig.ForEach(x => {
            sb.Append($"\nAmbiguous for \"{x.arg}\": {string.Join(", ", x.amb.Select(x => $"\"{x}\""))}");
        });
    }

    foreach (Client user in res.toActUpon) {
        BanLists.Crash(user);
    }

    return sb.ToString();
});

CommandHandler.RegisterCommand("ban",   args => { return BanLists.HandleBanCommand(args, (args) => MultiUserCommandHelper(args)); });
CommandHandler.RegisterCommand("unban", args => { return BanLists.HandleUnbanCommand(args); });

CommandHandler.RegisterCommand("send", args => {
    const string optionUsage = "Usage: send <stage> <id> <scenario[-1..127]> <player/*>";
    if (args.Length < 4) {
        return optionUsage;
    }

    string? stage = Stages.Input2Stage(args[0]);
    if (stage == null) {
        return "Invalid Stage Name! ```" + Stages.KingdomAliasMapping() + "```";
    }

    string id = args[1];

    if (!sbyte.TryParse(args[2], out sbyte scenario) || scenario < -1)
        return $"Invalid scenario number {args[2]} (range: [-1 to 127])";
    Client[] players = args[3] == "*"
        ? server.Clients.Where(c => c.Connected).ToArray()
        : server.Clients.Where(c =>
                c.Connected
                && args[3..].Any(x => c.Name.StartsWith(x) || (Guid.TryParse(x, out Guid result) && result == c.Id)))
            .ToArray();
    Parallel.ForEachAsync(players, async (c, _) => {
        await c.Send(new ChangeStagePacket {
            Stage = stage,
            Id = id,
            Scenario = scenario,
            SubScenarioType = 0
        });
    }).Wait();
    return $"Sent players to {stage}:{scenario}";
});

CommandHandler.RegisterCommand("sendall", args => {
    const string optionUsage = "Usage: sendall <stage>";
    if (args.Length < 1) {
        return optionUsage;
    }

    string? stage = Stages.Input2Stage(args[0]);
    if (stage == null) {
        return "Invalid Stage Name! ```" + Stages.KingdomAliasMapping() + "```";
    }

    Client[] players = server.Clients.Where(c => c.Connected).ToArray();

    Parallel.ForEachAsync(players, async (c, _) => {
        await c.Send(new ChangeStagePacket {
            Stage = stage,
            Id = "",
            Scenario = -1,
            SubScenarioType = 0
        });
    }).Wait();

    return $"Sent players to {stage}:{-1}";
});

CommandHandler.RegisterCommand("scenario", args => {
    const string optionUsage = "Valid options: merge [true/false]";
    if (args.Length < 1)
        return optionUsage;
    switch (args[0]) {
        case "merge" when args.Length == 2: {
            if (bool.TryParse(args[1], out bool result)) {
                Settings.Instance.Scenario.MergeEnabled = result;
                Settings.SaveSettings();
                return result ? "Enabled scenario merge" : "Disabled scenario merge";
            }

            return optionUsage;
        }
        case "merge" when args.Length == 1: {
            return $"Scenario merging is {Settings.Instance.Scenario.MergeEnabled}";
        }
        default:
            return optionUsage;
    }
});

CommandHandler.RegisterCommand("tag", args => {
    const string optionUsage =
        "Valid options:\n\ttime <user/*> <minutes[0-65535]> <seconds[0-59]>\n\tseeking <user/*> <true/false>\n\tstart <time> <seekers>";
    if (args.Length < 3)
        return optionUsage;
    switch (args[0]) {
        case "time" when args.Length == 4: {
            if (args[1] != "*" && server.Clients.All(x => x.Name != args[1])) return $"Cannot find user {args[1]}";
            Client? client = server.Clients.FirstOrDefault(x => x.Name == args[1]);
            if (!ushort.TryParse(args[2], out ushort minutes))
                return $"Invalid time for minutes {args[2]} (range: 0-65535)";
            if (!byte.TryParse(args[3], out byte seconds) || seconds >= 60)
                return $"Invalid time for seconds {args[3]} (range: 0-59)";
            TagPacket tagPacket = new TagPacket {
                UpdateType = TagPacket.TagUpdate.Time,
                Minutes = minutes,
                Seconds = seconds
            };
            if (args[1] == "*")
                server.Broadcast(tagPacket);
            else
                client?.Send(tagPacket);
            return $"Set time for {(args[1] == "*" ? "everyone" : args[1])} to {minutes}:{seconds}";
        }
        case "seeking" when args.Length == 3: {
            if (args[1] != "*" && server.Clients.All(x => x.Name != args[1])) return $"Cannot find user {args[1]}";
            Client? client = server.Clients.FirstOrDefault(x => x.Name == args[1]);
            if (!bool.TryParse(args[2], out bool seeking)) return $"Usage: tag seeking {args[1]} <true/false>";
            TagPacket tagPacket = new TagPacket {
                UpdateType = TagPacket.TagUpdate.State,
                IsIt = seeking
            };
            if (args[1] == "*")
                server.Broadcast(tagPacket);
            else
                client?.Send(tagPacket);
            return $"Set {(args[1] == "*" ? "everyone" : args[1])} to {(seeking ? "seeker" : "hider")}";
        }
        case "start" when args.Length > 2: {
            if (!byte.TryParse(args[1], out byte time)) return $"Invalid countdown seconds {args[1]} (range: 0-255)";
            string[] seekerNames = args[2..];
            Client[] seekers = server.Clients.Where(c => seekerNames.Contains(c.Name)).ToArray();
            if (seekers.Length != seekerNames.Length)
                return
                    $"Couldn't find seeker{(seekerNames.Length > 1 ? "s" : "")}: {string.Join(", ", seekerNames.Where(name => server.Clients.All(c => c.Name != name)))}";
            Task.Run(async () => {
                int realTime = 1000 * time;
                await Task.Delay(realTime);
                await Task.WhenAll(
                    Parallel.ForEachAsync(seekers, async (seeker, _) =>
                        await server.Broadcast(new TagPacket {
                            UpdateType = TagPacket.TagUpdate.State,
                            IsIt = true
                        }, seeker)),
                    Parallel.ForEachAsync(server.Clients.Except(seekers), async (hider, _) =>
                        await server.Broadcast(new TagPacket {
                            UpdateType = TagPacket.TagUpdate.State,
                            IsIt = false
                        }, hider)
                    )
                );
                consoleLogger.Info($"Started game with seekers {string.Join(", ", seekerNames)}");
            });
            return $"Starting game in {time} seconds with seekers {string.Join(", ", seekerNames)}";
        }
        default:
            return optionUsage;
    }
});

CommandHandler.RegisterCommand("maxplayers", args => {
    const string optionUsage = "Valid usage: maxplayers <playercount>";
    if (args.Length != 1) return optionUsage;
    if (!ushort.TryParse(args[0], out ushort maxPlayers)) return optionUsage;
    Settings.Instance.Server.MaxPlayers = maxPlayers;
    Settings.SaveSettings();
    foreach (Client client in server.Clients)
        client.Dispose(); // reconnect all players
    return $"Saved and set max players to {maxPlayers}";
});

CommandHandler.RegisterCommand("list",
    _ => $"List: {string.Join("\n\t", server.Clients.Where(x => x.Connected).Select(x => $"{x.Name} ({x.Id})"))}");

CommandHandler.RegisterCommand("flip", args => {
    const string optionUsage =
        "Valid options: \n\tlist\n\tadd <user id>\n\tremove <user id>\n\tset <true/false>\n\tpov <both/self/others>";
    if (args.Length < 1)
        return optionUsage;
    switch (args[0]) {
        case "list" when args.Length == 1:
            return "User ids: " + string.Join(", ", Settings.Instance.Flip.Players.ToList());
        case "add" when args.Length == 2: {
            if (Guid.TryParse(args[1], out Guid result)) {
                Settings.Instance.Flip.Players.Add(result);
                Settings.SaveSettings();
                return $"Added {result} to flipped players";
            }

            return $"Invalid user id {args[1]}";
        }
        case "remove" when args.Length == 2: {
            if (Guid.TryParse(args[1], out Guid result)) {
                string output = Settings.Instance.Flip.Players.Remove(result)
                    ? $"Removed {result} to flipped players"
                    : $"User {result} wasn't in the flipped players list";
                Settings.SaveSettings();
                return output;
            }

            return $"Invalid user id {args[1]}";
        }
        case "set" when args.Length == 2: {
            if (bool.TryParse(args[1], out bool result)) {
                Settings.Instance.Flip.Enabled = result;
                Settings.SaveSettings();
                return result ? "Enabled player flipping" : "Disabled player flipping";
            }

            return optionUsage;
        }
        case "pov" when args.Length == 2: {
            if (Enum.TryParse(args[1], true, out FlipOptions result)) {
                Settings.Instance.Flip.Pov = result;
                Settings.SaveSettings();
                return $"Point of view set to {result}";
            }

            return optionUsage;
        }
        default:
            return optionUsage;
    }
});

CommandHandler.RegisterCommand("shine", args => {
    const string optionUsage = "Valid options: list, clear, sync, send, set";
    if (args.Length < 1)
        return optionUsage;
    switch (args[0]) {
        case "list" when args.Length == 1:
            return $"Shines: {string.Join(", ", shineBag)}";
        case "clear" when args.Length == 1:
            shineBag.Clear();


            foreach (ConcurrentBag<int> playerBag in server.Clients.Select(serverClient =>
                (ConcurrentBag<int>)serverClient.Metadata["shineSync"]!)) playerBag?.Clear();

            return "Cleared shine bags";
        case "sync" when args.Length == 1:
            SyncShineBag();
            return "Synced shine bag automatically";
        case "send" when args.Length >= 3:
            if (int.TryParse(args[1], out int id)) {
                Client[] players = args[2] == "*"
                    ? server.Clients.Where(c => c.Connected).ToArray()
                    : server.Clients.Where(c => c.Connected && args[3..].Contains(c.Name)).ToArray();
                Parallel.ForEachAsync(players, async (c, _) => {
                    await c.Send(new ShinePacket {
                        ShineId = id
                    });
                }).Wait();
                return $"Sent Shine Num {id}";
            }

            return optionUsage;
        case "set" when args.Length == 2: {
            if (bool.TryParse(args[1], out bool result)) {
                Settings.Instance.Shines.Enabled = result;
                Settings.SaveSettings();
                return result ? "Enabled shine sync" : "Disabled shine sync";
            }

            return optionUsage;
        }
        default:
            return optionUsage;
    }
});

CommandHandler.RegisterCommand("loadsettings", _ => {
    Settings.LoadSettings();
    return "Loaded settings.json";
});

CommandHandler.RegisterCommand("restartserver", args =>
{
    if (args.Length != 0)
    {
        return "Usage: restartserver (no arguments)";
    }
    else
    {
        consoleLogger.Info("Received restartserver command");
        restartRequested = true;
        cts.Cancel();
        return "Restarting...";
    }
});
CommandHandler.RegisterCommand("reconnect", args =>
{
    const string optionUsage = "Valid options: (no arguments), <slot>, <slot> <password>";
    // Make this a switch-case later
    if (args.Length == 0)
    {
        connectAP();
        return "";
    }
    else if (args.Length == 1)
    {
        Settings.Instance.Archipelago.Slot = args[0];
        Settings.SaveSettings();
        connectAP();
        return "";
    }
    else if (args.Length == 2)
    {
        Settings.Instance.Archipelago.Slot = args[0];
        Settings.Instance.Archipelago.Password = args[1];
        Settings.SaveSettings();
        connectAP();
        return "";
    }
    return optionUsage;
});

CommandHandler.RegisterCommand("connect", args =>
{
    const string optionUsage = "Valid options: (no arguments), <address:port>, <address> <port>, <address> <port> <slot>, <address> <port> <slot> <password>";
    // Make this a switch-case later
    if (args.Length == 0)
    {
        connectAP();
        return "";
    }
    else if (args.Length == 1)
    {
        if (args[0].Contains(":"))
        {
            Settings.Instance.Archipelago.Server = args[0].Split(":")[0];
            Settings.Instance.Archipelago.Port = ushort.Parse(args[0].Split(":")[1]);
        }
        else
        {
            Settings.Instance.Archipelago.Server = args[0];
        }
        Settings.SaveSettings();
        connectAP();
        return "";
    }
    else if (args.Length == 2)
    {
        Settings.Instance.Archipelago.Server = args[0];
        Settings.Instance.Archipelago.Port = ushort.Parse(args[1]);
        Settings.SaveSettings();
        connectAP();
        return "";
    }
    else if (args.Length == 3)
    {
        Settings.Instance.Archipelago.Server = args[0];
        Settings.Instance.Archipelago.Port = ushort.Parse(args[1]);
        Settings.Instance.Archipelago.Slot = args[2];
        Settings.SaveSettings();
        connectAP();
        return "";
    }
    else if (args.Length == 4)
    {
        Settings.Instance.Archipelago.Server = args[0];
        Settings.Instance.Archipelago.Port = ushort.Parse(args[1]);
        Settings.Instance.Archipelago.Slot = args[2];
        Settings.Instance.Archipelago.Password = args[3];
        Settings.SaveSettings();
        connectAP();
        return "";
    }
    return optionUsage;
});


CommandHandler.RegisterCommand("apsync", args =>
{
    const string optionUsage = "Valid options: (no arguments)";
    // Make this a switch-case later
    if (args.Length == 0)
    {
        ApSync();
        return "Syncing AP Items";
    }
    return optionUsage;
});

Console.CancelKeyPress += (_, e) => {
    e.Cancel = true;
    consoleLogger.Info("Received Ctrl+C");
    cts.Cancel();
};

CommandHandler.RegisterCommandAliases(_ => {
    cts.Cancel();
    return "Shutting down";
}, "exit", "quit", "q");



#pragma warning disable CS4014
Task.Run(() => {
    consoleLogger.Info("Run help command for valid commands.");
    while (true) {
        string? text = Console.ReadLine();
        if (text != null) {
            foreach (string returnString in CommandHandler.GetResult(text).ReturnStrings) {
                consoleLogger.Info(returnString);
            }
        }
    }
}).ContinueWith(logError);
#pragma warning restore CS4014
async void Upnp()
{
    var discoverer = new NatDiscoverer();
    var cts = new CancellationTokenSource(10000);
    var device = await discoverer.DiscoverDeviceAsync(PortMapper.Upnp, cts);
    var ip = await device.GetExternalIPAsync();
    Console.WriteLine("The external IP Address is: {0} ", ip);

    try
    {
        await device.CreatePortMapAsync(new Mapping(Protocol.Tcp, Settings.Instance.Archipelago.Port, Settings.Instance.Archipelago.Port, "SMO Online Server"));
        await device.CreatePortMapAsync(new Mapping(Protocol.Udp, Settings.Instance.Archipelago.Port, Settings.Instance.Archipelago.Port, "SMO Online Server"));
    }
    catch (Exception ex) { consoleLogger.Info($"Failed to map port. {ex.Message}"); }


}

async void connectAP()
{
    apClient.Connect(Settings.Instance.Archipelago.Server, Settings.Instance.Archipelago.Slot, Settings.Instance.Archipelago.Password, Settings.Instance.Archipelago.Port);
    if (!apClient.loginFailed)
    {
        await LoadFiller();
        chatMessages.Enqueue("Connected to Archipelago");
    }
    else
    {
        chatMessages.Enqueue(apClient.get_error_message());
        return;
    }

    apClient.session.Items.ItemReceived += (receivedItemsHelper) =>
    {

        var itemReceivedName = receivedItemsHelper.PeekItem();
        consoleLogger.Info($"Received {itemReceivedName.ItemName} ID {itemReceivedName.ItemId}");
        if (itemReceivedName.ItemGame == "Super Mario Odyssey")
        {
            if (itemReceivedName.ItemId < 2500)
            {
                if (giftMoons.ContainsKey((int)itemReceivedName.ItemId))
                {
                    giftMoons[(int)itemReceivedName.ItemId] = true;
                    receivedItemsHelper.DequeueItem();
                    return;
                }
                shineBag.Add((int)itemReceivedName.ItemId);
                if (itemReceivedName.ItemName.Contains("Multi") && itemReceivedName.ItemId == itemReceivedName.LocationId)
                {
                    consoleLogger.Warn("Item at it's own location!");
                    receivedItemsHelper.DequeueItem();
                    grandTimer.Start();
                    return;
                }
            }
            SyncShineBag();
            if (itemReceivedName.ItemId >= 2502 && itemReceivedName.ItemId < 9990)
            {
                outfitBag.Add((int)itemReceivedName.ItemId);
            }
            SyncItem();
            if (itemReceivedName.ItemId >= 9990)
            {
                if (!fillerIndex.Contains(receivedItemsHelper.Index))
                {
                    fillerIndex.Add(receivedItemsHelper.Index);
                    IndexToFiller.Add(receivedItemsHelper.Index, (int)itemReceivedName.ItemId);
                }
            }
            SyncFillerItem();

        }
        if (itemReceivedName.ItemGame == "Super Mario Odyssey" && itemReceivedName.ItemName == apClient.get_goal())
            apClient.session.SetGoalAchieved();
        receivedItemsHelper.DequeueItem();
    };

    ApSync();

    apClient.session.MessageLog.OnMessageReceived += async (incomingMessage) =>
    {
        switch (incomingMessage)
        {
            case HintItemSendLogMessage hintLogMessage:
            {
                if (hintLogMessage.IsReceiverTheActivePlayer && !hintLogMessage.IsFound)
                {
                    string message = "";
                    if (hintLogMessage.Receiver.Name != hintLogMessage.Sender.Name)
                    {
                        message = $"has your {hintLogMessage.Item.ItemName}";

                        if (Constants.ChatMessageSize - message.Length > 0)
                        {
                            if (Constants.ChatMessageSize - message.Length > hintLogMessage.Sender.Name.Length)
                                message = hintLogMessage.Sender.Name + message;
                            else
                                message = hintLogMessage.Sender.Name.Substring(0, Constants.ChatMessageSize - message.Length) + message;
                        }

                        chatMessages.Enqueue(message);

                        message = "at ";
                        if (Constants.ChatMessageSize - message.Length > hintLogMessage.Item.LocationName.Length)
                            message += hintLogMessage.Item.LocationName;
                        else
                            message += hintLogMessage.Item.LocationName.Substring(0, Constants.ChatMessageSize - message.Length);

                        chatMessages.Enqueue(message);
                    }
                    else
                    {
                        message = $"{hintLogMessage.Item.ItemName} is at {hintLogMessage.Item.LocationName}";
                        chatMessages.Enqueue(message);
                    }
                }

                if (hintLogMessage.IsSenderTheActivePlayer && !(hintLogMessage.Receiver.Name == hintLogMessage.Sender.Name) && !hintLogMessage.IsFound)
                {
                    string message = "";

                    if (hintLogMessage.Sender.Name.Length + hintLogMessage.Item.ItemName.Length + 3 < Constants.ChatMessageSize)
                        message = $"{hintLogMessage.Sender.Name}'s {hintLogMessage.Item.ItemName}";
                    else if (hintLogMessage.Item.ItemName.Length < Constants.ChatMessageSize)
                        message += hintLogMessage.Item.ItemName;
                    else message += hintLogMessage.Item.ItemName.Substring(0, Constants.ChatMessageSize);

                    chatMessages.Enqueue(message);

                    message = $"is at {hintLogMessage.Item.LocationName}";
                    chatMessages.Enqueue(message);
                }
                break;
            }

            case ItemSendLogMessage sendLogMessage:
            {
                if (sendLogMessage.IsReceiverTheActivePlayer)
                {
                    string message = $"Got {sendLogMessage.Item.ItemName} from ";
                    if (Constants.ChatMessageSize - message.Length > 0)
                    {
                        if (Constants.ChatMessageSize - message.Length > sendLogMessage.Sender.Name.Length)
                            message += sendLogMessage.Sender.Name;
                        else
                            message += sendLogMessage.Sender.Name.Substring(0, Constants.ChatMessageSize - message.Length);
                    }

                    chatMessages.Enqueue(message);
                }

                if (sendLogMessage.IsSenderTheActivePlayer && !(sendLogMessage.Receiver.Name == sendLogMessage.Sender.Name))
                {
                    string message = $"Sent {sendLogMessage.Item.ItemName} to ";
                    if (Constants.ChatMessageSize - message.Length > 0)
                    {
                        if (Constants.ChatMessageSize - message.Length > sendLogMessage.Receiver.Name.Length)
                            message += sendLogMessage.Receiver.Name;
                        else
                            message += sendLogMessage.Receiver.Name.Substring(0, Constants.ChatMessageSize - message.Length);
                    }

                    chatMessages.Enqueue(message);
                    messageTimer.Stop();
                    SendLogMessage();
                    messageTimer.Start();
                }
                break;
            }

            case ServerChatLogMessage logMessage:
            {
                Queue<string> words = new Queue<string>();
                foreach (string word in logMessage.Message.Split(" "))
                    words.Enqueue(word);

                string a = "";
                do
                {
                    if (a.Length + words.Peek().Length < Constants.ChatMessageSize)
                        a += words.Dequeue();
                    else
                    {
                        chatMessages.Enqueue(a);
                        a = "";
                    }

                } while (words.Count > 0);

                break;
            }
        }

    };
}

void ApSync()
{
    foreach (long LocationId in apClient.session.Locations.AllLocationsChecked)
    {
        if (giftMoons.ContainsKey( (int)LocationId))
        {
            giftMoons.Remove((int)LocationId);
        }
    }

    foreach (Archipelago.MultiClient.Net.Models.ItemInfo item in apClient.session.Items.AllItemsReceived)
    {
        if (item.ItemId < 2500)
        {
            if (giftMoons.ContainsKey((int)item.ItemId))
            {
                giftMoons[(int)item.ItemId] = true;
            }
            shineBag.Add((int)item.ItemId);
        }
        if (item.ItemId >= 2502 && item.ItemId < 9990)
        {
            outfitBag.Add((int)item.ItemId);
        }
        if (item.ItemGame == "Super Mario Odyssey" && item.ItemName == apClient.get_goal())
            apClient.session.SetGoalAchieved();
        while (apClient.session.Items.PeekItem() != null)
        {
            apClient.session.Items.DequeueItem();
        }
    }
    SyncItem();
    SyncShineBag();
}

Upnp();
connectAP();
await server.Listen(cts.Token);


if (restartRequested) //need to do this here because this needs to happen after the listener closes, and there isn't an
                      //easy way to sync in the restartserver command without it exiting Main()
{
    string? path = System.Reflection.Assembly.GetEntryAssembly()?.GetName().Name;
    const string unableToStartMsg = "Unable to ascertain the executable location, you'll need to re-run the server manually.";
    if (path != null) //path is probably just "Server", but in the context of the assembly, that's all you need to restart it.
    {
        Console.WriteLine($"Server Running on (pid): {System.Diagnostics.Process.Start(path)?.Id.ToString() ?? unableToStartMsg}");
    }
    else
        consoleLogger.Info(unableToStartMsg);
}
