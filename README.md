# Program AT89S52 with Arduino

## Step 1: Prepare the Arduino ISP

Before connecting the AT89S52, you must turn your Arduino into a programmer.

1. Open the **Arduino IDE**.
2. Go to `File` > `Examples` > `11.ArduinoISP` > **ArduinoISP**.
3. Upload this sketch to your **Arduino Uno**.

------

## Step 2: Wiring the Hardware

Connect the Arduino to the AT89S52 as shown below.

| **AT89S52 Pin** | **Function** | **Arduino Pin** |
| --------------- | ------------ | --------------- |
| **Pin 9**       | RESET        | **Pin 10**      |
| **Pin 6**       | MOSI         | **Pin 11**      |
| **Pin 7**       | MISO         | **Pin 12**      |
| **Pin 8**       | SCK          | **Pin 13**      |
| **Pin 40**      | VCC (+5V)    | **5V**          |
| **Pin 20**      | GND          | **GND**         |

> **⚠️ Important:** Place a **10uF capacitor** between the Arduino's **Reset** and **GND** (Long leg to Reset, short to GND) *after* uploading the ISP sketch. This prevents the Arduino from resetting itself during the upload process.

<a href="https://ibb.co/Y7W1xCMb"><img src="https://i.ibb.co/wNLPfkXY/IMG-5996.avif" alt="IMG-5996" border="0"></a>
<a href="https://ibb.co/zT3b7PWy"><img src="https://i.ibb.co/4nbMY4w6/IMG-5995.avif" alt="IMG-5995" border="0"></a>





------

## Step 3: Download AVRDUDE

1. Download via this link: [AVRDUDE](https://github.com/avrdudes/avrdude/releases)

2. Preferred download download `avrdude-v8.1-windows-x64.zip`
3. Unzip them to a folder, should contain this 3 files in a single folder:  `avrdude.conf`, `avrdude.exe`, `avrdude.pdp`

------

## Step 4: Write and Compile your Code

C

```
#include <reg52.h>

sbit LED = P1^0; // Define LED on Port 1, Pin 0

void main() {
    LED = 0;    // Turn LED ON (assuming active low)
    while(1) {
        // Infinite loop to stay here
    }
}
```

Compile this in your IDE (like Keil) to generate a `.hex` file.

------

## Step 5: Uploading the Code

Copy the `.hex` file into the AVRDUDE folder. In the folder,`Shift + Right Click` choose to open terminal or PowerShell. Run the following command:

PowerShell

```
.\avrdude.exe -C .\avrdude.conf -c avrisp -P COM3 -b 19200 -p 89s52 -U flash:w:your_file.hex:i
```

### Command Breakdown:

- `-c avrisp`: Uses the Arduino as an AVRISP programmer.
- `-P COM3`: Replace with your actual Arduino COM port.
- `-b 19200`: The standard baud rate for ArduinoISP.
- `-p 89s52`: Identifies the target chip5.
- `-U flash:w:...`: Commands AVRDUDE to **w**rite the hex file to **flash** memory.

### Example OUTPUT:

```cmd
PS C:\Users\yoyon\Downloads\avrdude> .\avrdude.exe -C .\avrdude.conf -c avrisp -P COM3 -b 19200 -p 89s52 -U flash:w:blinky.hex:i
Reading 62 bytes for flash from input file blinky.hex
Writing 62 bytes to flash
Writing | ################################################## | 100% 1.20 s
Reading | ################################################## | 100% 0.41 s
62 bytes of flash verified

Avrdude done.  Thank you.
```

You can use the given `.hex` file and try it:

- `led.hex` using P1^0.

- blinky.hex using P1^2.
