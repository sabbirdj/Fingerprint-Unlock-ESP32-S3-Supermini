import asyncio
from bleak import BleakClient
from bleak.backends.winrt.scanner import BleakScannerWinRT

async def test():
    print("Searching for paired devices...")
    devices = await BleakScannerWinRT.discover()
    for d in devices:
        if d.name and "Viper" in d.name:
            print(f"Found {d.name} at {d.address}")
            try:
                async with BleakClient(d) as client:
                    print("Connected!")
                    return
            except Exception as e:
                print(f"Error: {e}")
    print("Not found")

if __name__ == '__main__':
    asyncio.run(test())
