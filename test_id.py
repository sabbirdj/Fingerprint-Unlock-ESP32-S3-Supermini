import asyncio
from bleak import BleakClient

ID = r"\\?\BTHLE#Dev_e072a1e9f46d#a&13f1cf15&1&e072a1e9f46d#{80795c47-03f0-446d-b1f2-e424b30db4da}"

async def run():
    print(f"Connecting to {ID}...")
    try:
        async with BleakClient(ID, timeout=5.0) as client:
            print("Connected!")
            services = await client.get_services()
            for s in services:
                print(s)
    except Exception as e:
        print(f"Error: {e}")

if __name__ == '__main__':
    asyncio.run(run())
