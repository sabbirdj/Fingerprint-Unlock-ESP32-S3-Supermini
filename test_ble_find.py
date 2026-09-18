import asyncio
from bleak import BleakScanner

async def run():
    print("Scanning...")
    d = await BleakScanner.find_device_by_name("Viper's Biometric Key")
    if d:
        print(f"Found: {d.name} {d.address}")
    else:
        print("Not found")

if __name__ == '__main__':
    asyncio.run(run())
