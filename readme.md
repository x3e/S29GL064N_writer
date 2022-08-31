# S29GL064N/S29GL032N flash writer
This is a quick and dirty program that runs on an ESP32-S2 to read and re-flash an S29GL064N flash chip. It is specifically used to reflash a [game console](https://forums.nesdev.org/viewtopic.php?t=23807), so it has some odd properties, for example it will shuffle the bit-order of the data read and written to the flash.

Maybe someone will find this useful as an example of how to interface with that specific chip.

## Wiring
I used three SNx4HC595 shift registers to address the flash chip. The output pins of these shift registers are connected to the A0 - A16 pins of the flash chip. The RESET# pin is connected to +3v3. See the first few lines of main.cpp for the rest of the connections.