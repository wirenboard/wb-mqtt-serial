var Relay = 0, Port, Handler, Scan;

var Module =
{
    print(text)
    {
        AppendOutput(text);
        console.log(text);
    },
    setStatus(text)
    {
        console.log(text);
    }
};

function AppendOutput(text)
{
    let output = document.querySelector('#output');
    output.value += text + '\n';
    output.scrollTop = output.scrollHeight;
}

function OnResult(result)
{
    Handler.result = JSON.parse(result);
}

function OnError(error)
{
    AppendOutput('error' + error);
    console.error(error);
}

class SerialPort
{
    replyTimeout = 500; // is 500 ms enough?
    options = new Object();
    isOpen = false;

    constructor()
    {
        if (navigator.serial)
            return;

        alert('Web Serial API is not supported by this browser :(');
    }

    setOptions(baudRate, dataBits, parity, stopBits)
    {
        switch (String.fromCharCode(parity))
        {
            case 'E': this.options.parity = 'even'; break;
            case 'O': this.options.parity = 'odd';  break;
            default:  this.options.parity = 'none'; break;
        }

        this.options.baudRate = baudRate;
        this.options.dataBits = dataBits;
        this.options.stopBits = stopBits;
    }

    async select(force)
    {
        if (this.serial && !force)
            return;

        this.serial = await navigator.serial.requestPort();
    }

    async open()
    {
        if (this.isOpen)
            await this.close();

        try
        {
            await this.select(false);
            await this.serial.open(this.options);
            this.isOpen = true;
        }
        catch (error)
        {
            console.error('Can not open serial port: ', error);
            delete this.serial;
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
        await this.open();

        if (!this.serial || !this.serial.writable)
        {
            console.error('Serial port is not open or not writable');
            return;
        }

        const writer = this.serial.writable.getWriter();
        await writer.write(data);
        writer.releaseLock();
    }

    async read(count)
    {
        if (!this.serial || !this.serial.readable)
        {
            console.error('Serial port is not open or not readable');
            return;
        }

        const reader = this.serial.readable.getReader();
        let data = new Uint8Array();
        let read = true;

        async function receive()
        {
            while (read)
            {
                let {value} = await reader.read();

                if (!read)
                    break;

                let buffer = new Uint8Array(data.length + value.length);
                buffer.set(data, 0)
                buffer.set(value, data.length);
                data = buffer;

                if (data.length < count)
                    continue;

                read = false;
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

            return data;
        }

        let result = await Promise.race([receive(), wait(this.replyTimeout)]);
        reader.releaseLock();
        return result;
    }
}

class RequestHandler
{
    async sendRequest(type, request)
    {
        function wait(resolve)
        {
            if (this.result === undefined)
                setTimeout(wait.bind(this, resolve), 10);
            else
                resolve();
        }

        switch (type)
        {
            case 'portScan': Module.portScan(JSON.stringify(request)); break;
            case 'deviceLoadConfig': Module.deviceLoadConfig(JSON.stringify(request)); break;
            case 'deviceSet': Module.deviceSet(JSON.stringify(request)); break;
        }

        this.result = undefined;
        await new Promise(wait.bind(this));
        return this.result;
    }
}

class PortScan
{
    index = 0;
    baudRates = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200];

    async sentRequest(start)
    {
        AppendOutput('Scan ' + (start ? 'start' : 'next') + ': ' + this.baudRates[this.index]);

        let request =
        {
            command: 96,
            mode: start ? 'start' : 'next',
            baud_rate: this.baudRates[this.index],
            data_bits: 8,
            parity: 'N',
            stop_bits: 2
        };

        return await Handler.sendRequest('portScan', request);
    }

    async exec()
    {
        let devices = new Array();
        let start = true;

        while (this.index < this.baudRates.length)
        {
            let data = await this.sentRequest(start);

            if (data.devices?.length)
            {
                data.devices.forEach(device => devices.push(device));
                start = false;
                continue;
            }

            this.index++;
            start = true;
        }

        AppendOutput(devices.length + ' devices found' + (devices.length ? ':' : ''));

        devices.forEach(device =>
        {
            AppendOutput(device.device_signature + ' ' + device.fw.version + ' (' + device.sn + ') ' + device.cfg.slave_id + ' [' + device.cfg.baud_rate + ']');
        });
    }
}

window.onload = function()
{
    Port = new SerialPort();
    Handler = new RequestHandler();

    document.querySelector('#portButton').addEventListener('click', async function()
    {
        await Port.select(true);
    });

    document.querySelector('#scanButton').addEventListener('click', async function()
    {
        await new PortScan().exec();
    });

    document.querySelector('#requestButton').addEventListener('click', async function()
    {
        let request =
        {
            baud_rate: 9600,
            data_bits: 8,
            parity: 'N',
            stop_bits: 2,
            device_type: 'WB-MR6C v.3',
            slave_id: 25
        };

        AppendOutput(await Handler.sendRequest('deviceLoadConfig', request));
    });

    document.querySelector('#testButton1').addEventListener('click', async function()
    {
        Relay = Relay ? 0 : 1;

        let request =
        {
            baud_rate: 9600,
            data_bits: 8,
            parity: 'N',
            stop_bits: 2,
            device_type: 'WB-MR6C v.3',
            slave_id: 25,
            channels: {K1: Relay}
        };

        await Handler.sendRequest('deviceSet', request);
    });

    document.querySelector('#testButton2').addEventListener('click', async function()
    {
        Relay = Relay ? 0 : 1;

        let request =
        {
            baud_rate: 115200,
            data_bits: 8,
            parity: 'N',
            stop_bits: 2,
            device_type: 'WB-MR6C',
            slave_id: 221,
            channels: {K1: Relay}
        };

        await Handler.sendRequest('deviceSet', request);
    });
}
