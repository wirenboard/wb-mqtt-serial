var Module =
{
    serial: new SerialPort(),

    async request(type, data)
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
            case 'portScan': this.portScan(JSON.stringify(data)); break;
            case 'deviceLoadConfig': this.deviceLoadConfig(JSON.stringify(data)); break;
            case 'deviceSet': this.deviceSet(JSON.stringify(data)); break;
        }

        this.result = undefined;
        await new Promise(wait.bind(this));
        return this.result;
    },

    onResult(result)
    {
        this.result = JSON.parse(result);
    },

    onError(error)
    {
        this.print('request error: ' + error);
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
        console.log(text);
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

    async sentRequest(start)
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

        Module.print(devices.length + ' devices found');

        devices.forEach(device =>
        {
            let row = table.insertRow();

            for (let i = 0; i < 7; i++)
            {
                let cell = row.insertCell();

                switch (i)
                {
                    case 0: cell.innerHTML = device.device_signature; break;
                    case 1: cell.innerHTML = device.fw_signature; break;
                    case 2: cell.innerHTML = device.cfg.slave_id; break;
                    case 3: cell.innerHTML = device.sn; break;
                    case 4: cell.innerHTML = device.fw.version; break;
                    case 5:
                    {
                        let select = document.createElement('select');
                        let button = document.createElement('button');
                        let deviceType = this.signatureMap[device.device_signature];

                        this.baudRates.forEach(baudRate => select.innerHTML += '<option' + (baudRate == device.cfg.baud_rate ? ' selected' : '') + '>' + baudRate + '</option>');

                        button.innerHTML = 'set';
                        button.addEventListener('click', async function()
                        {
                            let value = parseInt(select.value);

                            let request =
                            {
                                baud_rate: device.cfg.baud_rate,
                                data_bits: 8,
                                parity: 'N',
                                stop_bits: 2,
                                device_type: deviceType ?? device.device_signature,
                                slave_id: device.cfg.slave_id,
                                parameters: {baud_rate: value / 100}
                            };

                            if (await Module.request('deviceSet', request) !== null)
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
                        let deviceType = this.signatureMap[device.device_signature];

                        button.innerHTML = 'read params';
                        button.addEventListener('click', async function()
                        {
                            let request =
                            {
                                baud_rate: device.cfg.baud_rate,
                                data_bits: 8,
                                parity: 'N',
                                stop_bits: 2,
                                device_type: deviceType ?? device.device_signature,
                                slave_id: device.cfg.slave_id
                            };

                            console.log(request);

                            let result = await Module.request('deviceLoadConfig', request);
                            Module.print(JSON.stringify(result, 0, 2));
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
