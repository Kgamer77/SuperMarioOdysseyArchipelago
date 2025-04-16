# Super Mario Odyssey - Archipelago Mod
A mod adding Archipelago Multi World support to Super Mario Odyssey.
WARNING: This mod only works on version 1.0.0 of Super Mario Odyssey.

## Features
- Randomize Power Moons
- Supports all non-achievement (Toadette Moon) power moon locations.
- Supports all outfits, stickers, and souvenirs.
- Choose which kingdom is your win condition.

## Installation and Usage
* Install the latest version from the releases page. `Server_Vx.x.zip`, `smo.apworld`, and either `SMO_Archipelago_Vx.x_Switch.zip` for console or `SMO_Archipelago_Vx.x_Emu.zip` for emulator.
* Extract `Server_Vx.x.zip` to your directory of choice. Run `Server.exe` to start the server. On the first run, `settings.json` will be generated.
* Place `smo.apworld` in the `custom_worlds` folder of your Archipelago install which the default directory is `C:\ProgramData\Archipelago\custom_worlds`

<details>
<summary>Switch</summary> 
  
Extract `SMO_Archipelago_Vx.x_Switch.zip` and Place the `atmosphere` folder onto the root of your sd card.

</details>

<details>
<summary>Emulator</summary>

### Ryujinx
Extract `SMO_Archipelago_Vx.x_Emu.zip` and Place `SMOAP` folder in the mods directory for Super Mario Odyssey.

</details>

Connecting to the Server from Super Mario Odyssey.
- When prompted, the `IP Address` you are connecting to is your computer's local ipv4 this is found by entering the `ipconfig` command into command prompt on Windows.
- When prompted, the `Port` is `1027` by default which does not need to be changed.

`settings.json` has fields for the `Archipelago` connection.
- `Server` is where the ip address or url of the AP Server or room your joining goes
- `Port` is for the port you connect to the Archipelago over this should be left as the default `38281` unless specified by the host.
- `Slot` is where you put the name of your slot for the Archipelago.
- `Password` is where you put the password for the Archipelago you are joining. Leave this blank `""` for no password.
- `FillerIndexes` is an internal field used by the server and shouldn't be changed manually.
  
`Server` field in `settings.json` shouldn't be changed unless you know what you're doing.

<details>
<summary>Joining an Archipelago</summary> 

Run `Server.exe` and it should connect to the Archipelago room automatically.

If you see the error
```
Failed to Connect to <address> as <slot_name>:
    Connection timed out.
```
The Archipelago room may not be opened. Enter `reconnect` to attempt to reconnect to the Archipelago room.

If you see the error
```
Failed to Connect to <address> as <slot_name>:
    The slot name did not match any slot on the server.
    InvalidSlot
```
The `slot_name` in your settings was not a slot in the room you are trying to connect to. Enter `reconnect <slot_name>` to change your slot name and attempt to reconnect to the room.

</details>

Credits
- [Sanae](https://github.com/sanae6) Author of original server code
- [CraftyBoss](https://github.com/CraftyBoss) Author of SMO Online
- All other contributors to the aforementioned repos.
