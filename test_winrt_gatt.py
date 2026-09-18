import asyncio
from winrt.windows.devices.bluetooth.genericattributeprofile import GattDeviceService
from winrt.windows.devices.enumeration import DeviceInformation
from winrt.windows.storage.streams import DataWriter

async def send():
    aqs = 'System.Devices.Aep.ProtocolId:="{bb7bb05e-5972-42b5-94fc-76eaa7084d49}"'
    devices = await DeviceInformation.find_all_async()
    target_id = None
    for d in devices:
        if "Viper" in d.name and "19b10000" in d.id:
            target_id = d.id
            break
            
    if not target_id:
        print("Service not found")
        return
        
    print(f"Connecting to service {target_id}...")
    service = await GattDeviceService.from_id_async(target_id)
    if not service:
        print("Failed to open service")
        return
        
    chars = await service.get_characteristics_async()
    for c in chars.characteristics:
        if "19b10001" in str(c.uuid).lower():
            writer = DataWriter()
            writer.write_string("CMD:LOCK\n")
            res = await c.write_value_async(writer.detach_buffer())
            print(f"Write result: {res}")
            return
    print("Char not found")

if __name__ == '__main__':
    asyncio.run(send())
