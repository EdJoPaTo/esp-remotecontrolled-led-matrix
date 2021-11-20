# ESP Remotecontrolled LED Matrix

Tested with a Neopixel LED Matrix on ESP8266 and [this ESP32 I2S Shield](https://github.com/witnessmenow/ESP32-i2s-Matrix-Shield).

## Protocol

When connecting you get 2 bytes (8 bit unsigned): width and height.
Nothing is sent afterwards.

Now you can send your commands.
The first byte indicates the command you want to send.
Bytes after that are specific to the command.
Each byte is 8 bits unsigned.

See this pseudocode for the available commands:

```rust
/// Fill the whole matrix with one color
fn fill(red: u8, green: u8, blue: u8) {
    client.send([1, red, green, blue]);
}

/// Set one pixel of the matrix to the given color
fn pixel(x: u8, y: u8, red: u8, green: u8, blue: u8) {
    client.send([2, x, y, red, green, blue]);
}

/// Set rectangular area to the given color
fn pixel(x: u8, y: u8, width: u8, height: u8, red: u8, green: u8, blue: u8) {
    client.send([3, x, y, width, height, red, green, blue]);
}
```

There is a [Rust client library](https://github.com/EdJoPaTo/esp-wlan-led-matrix-rust-client) available.

## MQTT

`bri`ghtness and `on` can be controlled via MQTT (`<name>/set/bri`).

Also, some status updates are published to MQTT (`<name>/status/bri`):
- `bri`
- `clients`
- `commands-per-second`
- `kilobytes-per-second`
- `on`
- `rssi`
