import asyncio
from bleak import BleakClient

ID = r"\\?\BTHLEDevice#{19b10000-e8f2-537e-4f6c-d104768a1214}_Dev_VID&02e502_PID&a111_REV&0210_e072a1e9f46d#b&1ac73f01&6&0028#{19b10000-e8f2-537e-4f6c-d104768a1214}"

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
