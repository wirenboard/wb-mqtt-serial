var Module =
{
    serial: new SerialPort(),

    async request(type, data)
    {
        this.finished = false;

        function wait(resolve)
        {
            if (this.finished)
            {
                resolve();
                return;
            }

            setTimeout(wait.bind(this, resolve), 1);
        }

        switch (type)
        {
            case 'portScan': this.portScan(JSON.stringify(data)); break;
            case 'deviceLoadConfig': this.deviceLoadConfig(JSON.stringify(data)); break;
            case 'deviceSet': this.deviceSet(JSON.stringify(data)); break;
        }

        await new Promise(wait.bind(this));
        return this.reply;
    },

    parseReply(reply)
    {
        this.reply = JSON.parse(reply);

        if (this.reply.error)
            this.print('request error ' + this.reply.error.code + ': ' + this.reply.error.message);

        this.finished = true;
    },

    setStatus(text)
    {
        this.print(text);
    },

    print(text)
    {
        let output = document.querySelector('#output');
        output.value += text + '\n';
        output.scrollTop = output.scrollHeight;
    }
};

class PortScan
{
    baudRates = [115200, 57600, 38400, 19200, 9600, 4800, 2400, 1200];
    index = 0;

    signatureMap =
    {
        'MAP3E': 'WB-MAP3E fw2',
        'MAP6S': 'WB-MAP6S fw2',
        'MAP12E': 'WB-MAP12E fw2',
        'MR6CU': 'WB-MR6CU',
        'WBMAO4': 'tpl1_wb_mao4',
        'WBMCM8': 'WB-MCM8',
        'WBMD3': 'tpl1_wb_mdm3',
        'MRWL3': 'WB-MR3',
        'WBMAI6': 'WB-MAI6',
        'WBMR6': 'WB-MR6C',
        'WB-UPS v.3': 'wb_ups_v3'
    }

    async request(start)
    {
        Module.print('Scan ' + (start ? 'start' : 'next') + ': ' + this.baudRates[this.index]);

        let request =
        {
            command: 96,
            mode: start ? 'start' : 'next',
            baud_rate: this.baudRates[this.index],
            data_bits: 8,
            parity: 'N',
            stop_bits: 2
        };

        return await Module.request('portScan', request);
    }

    async scan()
    {
        let table = document.querySelector('table');
        let devices = new Array();
        let start = true;

        table.innerHTML = null;

        while (this.index < this.baudRates.length)
        {
            let reply = await this.request(start);

            if (reply.result?.devices?.length)
            {
                reply.result.devices.forEach(device => devices.push(device));
                start = false;
                continue;
            }

            this.index++;
            start = true;
        }

        Module.print(devices.length + ' devices found');

        devices.forEach(device =>
        {
            let deviceType = this.signatureMap[device.device_signature];
            let row = table.insertRow();

            for (let i = 0; i < 7; i++)
            {
                let cell = row.insertCell();

                switch (i)
                {
                    case 0: cell.innerHTML = device.device_signature; break;
                    case 1: cell.innerHTML = device.sn; break;
                    case 2: cell.innerHTML = device.fw_signature; break;
                    case 3: cell.innerHTML = device.fw.version; break;
                    case 4: cell.innerHTML = device.cfg.slave_id; break;

                    case 5:
                    {
                        let select = document.createElement('select');
                        let button = document.createElement('button');

                        this.baudRates.reverse().forEach(baudRate => select.innerHTML += '<option' + (baudRate == device.cfg.baud_rate ? ' selected' : '') + '>' + baudRate + '</option>');

                        button.innerHTML = 'set';
                        button.addEventListener('click', async function()
                        {
                            let value = parseInt(select.value);
                            let request = {...device.cfg, device_type: deviceType ?? device.device_signature, parameters: {baud_rate: value / 100}};
                            let reply = await Module.request('deviceSet', request);

                            if (reply.error)
                                return;

                            device.cfg.baud_rate = value;
                        });

                        cell.append(select);
                        cell.append(button);
                        break;
                    }

                    case 6:
                    {
                        let button = document.createElement('button');

                        button.innerHTML = 'read params';
                        button.addEventListener('click', async function()
                        {
                            let request = {...device.cfg, device_type: deviceType ?? device.device_signature};
                            let reply = await Module.request('deviceLoadConfig', request);

                            if (reply.error)
                                return;

                            Module.print(JSON.stringify(reply.result, null, 2));
                        });

                        cell.append(button);
                        break;
                    }
                }
            }
        });
    }
}

var Relay = 0;

window.onload = function()
{
    document.querySelector('#portButton').addEventListener('click', async function()
    {
        await Module.serial.select(true);
    });

    document.querySelector('#scanButton').addEventListener('click', async function()
    {
        await new PortScan().scan();
    });
}
