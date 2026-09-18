import asyncio
from winrt.windows.devices.enumeration import DeviceInformation

async def get_paired():
    devices = await DeviceInformation.find_all_async()
    for d in devices:
        if "Viper" in d.name:
            print(f"Name: {d.name}, Id: {d.id}")

if __name__ == '__main__':
    asyncio.run(get_paired())
