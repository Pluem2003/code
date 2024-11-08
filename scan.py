import asyncio
from bleak import BleakScanner

async def run():
    devices = await BleakScanner.discover()
    for device in devices:
        print(device)

asyncio.run(run())
