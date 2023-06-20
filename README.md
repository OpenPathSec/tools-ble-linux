Raw BLE advertisement tool
====
This tool is for receiving/sending raw BLE advertisement messages.
The project is based on the Linux Bluetooth protocol stack, [BlueZ](http://www.bluez.org/).

The <Makefile> is configured for Raspberry Pi 3 and LIB_DIRECTORY needs to be set properly for other environments.

## Preparation
Install dependencies.

```
sudo apt update && sudo apt-get -y install libdbus-1-dev libdbus-glib-1-dev libglib2.0-dev libical-dev libreadline-dev libudev-dev libusb-dev make bluetooth bluez-utils libbluetooth-dev
```

## Compilation
```
make
```

## Usage
The following shows the usage.
```
./bletool -h
```

If you want to receive advertisements, use -r option to set the tool *receive mode*.
```
./bletool -r
```

If you want to send advetisement, use -s option to set the tool *send mode*.
```
./bletool -s 00112233445566778899
```
The parameter following `-s` is the hex string of the message to advertise.

