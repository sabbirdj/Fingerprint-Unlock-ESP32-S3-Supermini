import asyncio
from bleak import BleakClient

MAC = "E0:72:A1:E9:F4:6D"
async def run():
    print(f"Connecting to {MAC}...")
    try:
        async with BleakClient(MAC, timeout=5.0) as client:
            print("Connected!")
            services = await client.get_services()
            for s in services:
                print(s)
    except Exception as e:
        print(f"Error: {e}")

if __name__ == '__main__':
    asyncio.run(run())
