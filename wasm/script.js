var Relay = 0;
var Port;

var Module =
{
    print(text)
    {
        let output = document.querySelector('#output');
        output.value += text + "\n";
        output.scrollTop = output.scrollHeight;
        console.log(text);
    },
    setStatus(text)
    {
        console.log(text);
    }
};

class SerialPort
{
    replyTimeout = 1000; // is 1 second enough?
    isOpen = false;

    constructor()
    {
        if (navigator.serial)
            return;

        alert('Web Serial API is not supported by this browser :(');
    }

    async select(force)
    {
        if (this.serial && !force)
            return;

        this.serial = await navigator.serial.requestPort();
    }

    async open(baudRate, dataBits, parity, stopBits)
    {
        switch (String.fromCharCode(parity))
        {
            case 'N': parity = 'none'; break;
            case 'E': parity = 'even'; break;
            case 'O': parity = 'odd'; break;
            default: console.error("Invalid parity value: ", parity); return;
        }

        if (this.isOpen)
            await this.close();

        try
        {
            await this.select(false);
            await this.serial.open({baudRate: baudRate, dataBits: dataBits, parity: parity, stopBits: stopBits});
            this.isOpen = true;
        }
        catch (error)
        {
            console.error("Can't open serial port: ", error);
        }
    }

    async close()
    {
        if (!this.serial && !this.isOpen)
            return;

        await this.serial.close();
        this.isOpen = false;
    }

    async write(data)
    {
        if (!this.serial || !this.serial.writable)
        {
            console.error("Serial port is not open or not writable");
            return;
        }

        const writer = this.serial.writable.getWriter();
        await writer.write(data);
        writer.releaseLock();
    }

    async read(length)
    {
        if (!this.serial || !this.serial.readable)
        {
            console.error("Serial port is not open or not readable");
            return;
        }

        const reader = this.serial.readable.getReader();
        let read = true;

        async function receive()
        {
            let data = new Uint8Array(length);
            let offset = 0;

            while (read)
            {
                let {value} = await reader.read();

                if (!read)
                    break;

                value = value.subarray(0, length - offset);
                data.set(value, offset);
                offset += value.length;

                if (length > offset)
                    continue;

                read = false;
                reader.releaseLock();
            }

            return data;
        }

        async function wait(timeout)
        {
            await new Promise(resolve => setTimeout(resolve, timeout));

            if (read)
            {
                read = false;
                reader.cancel();
            }

            return false;
        }

        return await Promise.race([receive(), wait(this.replyTimeout)]);
    }
}

window.onload = function()
{
    Port = new SerialPort();

    document.querySelector('#portButton').addEventListener('click', async function()
    {
        await Port.select(true);
    });

    document.querySelector('#requestButton').addEventListener('click', async function()
    {
        let request =
        {
            device_type: "WB-MR6C v.3",
            slave_id: 25
        };

        Module.deviceLoadConfig(JSON.stringify(request));
    });

    document.querySelector('#testButton1').addEventListener('click', async function()
    {
        Relay = Relay ? 0 : 1;

        let request =
        {
            device_type: "WB-MR6C v.3",
            slave_id: 25,
            channels: {K1: Relay}
        };

        Module.deviceSet(JSON.stringify(request));
    });

        document.querySelector('#testButton2').addEventListener('click', async function()
    {
        Relay = Relay ? 0 : 1;

        let request =
        {
            baud_rate: 115200,
            device_type: "WB-MR6C",
            slave_id: 221,
            channels: {K1: Relay}
        };

        Module.deviceSet(JSON.stringify(request));
    });
}
