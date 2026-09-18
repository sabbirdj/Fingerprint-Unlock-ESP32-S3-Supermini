import asyncio
from bleak import BleakScanner, BleakClient

async def run():
    print("Scanning...")
    devices = await BleakScanner.discover(timeout=3.0)
    for d in devices:
        if d.name and "Viper" in d.name:
            print(f"Found: {d.name} {d.address}")
            try:
                print("Connecting...")
                async with BleakClient(d.address) as client:
                    print("Connected! Reading services...")
            except Exception as e:
                print(f"Error: {e}")
            return
    print("Not found")

if __name__ == '__main__':
    asyncio.run(run())
