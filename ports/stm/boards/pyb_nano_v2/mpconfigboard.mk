USB_VID = 0x239A
USB_PID = 0x8068
USB_PRODUCT = "PYB LR Nano V2"
USB_MANUFACTURER = "MicroPython Chinese Community"

SPI_FLASH_FILESYSTEM = 1
EXTERNAL_FLASH_DEVICES = W25Q64JVxQ

MCU_SERIES = F4
MCU_VARIANT = STM32F411xE
MCU_PACKAGE = UFQFPN48

LD_COMMON = boards/common_default.ld
LD_FILE = boards/STM32F411_fs.ld

# Too big for the flash
CIRCUITPY_AUDIOCORE = 0
CIRCUITPY_AUDIOPWMIO = 0
CIRCUITPY_BITMAPTOOLS = 0
CIRCUITPY_BLEIO_HCI = 0
CIRCUITPY_BUSDEVICE = 0
CIRCUITPY_GIFIO = 0
CIRCUITPY_KEYPAD = 0
CIRCUITPY_MSGPACK = 0
CIRCUITPY_ONEWIREIO = 0
