# Super Mario Odyssey - Archipelago Mod
A mod adding Archipelago Multi World support to Super Mario Odyssey.
WARNING: This mod only works on version 1.0.0 of Super Mario Odyssey.

## Features
- Randomize Power Moons
- Supports all non-achievement (Toadette Moon) power moon locations.
- Supports all outfits, stickers, and souvenirs.
- Choose which kingdom is your win condition.

## Installation and Usage
* Install the latest version from the releases page. `smo.apworld` and either `SMO_Archipelago_Vx.x_Switch.zip` for console or `SMO_Archipelago_Vx.x_Emu.zip` for emulator.
* Place `smo.apworld` in the `custom_worlds` folder of your Archipelago install which the default directory is `C:\ProgramData\Archipelago\custom_worlds`

<details>
<summary>Switch</summary> 
  
- Extract `SMO_Archipelago_Vx.x_Switch.zip` and Place the `atmosphere` folder onto the root of your sd card.

</details>

<details>
<summary>Emulator</summary>

### Ryujinx (Cannot Send Checks)
- Extract `SMO_Archipelago_Vx.x_Emu.zip` and Place `SMOAP` folder in the mods directory for Super Mario Odyssey.

### Suyu
- Right Click on Super Mario Odyssey in the game menu and select `Open Mod Data Location`.
- Extract `SMO_Archipelago_Vx.x_Emu.zip` and Place `SMOAP` folder in the mods directory that opened.
</details>

Connecting to the Connector from Super Mario Odyssey.
- When prompted, the `IP Address` you are connecting to is your computer's local ipv4 this is found by entering the `ipconfig` command into command prompt on Windows.
- When prompted, the `Port` is `1027` by default which does not need to be changed.

Using Options that Generate a Patch
- If a world is generated using options that require additional romFS patches (`shop_sanity`, `colors`, `counts`) then an `<seed><slot><slot_name>.apsmo` file will be generated.
- To generate the patch files open the patch file with the `Open Patch` option in the Archipelago Launcher.
- This requires you to have an extracted romfs for Super Mario Odyssey (methods for extracting romfs will not be covered here) which you will be prompted to select the directory of when opening a patch file for the first time.
<details>
<summary>Switch</summary> 
  
- Place the generated `atmosphere` folder onto the root of your sd card and select `Replace the files in the destination` if prompted.

</details>

<details>
<summary>Emulator</summary>
  
### Suyu (Yuzu forks)
- Right Click on Super Mario Odyssey in the game menu and select `Open Mod Data Location`.
- Place `romfs` folder in the `SMOAP` folder that opened and select `Replace the files in the destination` if prompted.
</details>

Credits
- [Sanae](https://github.com/sanae6) Author of original server code
- [CraftyBoss](https://github.com/CraftyBoss) Author of SMO Online
- All other contributors to the aforementioned repos.
